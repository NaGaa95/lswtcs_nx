/* egl.c -- logging + compatibility wrappers around mesa's EGL
 *
 * LSWTCS asks eglChooseConfig for a config with
 * EGL_SURFACE_TYPE = WINDOW|PBUFFER (it creates pbuffer surfaces with shared
 * contexts for its loader threads). The Switch mesa platform has no
 * pbuffer-capable configs, so the request matches nothing, the game blindly
 * continues with a garbage config and ends up calling glGetString with no
 * context current (hardware crash log #3: NULL deref in
 * NuDeviceSpecs::DetermineDeviceSpecs <- InitialiseOpenGLContext).
 *
 * Countermeasures:
 *  - eglChooseConfig strips the PBUFFER bit and falls back to relaxed
 *    attribute lists if the request still matches nothing;
 *  - eglCreatePbufferSurface hands out a fake surface handle when the real
 *    call fails; eglMakeCurrent maps it to surfaceless binding (the loader
 *    contexts only upload resources, they never present);
 *  - glGetString never returns NULL (Tegra-flavored fallbacks; the game only
 *    sniffs for "Adreno"/"320"/" 1.");
 *  - everything is logged so debug.log shows the whole EGL handshake.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "../config.h"
#include "../util.h"
#include "../hooks.h"

// The Switch mesa stack has no pbuffer-capable window configs.  Earlier
// builds mapped fake pbuffers to surfaceless contexts, but the game does issue
// occasional default-framebuffer work while a pbuffer context is current.  Give
// those contexts a tiny real GL FBO so FBO 0 behaves like a 1x1 pbuffer instead
// of an incomplete surfaceless framebuffer.
#define FAKE_PBUFFER_MAGIC 0x50425546u /* "PBUF" */
#define MAX_FAKE_PBUFFERS 8

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif

typedef struct PbufferTarget {
  EGLContext ctx;
  GLuint fbo;
  GLuint color;
  int ready;
  struct PbufferTarget *next;
} PbufferTarget;

typedef struct FakePbuffer {
  uint32_t magic;
  EGLint width;
  EGLint height;
  PbufferTarget *targets;
} FakePbuffer;

static FakePbuffer *fake_pbuffers[MAX_FAKE_PBUFFERS];
static int fake_pbuffer_count = 0;

#define FAKE_CONTEXT_MAGIC 0x43545841u /* "CTXA" */
#define MAX_FAKE_CONTEXTS 8

typedef struct FakeContext {
  uint32_t magic;
  EGLContext real;
  EGLContext share;
} FakeContext;

static FakeContext *fake_contexts[MAX_FAKE_CONTEXTS];
static int fake_context_count = 0;

static __thread EGLContext current_ctx = EGL_NO_CONTEXT;
static __thread EGLContext current_real_ctx = EGL_NO_CONTEXT;
static __thread EGLSurface current_draw = EGL_NO_SURFACE;
static __thread EGLSurface current_read = EGL_NO_SURFACE;
static __thread FakePbuffer *current_fake_draw = NULL;
static __thread GLuint current_virtual_fbo = 0;
static __thread int gl_context_depth = 0;
static EGLDisplay last_dpy = EGL_NO_DISPLAY;
static EGLSurface main_window_surface = EGL_NO_SURFACE;
static EGLContext main_real_context = EGL_NO_CONTEXT;

// ---------------------------------------------------------------------------
// ONE real context + virtual per-game-context state (the Vita port's model)
//
// The game juggles four EGL contexts across threads with Android-permissive
// timing. eglMakeCurrent is virtualized: each thread tracks what it thinks
// is current (TLS above) and the real binding happens lazily at the next GL
// call. Real shared mesa contexts do NOT work here: GLES sharing requires
// the consumer to re-bind objects after the producer's flush, which the game
// never does, so loader uploads stay invisible (black). Like the Vita port
// (gm666q/lswtcs-vita), everything aliases ONE real context:
//  - the real drawable is always the window surface; logical pbuffer
//    surfaces redirect FBO 0 to a backing FBO,
//  - each logical context shadows the bindings the game sets; the shadow is
//    replayed when the real context switches logical tenants,
//  - cross-thread handoff via sticky ownership: a thread keeps the real
//    binding until another asks (release_requested), serviced at GL
//    boundaries, parks and service points. The render thread keeps it across
//    parks (its wait shims poll in short slices), so uncontended frames cost
//    zero real makeCurrent.
// ---------------------------------------------------------------------------

#define MAX_CTX_OWNERS 8

typedef struct CtxOwner {
  EGLContext ctx;
  pthread_t owner;
  int owner_valid;
  volatile int release_requested;
} CtxOwner;

static CtxOwner ctx_owners[MAX_CTX_OWNERS];
static int ctx_owner_count = 0;
static pthread_mutex_t gl_owner_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gl_owner_cond = PTHREAD_COND_INITIALIZER;

// what this thread REALLY has current right now
static __thread EGLContext tls_bound_ctx = EGL_NO_CONTEXT;
static __thread EGLSurface tls_bound_draw = EGL_NO_SURFACE;
static __thread FakePbuffer *tls_bound_fake = NULL;
static __thread GLuint tls_bound_vfbo = 0;
static __thread CtxOwner *tls_bound_rec = NULL;

volatile int egl_real_makecurrent_count = 0;
volatile int egl_owner_release_count = 0;

// find or create the owner record for a real context; call with mutex held
static CtxOwner *ctx_owner_rec(EGLContext ctx) {
  for (int i = 0; i < ctx_owner_count; i++)
    if (ctx_owners[i].ctx == ctx)
      return &ctx_owners[i];
  if (ctx_owner_count < MAX_CTX_OWNERS) {
    CtxOwner *r = &ctx_owners[ctx_owner_count++];
    r->ctx = ctx;
    r->owner_valid = 0;
    r->release_requested = 0;
    return r;
  }
  debugPrintf("EGL: ctx owner table full!\n");
  return &ctx_owners[0];
}

static GLuint current_fake_pbuffer_fbo(void);

// ---------------------------------------------------------------------------
// per-logical-context shadow state
//
// All logical contexts share one real context; without this a loader's binds
// would leak into the renderer's next draw. Hooks record what each logical
// context last set; apply_ctx_shadow() replays it on tenant switch. A shadow
// is written only by the thread its context is current on and read only by
// the ownership holder, so the owner handshake is locking enough.
// ---------------------------------------------------------------------------

#define SHADOW_TEX_UNITS 8

typedef void (*PFNGLBINDVERTEXARRAYOESPROC)(GLuint);
static PFNGLBINDVERTEXARRAYOESPROC real_glBindVertexArrayOES;

typedef struct CtxShadow {
  EGLContext ctx;
  GLuint program;
  GLenum active_texture; // GL_TEXTURE0 + n
  GLuint tex2d[SHADOW_TEX_UNITS];
  GLuint texcube[SHADOW_TEX_UNITS];
  GLuint array_buffer;
  GLuint element_buffer;
  GLuint vao;
  GLint vp[4];
  int vp_set;
  GLfloat clear_color[4];
  int clear_color_set;
} CtxShadow;

#define MAX_CTX_SHADOWS (MAX_FAKE_CONTEXTS + 2)
static CtxShadow ctx_shadows[MAX_CTX_SHADOWS];
static volatile int ctx_shadow_count = 0;
// highest texture unit the game ever selected; bounds the replay loop
static volatile int shadow_unit_watermark = 2;
// which logical context's shadow the real context currently reflects;
// only ever touched by the thread holding the real binding
static EGLContext g_applied_logical = EGL_NO_CONTEXT;

static CtxShadow *ctx_shadow(EGLContext ctx) {
  int n = ctx_shadow_count;
  for (int i = 0; i < n; i++)
    if (ctx_shadows[i].ctx == ctx)
      return &ctx_shadows[i];
  pthread_mutex_lock(&gl_owner_mutex);
  n = ctx_shadow_count;
  for (int i = 0; i < n; i++)
    if (ctx_shadows[i].ctx == ctx) {
      pthread_mutex_unlock(&gl_owner_mutex);
      return &ctx_shadows[i];
    }
  CtxShadow *s = NULL;
  if (n < MAX_CTX_SHADOWS) {
    s = &ctx_shadows[n];
    memset(s, 0, sizeof(*s));
    s->ctx = ctx;
    s->active_texture = GL_TEXTURE0;
    ctx_shadow_count = n + 1;
  } else {
    debugPrintf("EGL: ctx shadow table full!\n");
    s = &ctx_shadows[0];
  }
  pthread_mutex_unlock(&gl_owner_mutex);
  return s;
}

static CtxShadow *current_shadow(void) {
  return ctx_shadow(current_ctx);
}

// replay a logical context's bindings onto the real context (raw GL only --
// must not recurse into the hooks). Called with the real binding held.
static void apply_ctx_shadow(const CtxShadow *s) {
  if (real_glBindVertexArrayOES)
    real_glBindVertexArrayOES(s->vao);
  glUseProgram(s->program);
  glBindBuffer(GL_ARRAY_BUFFER, s->array_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->element_buffer);
  const int units = shadow_unit_watermark;
  for (int i = 0; i < units && i < SHADOW_TEX_UNITS; i++) {
    glActiveTexture(GL_TEXTURE0 + (GLenum)i);
    glBindTexture(GL_TEXTURE_2D, s->tex2d[i]);
    glBindTexture(GL_TEXTURE_CUBE_MAP, s->texcube[i]);
  }
  glActiveTexture(s->active_texture);
  if (s->vp_set)
    glViewport(s->vp[0], s->vp[1], s->vp[2], s->vp[3]);
  if (s->clear_color_set)
    glClearColor(s->clear_color[0], s->clear_color[1],
                 s->clear_color[2], s->clear_color[3]);
}

static void apply_logical_state_if_changed(void) {
  if (g_applied_logical == current_ctx)
    return;
  apply_ctx_shadow(current_shadow());
  g_applied_logical = current_ctx;
}

static int shadow_active_unit(const CtxShadow *s) {
  int unit = (int)(s->active_texture - GL_TEXTURE0);
  if (unit < 0 || unit >= SHADOW_TEX_UNITS)
    return 0;
  return unit;
}

static void log_attribs(const char *tag, const EGLint *attribs) {
  if (!attribs) {
    debugPrintf("EGL: %s attribs = (null)\n", tag);
    return;
  }
  char buf[512];
  int pos = 0;
  for (int i = 0; attribs[i] != EGL_NONE && pos < (int)sizeof(buf) - 32; i += 2)
    pos += snprintf(buf + pos, sizeof(buf) - pos, "0x%04x=%d ", attribs[i], attribs[i + 1]);
  buf[pos] = '\0';
  debugPrintf("EGL: %s attribs = [ %s]\n", tag, buf);
}

static EGLint attrib_value(const EGLint *attribs, EGLint key, EGLint fallback) {
  if (!attribs)
    return fallback;
  for (int i = 0; attribs[i] != EGL_NONE; i += 2)
    if (attribs[i] == key)
      return attribs[i + 1];
  return fallback;
}

static FakePbuffer *fake_pbuffer_from_surface(EGLSurface surface) {
  for (int i = 0; i < fake_pbuffer_count; i++)
    if ((EGLSurface)fake_pbuffers[i] == surface)
      return fake_pbuffers[i];
  return NULL;
}

static FakeContext *fake_context_from_context(EGLContext ctx) {
  for (int i = 0; i < fake_context_count; i++)
    if ((EGLContext)fake_contexts[i] == ctx)
      return fake_contexts[i];
  return NULL;
}

static EGLContext real_context_from_context(EGLContext ctx) {
  FakeContext *fake = fake_context_from_context(ctx);
  return fake ? fake->real : ctx;
}

// A GL call on a thread with no virtual context current is a no-op, exactly
// like Android (never "helpfully" bind one: that races the render thread).

static int framebuffer_target(GLenum target) {
  return target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER;
}

static PbufferTarget *fake_pbuffer_target(FakePbuffer *surf, EGLContext ctx) {
  if (!surf || ctx == EGL_NO_CONTEXT)
    return NULL;

  for (PbufferTarget *t = surf->targets; t; t = t->next)
    if (t->ctx == ctx)
      return t;

  PbufferTarget *t = (PbufferTarget *)calloc(1, sizeof(*t));
  if (!t)
    return NULL;
  t->ctx = ctx;
  t->next = surf->targets;
  surf->targets = t;
  return t;
}

static PbufferTarget *ensure_fake_pbuffer_target(FakePbuffer *surf, EGLContext ctx) {
  PbufferTarget *t = fake_pbuffer_target(surf, ctx);
  if (!t)
    return NULL;
  if (t->ready)
    return t;

  GLint prev_active = GL_TEXTURE0;
  GLint prev_tex = 0;
  GLint prev_fbo = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &t->color);
  glBindTexture(GL_TEXTURE_2D, t->color);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surf->width, surf->height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glGenFramebuffers(1, &t->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, t->color, 0);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  GLenum err = glGetError();
  t->ready = (status == GL_FRAMEBUFFER_COMPLETE);
  debugPrintf("EGL: fake pbuffer target ctx=%p fbo=%u tex=%u %dx%d status=0x%x err=0x%x\n",
              ctx, t->fbo, t->color, surf->width, surf->height, status, err);

  glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
  glActiveTexture((GLenum)prev_active);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  return t;
}

static GLuint current_fake_pbuffer_fbo(void) {
  PbufferTarget *t = ensure_fake_pbuffer_target(current_fake_draw, current_ctx);
  return (t && t->ready) ? t->fbo : 0;
}

static void bind_virtual_framebuffer_for_context(void) {
  GLuint actual = current_virtual_fbo;
  if (current_fake_draw && current_virtual_fbo == 0)
    actual = current_fake_pbuffer_fbo();
  glBindFramebuffer(GL_FRAMEBUFFER, actual);
}

// drop the really-bound context. No explicit glFlush: eglMakeCurrent flushes
// the outgoing context implicitly, and one real context = one command stream.
static void release_gl_ownership(void) {
  if (tls_bound_ctx == EGL_NO_CONTEXT)
    return;
  ++egl_owner_release_count;
  ++egl_real_makecurrent_count;
  eglMakeCurrent(last_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  pthread_mutex_lock(&gl_owner_mutex);
  if (tls_bound_rec && tls_bound_rec->owner_valid &&
      pthread_equal(tls_bound_rec->owner, pthread_self())) {
    tls_bound_rec->owner_valid = 0;
    tls_bound_rec->release_requested = 0;
    pthread_cond_broadcast(&gl_owner_cond);
  }
  pthread_mutex_unlock(&gl_owner_mutex);
  tls_bound_ctx = EGL_NO_CONTEXT;
  tls_bound_draw = EGL_NO_SURFACE;
  tls_bound_fake = NULL;
  tls_bound_vfbo = 0;
  tls_bound_rec = NULL;
}

// called from the pthread/sleep shims: a thread about to block must not park
// while holding the real context or it starves everyone wanting GL.
// EXCEPTION: the render thread parks every frame; it keeps the binding
// unless someone is already waiting (its wait shims service handover in
// short slices), so uncontended frames pay no unbind/rebind.
volatile int egl_park_keep_count = 0;
volatile int egl_park_release_count = 0;

void egl_gl_ownership_park(void) {
  extern int pthr_is_render_thread(void);
  if (pthr_is_render_thread() && tls_bound_rec && !tls_bound_rec->release_requested) {
    ++egl_park_keep_count;
    return;
  }
  if (tls_bound_ctx != EGL_NO_CONTEXT)
    ++egl_park_release_count;
  release_gl_ownership();
}

// unconditional release, for points where the thread is done with GL for good
// (thread exit, end of the wrapper's startup sequence) -- the park above may
// keep the binding for the render thread, which would orphan it on exit
void egl_gl_ownership_release(void) {
  release_gl_ownership();
}

// hand-over service point for threads that keep running without calling GL
// or parking (the wrapper main loop, the NuFrameEnd clock_gettime busy-spin);
// one TLS check when idle.
void egl_gl_service_handover(void) {
  if (gl_context_depth == 0 && tls_bound_rec && tls_bound_rec->release_requested)
    release_gl_ownership();
}

// does the calling thread really hold the GL context right now? (the render
// thread's wait shims only poll while it does)
int egl_gl_thread_holds_context(void) {
  return tls_bound_ctx != EGL_NO_CONTEXT;
}

static EGLBoolean begin_gl_call(const char *why) {
  if (gl_context_depth++ > 0)
    return EGL_TRUE;

  EGLContext want = current_real_ctx;
  if (last_dpy == EGL_NO_DISPLAY || want == EGL_NO_CONTEXT || want == NULL) {
    // no virtual context on this thread: GL is a no-op, like Android
    gl_context_depth--;
    // the thread may still really hold the context from an earlier tenure
    // (virtual release keeps the binding sticky); don't starve a waiter
    if (tls_bound_rec && tls_bound_rec->release_requested)
      release_gl_ownership();
    static int n = 0;
    if (n++ < 40)
      debugPrintf("EGL: GL call %s with no context current (thread skips)\n", why);
    return EGL_FALSE;
  }

  // ONE real context, ONE stable drawable: always the window surface (mesa's
  // DrawSurface bookkeeping corrupts if the drawable churns between the
  // window and NULL). Logical pbuffer surfaces never reach mesa; their FBO 0
  // is redirected to a backing FBO below.
  EGLSurface draw = main_window_surface;
  EGLSurface read = draw;

  // fast path: this thread really has the right context+state bound
  if (tls_bound_ctx == want && tls_bound_draw == draw &&
      tls_bound_fake == current_fake_draw && tls_bound_vfbo == current_virtual_fbo) {
    // same thread may have switched LOGICAL contexts since the last call
    apply_logical_state_if_changed();
    return EGL_TRUE;
  }

  if (tls_bound_ctx != want) {
    const pthread_t self = pthread_self();
    pthread_mutex_lock(&gl_owner_mutex);
    // hand back whatever we held
    if (tls_bound_rec && tls_bound_rec->owner_valid &&
        pthread_equal(tls_bound_rec->owner, self)) {
      tls_bound_rec->owner_valid = 0;
      tls_bound_rec->release_requested = 0;
      pthread_cond_broadcast(&gl_owner_cond);
    }
    tls_bound_rec = NULL;
    // claim the context we want
    CtxOwner *rec = ctx_owner_rec(want);
    int starved = 0;
    while (rec->owner_valid && !pthread_equal(rec->owner, self)) {
      rec->release_requested = 1;
      // bounded wait purely as a watchdog: a healthy owner hands over at its
      // next GL boundary or park; if not, we want a log trail, not a freeze
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += 2;
      if (pthread_cond_timedwait(&gl_owner_cond, &gl_owner_mutex, &ts) != 0 &&
          rec->owner_valid && !pthread_equal(rec->owner, self)) {
        if (starved++ < 5)
          debugPrintf("EGL: ctx %p ownership starved for %s (owner parked?)\n", want, why);
      }
    }
    rec->owner = self;
    rec->owner_valid = 1;
    rec->release_requested = 0;
    pthread_mutex_unlock(&gl_owner_mutex);
    tls_bound_rec = rec;
    tls_bound_ctx = EGL_NO_CONTEXT; // not really bound yet; bind below
  }

  // (re)bind the desired context+surface on this thread
  if (tls_bound_ctx != want || tls_bound_draw != draw) {
    ++egl_real_makecurrent_count;
    EGLBoolean r = eglMakeCurrent(last_dpy, draw, read, want);
    if (r != EGL_TRUE) {
      EGLint err = eglGetError();
      static int n = 0;
      if (n++ < 60)
        debugPrintf("EGL: bind for %s failed (ctx=%p draw=%p err=0x%x)\n",
                    why, want, draw, err);
      pthread_mutex_lock(&gl_owner_mutex);
      if (tls_bound_rec) {
        tls_bound_rec->owner_valid = 0;
        pthread_cond_broadcast(&gl_owner_cond);
        tls_bound_rec = NULL;
      }
      pthread_mutex_unlock(&gl_owner_mutex);
      gl_context_depth--;
      return EGL_FALSE;
    }
    tls_bound_ctx = want;
    tls_bound_draw = draw;
    tls_bound_fake = NULL;
    tls_bound_vfbo = 0;
  }

  // the real context may still carry another logical context's bindings
  apply_logical_state_if_changed();

  // restore this thread's virtual framebuffer binding if it changed
  if (tls_bound_fake != current_fake_draw || tls_bound_vfbo != current_virtual_fbo) {
    bind_virtual_framebuffer_for_context();
    tls_bound_fake = current_fake_draw;
    tls_bound_vfbo = current_virtual_fbo;
  }
  return EGL_TRUE;
}

static void end_gl_call(void) {
  if (gl_context_depth <= 0)
    return;
  if (--gl_context_depth > 0)
    return;
  // hand the context over only when somebody is actually waiting; otherwise
  // stay bound so the next call from this thread is free
  if (tls_bound_rec && tls_bound_rec->release_requested)
    release_gl_ownership();
}

static const char *surface_kind(void) {
  if (current_fake_draw)
    return "pbuffer";
  if (current_draw == EGL_NO_SURFACE)
    return "none";
  return "window";
}

// log a shader/program info log only when there's something to say (a compile
// or link diagnostic); cheap and only fires on the rare interesting case
static void log_info_log_shader(GLuint shader) {
  GLint len = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
  if (len <= 1)
    return;
  char buf[512];
  glGetShaderInfoLog(shader, sizeof(buf), NULL, buf);
  buf[sizeof(buf) - 1] = '\0';
  debugPrintf("GL: shader %u info: %s\n", shader, buf);
}

static void log_info_log_program(GLuint program) {
  GLint len = 0;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
  if (len <= 1)
    return;
  char buf[512];
  glGetProgramInfoLog(program, sizeof(buf), NULL, buf);
  buf[sizeof(buf) - 1] = '\0';
  debugPrintf("GL: program %u info: %s\n", program, buf);
}

EGLDisplay eglGetDisplayHook(EGLNativeDisplayType display_id) {
  EGLDisplay dpy = eglGetDisplay(display_id);
  debugPrintf("EGL: eglGetDisplay(%p) -> %p\n", (void *)display_id, dpy);
  return dpy;
}

EGLBoolean eglInitializeHook(EGLDisplay dpy, EGLint *major, EGLint *minor) {
  EGLBoolean r = eglInitialize(dpy, major, minor);
  debugPrintf("EGL: eglInitialize -> %d (v%d.%d)\n", r,
              major ? *major : 0, minor ? *minor : 0);
  return r;
}

EGLBoolean eglBindAPIHook(EGLenum api) {
  EGLBoolean r = eglBindAPI(api);
  debugPrintf("EGL: eglBindAPI(0x%x) -> %d\n", api, r);
  return r;
}

EGLBoolean eglChooseConfigHook(EGLDisplay dpy, const EGLint *attrib_list,
                               EGLConfig *configs, EGLint config_size, EGLint *num_config) {
  log_attribs("eglChooseConfig", attrib_list);

  // copy the attrib list and strip EGL_PBUFFER_BIT from EGL_SURFACE_TYPE:
  // no Switch mesa config has it, and the loader-thread pbuffers it's meant
  // for are faked below anyway
  EGLint attribs[64];
  int n = 0;
  if (attrib_list) {
    while (attrib_list[n] != EGL_NONE && n < 62) {
      attribs[n] = attrib_list[n];
      attribs[n + 1] = attrib_list[n + 1];
      if (attribs[n] == EGL_SURFACE_TYPE && (attribs[n + 1] & EGL_PBUFFER_BIT)) {
        debugPrintf("EGL: stripping PBUFFER bit from surface type 0x%x\n", attribs[n + 1]);
        attribs[n + 1] &= ~EGL_PBUFFER_BIT;
      }
      n += 2;
    }
  }
  attribs[n] = EGL_NONE;

  EGLBoolean r = eglChooseConfig(dpy, attribs, configs, config_size, num_config);
  debugPrintf("EGL: eglChooseConfig -> %d, %d configs\n", r, num_config ? *num_config : -1);
  if (r == EGL_TRUE && num_config && *num_config > 0)
    return r;

  // still nothing: retry with progressively relaxed lists so the game always
  // gets a usable ES2 config (it ignores failure here)
  static const EGLint relaxed[] = {
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_NONE
  };
  static const EGLint minimal[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };
  const EGLint *fallbacks[] = { relaxed, minimal };
  for (unsigned i = 0; i < 2; i++) {
    r = eglChooseConfig(dpy, fallbacks[i], configs, config_size, num_config);
    debugPrintf("EGL: eglChooseConfig fallback %u -> %d, %d configs\n",
                i, r, num_config ? *num_config : -1);
    if (r == EGL_TRUE && num_config && *num_config > 0)
      return r;
  }
  return r;
}

EGLContext eglCreateContextHook(EGLDisplay dpy, EGLConfig config,
                                EGLContext share_context, const EGLint *attrib_list) {
  log_attribs("eglCreateContext", attrib_list);
  // ONE real mesa context, ever; later creates return aliases (see the
  // header comment for why real shared contexts go black on mesa)
  if (main_real_context == EGL_NO_CONTEXT) {
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, attrib_list);
    main_real_context = ctx;
    debugPrintf("EGL: eglCreateContext(cfg=%p, share=%p) -> %p (real, err 0x%x)\n",
                config, share_context, ctx, eglGetError());
    return ctx;
  }
  if (fake_context_count >= MAX_FAKE_CONTEXTS) {
    debugPrintf("EGL: eglCreateContext: fake context table full!\n");
    return EGL_NO_CONTEXT;
  }
  FakeContext *fake = (FakeContext *)calloc(1, sizeof(*fake));
  if (!fake)
    return EGL_NO_CONTEXT;
  fake->magic = FAKE_CONTEXT_MAGIC;
  fake->real = main_real_context;
  fake->share = share_context;
  fake_contexts[fake_context_count++] = fake;
  debugPrintf("EGL: eglCreateContext(cfg=%p, share=%p) -> %p (alias of %p)\n",
              config, share_context, (void *)fake, main_real_context);
  return (EGLContext)fake;
}

EGLSurface eglCreateWindowSurfaceHook(EGLDisplay dpy, EGLConfig config,
                                      EGLNativeWindowType win, const EGLint *attrib_list) {
  EGLSurface s = eglCreateWindowSurface(dpy, config, win, attrib_list);
  if (s != EGL_NO_SURFACE) {
    last_dpy = dpy;
    main_window_surface = s;
  }
  debugPrintf("EGL: eglCreateWindowSurface(cfg=%p, win=%p) -> %p (err 0x%x)\n",
              config, (void *)win, s, eglGetError());
  return s;
}

EGLSurface eglCreatePbufferSurfaceHook(EGLDisplay dpy, EGLConfig config,
                                       const EGLint *attrib_list) {
  log_attribs("eglCreatePbufferSurface", attrib_list);
  // try the real thing first in case the platform does support it
  EGLSurface s = eglCreatePbufferSurface(dpy, config, attrib_list);
  if (s != EGL_NO_SURFACE) {
    debugPrintf("EGL: eglCreatePbufferSurface -> %p (real)\n", s);
    return s;
  }
  EGLint err = eglGetError();
  if (fake_pbuffer_count >= MAX_FAKE_PBUFFERS) {
    debugPrintf("EGL: eglCreatePbufferSurface failed (err 0x%x), no fake slots left\n", err);
    return EGL_NO_SURFACE;
  }

  FakePbuffer *fake = (FakePbuffer *)calloc(1, sizeof(*fake));
  if (!fake) {
    debugPrintf("EGL: eglCreatePbufferSurface failed (err 0x%x), fake alloc failed\n", err);
    return EGL_NO_SURFACE;
  }
  fake->magic = FAKE_PBUFFER_MAGIC;
  fake->width = attrib_value(attrib_list, EGL_WIDTH, 1);
  fake->height = attrib_value(attrib_list, EGL_HEIGHT, 1);
  if (fake->width <= 0) fake->width = 1;
  if (fake->height <= 0) fake->height = 1;
  fake_pbuffers[fake_pbuffer_count++] = fake;
  debugPrintf("EGL: eglCreatePbufferSurface failed (err 0x%x), faking %dx%d pbuffer %p\n",
              err, fake->width, fake->height, fake);
  return (EGLSurface)fake;
}

EGLBoolean eglDestroySurfaceHook(EGLDisplay dpy, EGLSurface surface) {
  FakePbuffer *fake = fake_pbuffer_from_surface(surface);
  if (fake) {
    debugPrintf("EGL: eglDestroySurface(fake pbuffer %p)\n", fake);
    for (int i = 0; i < fake_pbuffer_count; i++) {
      if (fake_pbuffers[i] == fake) {
        fake_pbuffers[i] = fake_pbuffers[--fake_pbuffer_count];
        break;
      }
    }
    free(fake);
    return EGL_TRUE;
  }
  EGLBoolean r = eglDestroySurface(dpy, surface);
  debugPrintf("EGL: eglDestroySurface(%p) -> %d\n", surface, r);
  return r;
}

// counts every eglMakeCurrent; the main-loop heartbeat reports the delta so we
// can tell a spinning loader thread (count exploding) from a stalled one
volatile int egl_makecurrent_count = 0;

EGLBoolean eglMakeCurrentHook(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
  const int n = ++egl_makecurrent_count;
  const int log = (n <= 40); // the boot-time context dance only
  FakePbuffer *fake_draw = fake_pbuffer_from_surface(draw);
  FakePbuffer *fake_read = fake_pbuffer_from_surface(read);
  EGLContext real_ctx = real_context_from_context(ctx);

  if (ctx == EGL_NO_CONTEXT || ctx == NULL) {
    // the game's own release point: keep the REAL binding sticky (the same
    // thread almost always rebinds moments later); hand the context over
    // only if another thread is actually waiting
    current_ctx = EGL_NO_CONTEXT;
    current_real_ctx = EGL_NO_CONTEXT;
    current_draw = EGL_NO_SURFACE;
    current_read = EGL_NO_SURFACE;
    current_fake_draw = NULL;
    current_virtual_fbo = 0;
    if (tls_bound_ctx != EGL_NO_CONTEXT) {
      if (tls_bound_rec && tls_bound_rec->release_requested)
        release_gl_ownership();
    }
    if (log)
      debugPrintf("EGL: eglMakeCurrent #%d release (virtual)\n", n);
    return EGL_TRUE;
  }

  current_ctx = ctx;
  current_real_ctx = real_ctx;
  current_draw = draw;
  current_read = read;
  current_fake_draw = fake_draw ? fake_draw : fake_read;
  current_virtual_fbo = 0;
  if (!current_fake_draw && draw != EGL_NO_SURFACE) {
    last_dpy = dpy;
    main_window_surface = draw;
    if (!fake_context_from_context(ctx))
      main_real_context = real_ctx;
  }
  if (log)
    debugPrintf("EGL: eglMakeCurrent #%d (draw=%p, read=%p, ctx=%p real=%p fake=%p) virtual -> 1\n",
                n, draw, read, ctx, real_ctx, (void *)current_fake_draw);
  return EGL_TRUE;
}

EGLBoolean eglQuerySurfaceHook(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value) {
  FakePbuffer *fake = fake_pbuffer_from_surface(surface);
  if (fake) {
    EGLint v = 0;
    switch (attribute) {
      case EGL_WIDTH: v = fake->width; break;
      case EGL_HEIGHT: v = fake->height; break;
      default: v = 0; break;
    }
    if (value) *value = v;
    debugPrintf("EGL: eglQuerySurface(fake pbuffer %p, 0x%x) -> %d\n",
                fake, attribute, v);
    return EGL_TRUE;
  }
  EGLBoolean r = eglQuerySurface(dpy, surface, attribute, value);
  // mesa reports the window surface as 0x0 until the first buffer is
  // acquired; the game trusts this and builds a 0x0 render target (the
  // black-screen bug). Answer with the real framebuffer size instead.
  if (r == EGL_TRUE && value && *value == 0) {
    if (attribute == EGL_WIDTH) {
      debugPrintf("EGL: eglQuerySurface(%p, WIDTH) = 0, overriding -> %d\n", surface, screen_width);
      *value = screen_width;
      return r;
    }
    if (attribute == EGL_HEIGHT) {
      debugPrintf("EGL: eglQuerySurface(%p, HEIGHT) = 0, overriding -> %d\n", surface, screen_height);
      *value = screen_height;
      return r;
    }
  }
  debugPrintf("EGL: eglQuerySurface(%p, 0x%x) -> %d, value=%d\n",
              surface, attribute, r, value ? *value : -1);
  return r;
}

#include "../config.h"
#include "../fps.h"

// read by the main loop's heartbeat so debug.log shows whether the render
// thread is still presenting after the log otherwise goes quiet
volatile int egl_swap_count = 0;

EGLBoolean eglSwapBuffersHook(EGLDisplay dpy, EGLSurface surface) {
  const int n = ++egl_swap_count;
  if (!begin_gl_call("eglSwapBuffers"))
    return EGL_FALSE;

#ifdef DEBUG_LOG
  if (n == 1 || n == 2 || n == 60 || (n % 1800) == 0) {
    GLenum gerr = glGetError();
    debugPrintf("EGL: eglSwapBuffers #%d (glGetError=0x%x)\n", n, gerr);
  }

  // probe the frame about to be presented: reads the center pixel of the
  // backbuffer. Tells black-content (probe black, present fine) apart from
  // broken present (probe shows content, screen stays black) in one log line.
  // DEBUG only: glReadPixels stalls the GPU pipeline, it must not ship.
  if (n == 120 || (n % 600) == 0) {
    GLint prev_fbo = 0, prog = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    unsigned char px[4] = {0, 0, 0, 0};
    glReadPixels(screen_width / 2, screen_height / 2, 1, 1,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    debugPrintf("EGL: probe swap #%d center=%u,%u,%u,%u prog=%d err=0x%x\n",
                n, px[0], px[1], px[2], px[3], prog, glGetError());
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  }
#endif

  if (config.show_fps)
    fps_render();

  EGLBoolean r = eglSwapBuffers(dpy, surface);
  if (r != EGL_TRUE) {
    static int nfail = 0;
    if (nfail++ < 20)
      debugPrintf("EGL: eglSwapBuffers #%d FAILED (err=0x%x surface=%p)\n",
                  n, eglGetError(), surface);
  }
  // end_gl_call hands the context to a waiter if one asked (gl_release_requested);
  // when uncontended the presenting thread keeps it bound, so we don't pay an
  // unbind+rebind every frame. Loader threads that want in set the flag and get
  // it at this boundary.
  end_gl_call();
  return r;
}

// ---------------------------------------------------------------------------
// framebuffer / texture instrumentation: find why the rendered content is
// black (FBO completeness, ETC1 compressed-texture upload errors)
// ---------------------------------------------------------------------------

void glBindFramebufferHook(GLenum target, GLuint framebuffer) {
  if (!begin_gl_call("glBindFramebuffer"))
    return;
  GLuint actual = framebuffer;
  if (current_fake_draw && framebuffer == 0 && framebuffer_target(target))
    actual = current_fake_pbuffer_fbo();
  glBindFramebuffer(target, actual);
  if (framebuffer_target(target)) {
    current_virtual_fbo = framebuffer;
    // keep the sticky-binding cache in sync so the next begin_gl_call's fast
    // path doesn't think the framebuffer needs rebinding
    tls_bound_vfbo = framebuffer;
    tls_bound_fake = current_fake_draw;
  }
  end_gl_call();
}

void glBindBufferHook(GLenum target, GLuint buffer) {
  if (!begin_gl_call("glBindBuffer"))
    return;
  glBindBuffer(target, buffer);
  if (target == GL_ARRAY_BUFFER)
    current_shadow()->array_buffer = buffer;
  else if (target == GL_ELEMENT_ARRAY_BUFFER)
    current_shadow()->element_buffer = buffer;
  end_gl_call();
}

GLenum glCheckFramebufferStatusHook(GLenum target) {
  if (!begin_gl_call("glCheckFramebufferStatus"))
    return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
  GLenum s = glCheckFramebufferStatus(target);
  if (s != GL_FRAMEBUFFER_COMPLETE)
    debugPrintf("GL: glCheckFramebufferStatus = 0x%x (NOT complete, surf=%s ctx=%p)\n",
                s, surface_kind(), current_ctx);
  end_gl_call();
  return s;
}

static void glFramebufferTexture2DProcHook(GLenum target, GLenum attachment,
                                           GLenum textarget, GLuint texture, GLint level) {
  static int n = 0;
  GLint fbo = 0;
  if (!begin_gl_call("glFramebufferTexture2D"))
    return;
  if (n++ < 40) {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    debugPrintf("GL: glFramebufferTexture2D target=0x%x fbo=%d att=0x%x texTarget=0x%x tex=%u level=%d surf=%s ctx=%p\n",
                target, fbo, attachment, textarget, texture, level, surface_kind(), current_ctx);
  }
  glFramebufferTexture2D(target, attachment, textarget, texture, level);
  GLenum e = glGetError();
  if (e)
    debugPrintf("GL: glFramebufferTexture2D err=0x%x\n", e);
  end_gl_call();
}

static void glFramebufferRenderbufferProcHook(GLenum target, GLenum attachment,
                                              GLenum renderbuffertarget, GLuint renderbuffer) {
  static int n = 0;
  GLint fbo = 0;
  if (!begin_gl_call("glFramebufferRenderbuffer"))
    return;
  if (n++ < 40) {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    debugPrintf("GL: glFramebufferRenderbuffer target=0x%x fbo=%d att=0x%x rbTarget=0x%x rb=%u surf=%s ctx=%p\n",
                target, fbo, attachment, renderbuffertarget, renderbuffer, surface_kind(), current_ctx);
  }
  glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
  GLenum e = glGetError();
  if (e)
    debugPrintf("GL: glFramebufferRenderbuffer err=0x%x\n", e);
  end_gl_call();
}

static void glBindRenderbufferProcHook(GLenum target, GLuint renderbuffer) {
  static int n = 0;
  if (!begin_gl_call("glBindRenderbuffer"))
    return;
  if (n++ < 40)
    debugPrintf("GL: glBindRenderbuffer target=0x%x rb=%u surf=%s ctx=%p\n",
                target, renderbuffer, surface_kind(), current_ctx);
  glBindRenderbuffer(target, renderbuffer);
  end_gl_call();
}

static void glRenderbufferStorageProcHook(GLenum target, GLenum internalformat,
                                          GLsizei width, GLsizei height) {
  static int n = 0;
  if (!begin_gl_call("glRenderbufferStorage"))
    return;
  if (n++ < 40)
    debugPrintf("GL: glRenderbufferStorage target=0x%x ifmt=0x%x %dx%d surf=%s ctx=%p\n",
                target, internalformat, width, height, surface_kind(), current_ctx);
  glRenderbufferStorage(target, internalformat, width, height);
  GLenum e = glGetError();
  if (e)
    debugPrintf("GL: glRenderbufferStorage err=0x%x\n", e);
  end_gl_call();
}

static void glGenRenderbuffersProcHook(GLsizei n, GLuint *renderbuffers) {
  if (!begin_gl_call("glGenRenderbuffers")) {
    if (renderbuffers && n > 0)
      memset(renderbuffers, 0, (size_t)n * sizeof(*renderbuffers));
    return;
  }
  glGenRenderbuffers(n, renderbuffers);
  if (n > 0)
    debugPrintf("GL: glGenRenderbuffers n=%d first=%u surf=%s ctx=%p\n",
                n, renderbuffers ? renderbuffers[0] : 0, surface_kind(), current_ctx);
  end_gl_call();
}

typedef void (*PFNGLBLITFRAMEBUFFERPROC)(GLint, GLint, GLint, GLint,
                                         GLint, GLint, GLint, GLint,
                                         GLbitfield, GLenum);
static PFNGLBLITFRAMEBUFFERPROC real_glBlitFramebuffer = NULL;

static void glBlitFramebufferProcHook(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                      GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                                      GLbitfield mask, GLenum filter) {
  if (!begin_gl_call("glBlitFramebuffer"))
    return;
  debugPrintf("GL: glBlitFramebuffer src=%d,%d-%d,%d dst=%d,%d-%d,%d mask=0x%x filter=0x%x surf=%s ctx=%p\n",
              srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
              mask, filter, surface_kind(), current_ctx);
  if (real_glBlitFramebuffer)
    real_glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                           dstX0, dstY0, dstX1, dstY1, mask, filter);
  end_gl_call();
}

typedef void (*PFNGLGENVERTEXARRAYSOESPROC)(GLsizei, GLuint *);
typedef void (*PFNGLDELETEVERTEXARRAYSOESPROC)(GLsizei, const GLuint *);
static PFNGLGENVERTEXARRAYSOESPROC real_glGenVertexArraysOES = NULL;
static PFNGLDELETEVERTEXARRAYSOESPROC real_glDeleteVertexArraysOES = NULL;
// (real_glBindVertexArrayOES lives with the shadow machinery near the top)

static void glGenVertexArraysOESProcHook(GLsizei n, GLuint *arrays) {
  if (!begin_gl_call("glGenVertexArraysOES")) {
    if (arrays && n > 0)
      memset(arrays, 0, (size_t)n * sizeof(*arrays));
    return;
  }
  if (real_glGenVertexArraysOES)
    real_glGenVertexArraysOES(n, arrays);
  if (n > 0)
    debugPrintf("GL: glGenVertexArraysOES n=%d first=%u surf=%s ctx=%p\n",
                n, arrays ? arrays[0] : 0, surface_kind(), current_ctx);
  end_gl_call();
}

static void glBindVertexArrayOESProcHook(GLuint array) {
  if (!begin_gl_call("glBindVertexArrayOES"))
    return;
  if (real_glBindVertexArrayOES)
    real_glBindVertexArrayOES(array);
  current_shadow()->vao = array;
  end_gl_call();
}

static void glDeleteVertexArraysOESProcHook(GLsizei n, const GLuint *arrays) {
  if (!begin_gl_call("glDeleteVertexArraysOES"))
    return;
  if (real_glDeleteVertexArraysOES)
    real_glDeleteVertexArraysOES(n, arrays);
  end_gl_call();
}

void glGetIntegervHook(GLenum pname, GLint *data) {
  if (!begin_gl_call("glGetIntegerv")) {
    if (data)
      *data = 0;
    return;
  }
  glGetIntegerv(pname, data);
  if (!data || !current_fake_draw) {
    end_gl_call();
    return;
  }

  if (pname == GL_FRAMEBUFFER_BINDING ||
      pname == GL_DRAW_FRAMEBUFFER_BINDING ||
      pname == GL_READ_FRAMEBUFFER_BINDING) {
    GLuint fbo = current_fake_pbuffer_fbo();
    if (fbo && *data == (GLint)fbo)
      *data = 0;
  }
  end_gl_call();
}

void glCopyTexImage2DHook(GLenum target, GLint level, GLenum internalformat,
                          GLint x, GLint y, GLsizei width, GLsizei height,
                          GLint border) {
  // the Vita port drops this entirely when a pbuffer context is current (its
  // read buffer there is meaningless); ours is a 1x1 stand-in, so a real
  // full-screen copy would fail with GL_INVALID_VALUE or read garbage
  if (current_fake_draw) {
    static int n = 0;
    if (n++ < 4)
      debugPrintf("GL: glCopyTexImage2D skipped on pbuffer ctx target=0x%x level=%d %dx%d at %d,%d\n",
                  target, level, width, height, x, y);
    return;
  }
  if (!begin_gl_call("glCopyTexImage2D"))
    return;
  glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
  GLenum e = glGetError();
  if (e)
    debugPrintf("GL: glCopyTexImage2D target=0x%x ifmt=0x%x %dx%d at %d,%d -> err=0x%x\n",
                target, internalformat, width, height, x, y, e);
  end_gl_call();
}

GLuint glCreateShaderHook(GLenum type) {
  if (!begin_gl_call("glCreateShader"))
    return 0;
  GLuint shader = glCreateShader(type);
  end_gl_call();
  return shader;
}

void glShaderSourceHook(GLuint shader, GLsizei count, const GLchar *const *string,
                        const GLint *length) {
  if (!begin_gl_call("glShaderSource"))
    return;
  glShaderSource(shader, count, string, length);
  end_gl_call();
}

void glCompileShaderHook(GLuint shader) {
  if (!begin_gl_call("glCompileShader"))
    return;
  glCompileShader(shader);
  GLint ok = 1;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) { // only the failures are interesting
    debugPrintf("GL: glCompileShader shader=%u FAILED\n", shader);
    log_info_log_shader(shader);
  }
  end_gl_call();
}

GLuint glCreateProgramHook(void) {
  if (!begin_gl_call("glCreateProgram"))
    return 0;
  GLuint program = glCreateProgram();
  end_gl_call();
  return program;
}

void glAttachShaderHook(GLuint program, GLuint shader) {
  if (!begin_gl_call("glAttachShader"))
    return;
  glAttachShader(program, shader);
  end_gl_call();
}

void glBindAttribLocationHook(GLuint program, GLuint index, const GLchar *name) {
  if (!begin_gl_call("glBindAttribLocation"))
    return;
  glBindAttribLocation(program, index, name);
  end_gl_call();
}

void glLinkProgramHook(GLuint program) {
  if (!begin_gl_call("glLinkProgram"))
    return;
  glLinkProgram(program);
  GLint ok = 1;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) { // only the failures are interesting
    debugPrintf("GL: glLinkProgram program=%u FAILED\n", program);
    log_info_log_program(program);
  }
  GLenum e = glGetError();
  if (e)
    debugPrintf("GL: glLinkProgram program=%u err=0x%x\n", program, e);
  end_gl_call();
}

void glUseProgramHook(GLuint program) {
  if (!begin_gl_call("glUseProgram"))
    return;
  glUseProgram(program);
  current_shadow()->program = program;
  end_gl_call();
}

void glVertexAttribPointerHook(GLuint index, GLint size, GLenum type,
                               GLboolean normalized, GLsizei stride,
                               const void *pointer) {
  if (!begin_gl_call("glVertexAttribPointer"))
    return;
  glVertexAttribPointer(index, size, type, normalized, stride, pointer);
  end_gl_call();
}

void glEnableVertexAttribArrayHook(GLuint index) {
  if (!begin_gl_call("glEnableVertexAttribArray"))
    return;
  glEnableVertexAttribArray(index);
  end_gl_call();
}

void glDisableVertexAttribArrayHook(GLuint index) {
  if (!begin_gl_call("glDisableVertexAttribArray"))
    return;
  glDisableVertexAttribArray(index);
  end_gl_call();
}

static int seen_format(GLenum fmt) {
  static GLenum seen[32];
  static int n = 0;
  for (int i = 0; i < n; i++)
    if (seen[i] == fmt) return 1;
  if (n < 32) seen[n++] = fmt;
  return 0;
}

void glCompressedTexImage2DHook(GLenum target, GLint level, GLenum internalformat,
                                GLsizei width, GLsizei height, GLint border,
                                GLsizei imageSize, const void *data) {
  if (!begin_gl_call("glCompressedTexImage2D"))
    return;
  glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
  if (!seen_format(internalformat)) {
    GLenum e = glGetError();
    debugPrintf("GL: glCompressedTexImage2D fmt=0x%x %dx%d level=%d size=%d -> err=0x%x\n",
                internalformat, width, height, level, imageSize, e);
  }
  end_gl_call();
}

void glTexImage2DHook(GLenum target, GLint level, GLint internalformat, GLsizei width,
                      GLsizei height, GLint border, GLenum format, GLenum type,
                      const void *pixels) {
  if (!begin_gl_call("glTexImage2D"))
    return;
  glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
  GLenum key = (GLenum)internalformat | 0x80000000u; // distinguish from compressed
  if (!seen_format(key)) {
    GLenum e = glGetError();
    debugPrintf("GL: glTexImage2D ifmt=0x%x fmt=0x%x type=0x%x %dx%d -> err=0x%x\n",
                internalformat, format, type, width, height, e);
  }
  end_gl_call();
}

void glActiveTextureHook(GLenum texture) {
  if (!begin_gl_call("glActiveTexture")) return;
  glActiveTexture(texture);
  CtxShadow *s = current_shadow();
  s->active_texture = texture;
  const int unit = shadow_active_unit(s);
  if (unit + 1 > shadow_unit_watermark)
    shadow_unit_watermark = unit + 1;
  end_gl_call();
}

void glBindTextureHook(GLenum target, GLuint texture) {
  if (!begin_gl_call("glBindTexture")) return;
  glBindTexture(target, texture);
  CtxShadow *s = current_shadow();
  if (target == GL_TEXTURE_2D)
    s->tex2d[shadow_active_unit(s)] = texture;
  else if (target == GL_TEXTURE_CUBE_MAP)
    s->texcube[shadow_active_unit(s)] = texture;
  end_gl_call();
}

void glBlendEquationSeparateHook(GLenum modeRGB, GLenum modeAlpha) {
  if (!begin_gl_call("glBlendEquationSeparate")) return;
  glBlendEquationSeparate(modeRGB, modeAlpha);
  end_gl_call();
}

void glBlendFuncSeparateHook(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
  if (!begin_gl_call("glBlendFuncSeparate")) return;
  glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
  end_gl_call();
}

void glBufferDataHook(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
  if (!begin_gl_call("glBufferData")) return;
  glBufferData(target, size, data, usage);
  end_gl_call();
}

void glBufferSubDataHook(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
  if (!begin_gl_call("glBufferSubData")) return;
  glBufferSubData(target, offset, size, data);
  end_gl_call();
}

void glCullFaceHook(GLenum mode) {
  if (!begin_gl_call("glCullFace")) return;
  glCullFace(mode);
  end_gl_call();
}

void glDeleteBuffersHook(GLsizei n, const GLuint *buffers) {
  if (!begin_gl_call("glDeleteBuffers")) return;
  glDeleteBuffers(n, buffers);
  end_gl_call();
}

void glDeleteFramebuffersHook(GLsizei n, const GLuint *framebuffers) {
  if (!begin_gl_call("glDeleteFramebuffers")) return;
  glDeleteFramebuffers(n, framebuffers);
  end_gl_call();
}

void glDeleteProgramHook(GLuint program) {
  if (!begin_gl_call("glDeleteProgram")) return;
  glDeleteProgram(program);
  end_gl_call();
}

void glDeleteRenderbuffersHook(GLsizei n, const GLuint *renderbuffers) {
  if (!begin_gl_call("glDeleteRenderbuffers")) return;
  glDeleteRenderbuffers(n, renderbuffers);
  end_gl_call();
}

void glDeleteShaderHook(GLuint shader) {
  if (!begin_gl_call("glDeleteShader")) return;
  glDeleteShader(shader);
  end_gl_call();
}

void glDeleteTexturesHook(GLsizei n, const GLuint *textures) {
  if (!begin_gl_call("glDeleteTextures")) return;
  glDeleteTextures(n, textures);
  end_gl_call();
}

void glDepthFuncHook(GLenum func) {
  if (!begin_gl_call("glDepthFunc")) return;
  glDepthFunc(func);
  end_gl_call();
}

void glDepthMaskHook(GLboolean flag) {
  if (!begin_gl_call("glDepthMask")) return;
  glDepthMask(flag);
  end_gl_call();
}

void glDisableHook(GLenum cap) {
  if (!begin_gl_call("glDisable")) return;
  glDisable(cap);
  end_gl_call();
}

void glEnableHook(GLenum cap) {
  if (!begin_gl_call("glEnable")) return;
  glEnable(cap);
  end_gl_call();
}

void glFinishHook(void) {
  // a worker finishing/flushing the SHARED real context stalls the renderer's
  // pipeline for nothing -- its uploads complete on the one context's command
  // stream without any explicit synchronization
  if (current_fake_draw) return;
  if (!begin_gl_call("glFinish")) return;
  glFinish();
  end_gl_call();
}

void glFlushHook(void) {
  if (current_fake_draw) return;
  if (!begin_gl_call("glFlush")) return;
  glFlush();
  end_gl_call();
}

void glFrontFaceHook(GLenum mode) {
  if (!begin_gl_call("glFrontFace")) return;
  glFrontFace(mode);
  end_gl_call();
}

void glGenBuffersHook(GLsizei n, GLuint *buffers) {
  if (!begin_gl_call("glGenBuffers")) {
    if (buffers && n > 0) memset(buffers, 0, (size_t)n * sizeof(*buffers));
    return;
  }
  glGenBuffers(n, buffers);
  end_gl_call();
}

void glGenFramebuffersHook(GLsizei n, GLuint *framebuffers) {
  if (!begin_gl_call("glGenFramebuffers")) {
    if (framebuffers && n > 0) memset(framebuffers, 0, (size_t)n * sizeof(*framebuffers));
    return;
  }
  glGenFramebuffers(n, framebuffers);
  end_gl_call();
}

void glGenTexturesHook(GLsizei n, GLuint *textures) {
  if (!begin_gl_call("glGenTextures")) {
    if (textures && n > 0) memset(textures, 0, (size_t)n * sizeof(*textures));
    return;
  }
  glGenTextures(n, textures);
  end_gl_call();
}

void glGenerateMipmapHook(GLenum target) {
  if (!begin_gl_call("glGenerateMipmap")) return;
  glGenerateMipmap(target);
  end_gl_call();
}

void glGetActiveAttribHook(GLuint program, GLuint index, GLsizei bufSize,
                           GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
  if (!begin_gl_call("glGetActiveAttrib")) {
    if (length) *length = 0;
    if (name && bufSize > 0) name[0] = '\0';
    return;
  }
  glGetActiveAttrib(program, index, bufSize, length, size, type, name);
  end_gl_call();
}

void glGetActiveUniformHook(GLuint program, GLuint index, GLsizei bufSize,
                            GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
  if (!begin_gl_call("glGetActiveUniform")) {
    if (length) *length = 0;
    if (name && bufSize > 0) name[0] = '\0';
    return;
  }
  glGetActiveUniform(program, index, bufSize, length, size, type, name);
  end_gl_call();
}

void glGetAttachedShadersHook(GLuint program, GLsizei maxCount,
                              GLsizei *count, GLuint *shaders) {
  if (!begin_gl_call("glGetAttachedShaders")) {
    if (count) *count = 0;
    return;
  }
  glGetAttachedShaders(program, maxCount, count, shaders);
  end_gl_call();
}

GLint glGetAttribLocationHook(GLuint program, const GLchar *name) {
  if (!begin_gl_call("glGetAttribLocation")) return -1;
  GLint r = glGetAttribLocation(program, name);
  end_gl_call();
  return r;
}

GLenum glGetErrorHook(void) {
  if (!begin_gl_call("glGetError")) return GL_NO_ERROR;
  GLenum r = glGetError();
  end_gl_call();
  return r;
}

void glGetProgramInfoLogHook(GLuint program, GLsizei bufSize,
                             GLsizei *length, GLchar *infoLog) {
  if (!begin_gl_call("glGetProgramInfoLog")) {
    if (length) *length = 0;
    if (infoLog && bufSize > 0) infoLog[0] = '\0';
    return;
  }
  glGetProgramInfoLog(program, bufSize, length, infoLog);
  end_gl_call();
}

void glGetProgramivHook(GLuint program, GLenum pname, GLint *params) {
  if (!begin_gl_call("glGetProgramiv")) {
    if (params) *params = 0;
    return;
  }
  glGetProgramiv(program, pname, params);
  end_gl_call();
}

void glGetShaderSourceHook(GLuint shader, GLsizei bufSize,
                           GLsizei *length, GLchar *source) {
  if (!begin_gl_call("glGetShaderSource")) {
    if (length) *length = 0;
    if (source && bufSize > 0) source[0] = '\0';
    return;
  }
  glGetShaderSource(shader, bufSize, length, source);
  end_gl_call();
}

void glGetShaderivHook(GLuint shader, GLenum pname, GLint *params) {
  if (!begin_gl_call("glGetShaderiv")) {
    if (params) *params = 0;
    return;
  }
  glGetShaderiv(shader, pname, params);
  end_gl_call();
}

GLint glGetUniformLocationHook(GLuint program, const GLchar *name) {
  if (!begin_gl_call("glGetUniformLocation")) return -1;
  GLint r = glGetUniformLocation(program, name);
  end_gl_call();
  return r;
}

void glGetVertexAttribPointervHook(GLuint index, GLenum pname, void **pointer) {
  if (!begin_gl_call("glGetVertexAttribPointerv")) {
    if (pointer) *pointer = NULL;
    return;
  }
  glGetVertexAttribPointerv(index, pname, pointer);
  end_gl_call();
}

void glGetVertexAttribivHook(GLuint index, GLenum pname, GLint *params) {
  if (!begin_gl_call("glGetVertexAttribiv")) {
    if (params) *params = 0;
    return;
  }
  glGetVertexAttribiv(index, pname, params);
  end_gl_call();
}

void glReleaseShaderCompilerHook(void) {
  if (!begin_gl_call("glReleaseShaderCompiler")) return;
  glReleaseShaderCompiler();
  end_gl_call();
}

void glTexParameteriHook(GLenum target, GLenum pname, GLint param) {
  if (!begin_gl_call("glTexParameteri")) return;
  glTexParameteri(target, pname, param);
  end_gl_call();
}

void glUniform1fvHook(GLint location, GLsizei count, const GLfloat *value) {
  if (!begin_gl_call("glUniform1fv")) return;
  glUniform1fv(location, count, value);
  end_gl_call();
}

void glUniform1iHook(GLint location, GLint v0) {
  if (!begin_gl_call("glUniform1i")) return;
  glUniform1i(location, v0);
  end_gl_call();
}

void glUniform2fvHook(GLint location, GLsizei count, const GLfloat *value) {
  if (!begin_gl_call("glUniform2fv")) return;
  glUniform2fv(location, count, value);
  end_gl_call();
}

void glUniform3fvHook(GLint location, GLsizei count, const GLfloat *value) {
  if (!begin_gl_call("glUniform3fv")) return;
  glUniform3fv(location, count, value);
  end_gl_call();
}

void glUniform4fvHook(GLint location, GLsizei count, const GLfloat *value) {
  if (!begin_gl_call("glUniform4fv")) return;
  glUniform4fv(location, count, value);
  end_gl_call();
}

void glValidateProgramHook(GLuint program) {
  if (!begin_gl_call("glValidateProgram")) return;
  glValidateProgram(program);
  end_gl_call();
}

// ---------------------------------------------------------------------------
// draw / clear instrumentation: is the game actually issuing draws into FBO0,
// and what does it clear to? Counters are read by the main-loop heartbeat.
// ---------------------------------------------------------------------------

volatile int gl_draw_count = 0;
volatile int gl_draw_window_count = 0;
volatile int gl_draw_pbuffer_count = 0;
volatile int gl_draw_pbuffer_skip_count = 0;

static int skip_fake_pbuffer_default_draw(void) {
  if (current_fake_draw && current_virtual_fbo == 0) {
    ++gl_draw_pbuffer_skip_count;
    if (tls_bound_rec && tls_bound_rec->release_requested)
      release_gl_ownership();
    return 1;
  }
  return 0;
}

void glClearHook(GLbitfield mask) {
  if (current_fake_draw && current_virtual_fbo == 0) {
    if (tls_bound_rec && tls_bound_rec->release_requested)
      release_gl_ownership();
    return;
  }
  if (!begin_gl_call("glClear"))
    return;
  glClear(mask);
  end_gl_call();
}

void glClearColorHook(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
  if (!begin_gl_call("glClearColor"))
    return;
  glClearColor(r, g, b, a);
  CtxShadow *s = current_shadow();
  s->clear_color[0] = r; s->clear_color[1] = g;
  s->clear_color[2] = b; s->clear_color[3] = a;
  s->clear_color_set = 1;
  end_gl_call();
}

void glDrawElementsHook(GLenum mode, GLsizei count, GLenum type, const void *indices) {
  ++gl_draw_count; // read by the heartbeat
  if (skip_fake_pbuffer_default_draw())
    return;
  if (current_fake_draw)
    ++gl_draw_pbuffer_count;
  else
    ++gl_draw_window_count;
  if (!begin_gl_call("glDrawElements"))
    return;
  glDrawElements(mode, count, type, indices);
  end_gl_call();
}

void glDrawArraysHook(GLenum mode, GLint first, GLsizei count) {
  ++gl_draw_count;
  if (skip_fake_pbuffer_default_draw())
    return;
  if (current_fake_draw)
    ++gl_draw_pbuffer_count;
  else
    ++gl_draw_window_count;
  if (!begin_gl_call("glDrawArrays"))
    return;
  glDrawArrays(mode, first, count);
  end_gl_call();
}

// log every GL entry point the game resolves dynamically (reveals whether it
// uses glBlitFramebuffer, VAOs, multisample, etc. that bypass our import hooks)
void *eglGetProcAddressHook(const char *name) {
  void *p = (void *)eglGetProcAddress(name);
  if (name) {
    if (!strcmp(name, "glActiveTexture")) p = (void *)glActiveTextureHook;
    else if (!strcmp(name, "glBindFramebuffer")) p = (void *)glBindFramebufferHook;
    else if (!strcmp(name, "glBindBuffer")) p = (void *)glBindBufferHook;
    else if (!strcmp(name, "glBindTexture")) p = (void *)glBindTextureHook;
    else if (!strcmp(name, "glBlendEquationSeparate")) p = (void *)glBlendEquationSeparateHook;
    else if (!strcmp(name, "glBlendFuncSeparate")) p = (void *)glBlendFuncSeparateHook;
    else if (!strcmp(name, "glBufferData")) p = (void *)glBufferDataHook;
    else if (!strcmp(name, "glBufferSubData")) p = (void *)glBufferSubDataHook;
    else if (!strcmp(name, "glCheckFramebufferStatus")) p = (void *)glCheckFramebufferStatusHook;
    else if (!strcmp(name, "glCullFace")) p = (void *)glCullFaceHook;
    else if (!strcmp(name, "glDeleteBuffers")) p = (void *)glDeleteBuffersHook;
    else if (!strcmp(name, "glDeleteFramebuffers")) p = (void *)glDeleteFramebuffersHook;
    else if (!strcmp(name, "glDeleteProgram")) p = (void *)glDeleteProgramHook;
    else if (!strcmp(name, "glDeleteRenderbuffers")) p = (void *)glDeleteRenderbuffersHook;
    else if (!strcmp(name, "glDeleteShader")) p = (void *)glDeleteShaderHook;
    else if (!strcmp(name, "glDeleteTextures")) p = (void *)glDeleteTexturesHook;
    else if (!strcmp(name, "glDepthFunc")) p = (void *)glDepthFuncHook;
    else if (!strcmp(name, "glDepthMask")) p = (void *)glDepthMaskHook;
    else if (!strcmp(name, "glDisable")) p = (void *)glDisableHook;
    else if (!strcmp(name, "glEnable")) p = (void *)glEnableHook;
    else if (!strcmp(name, "glFramebufferTexture2D")) p = (void *)glFramebufferTexture2DProcHook;
    else if (!strcmp(name, "glFramebufferRenderbuffer")) p = (void *)glFramebufferRenderbufferProcHook;
    else if (!strcmp(name, "glBindRenderbuffer")) p = (void *)glBindRenderbufferProcHook;
    else if (!strcmp(name, "glRenderbufferStorage")) p = (void *)glRenderbufferStorageProcHook;
    else if (!strcmp(name, "glGenRenderbuffers")) p = (void *)glGenRenderbuffersProcHook;
    else if (!strcmp(name, "glFinish")) p = (void *)glFinishHook;
    else if (!strcmp(name, "glFlush")) p = (void *)glFlushHook;
    else if (!strcmp(name, "glFrontFace")) p = (void *)glFrontFaceHook;
    else if (!strcmp(name, "glGenBuffers")) p = (void *)glGenBuffersHook;
    else if (!strcmp(name, "glGenFramebuffers")) p = (void *)glGenFramebuffersHook;
    else if (!strcmp(name, "glGenTextures")) p = (void *)glGenTexturesHook;
    else if (!strcmp(name, "glGenerateMipmap")) p = (void *)glGenerateMipmapHook;
    else if (!strcmp(name, "glGetActiveAttrib")) p = (void *)glGetActiveAttribHook;
    else if (!strcmp(name, "glGetActiveUniform")) p = (void *)glGetActiveUniformHook;
    else if (!strcmp(name, "glGetAttachedShaders")) p = (void *)glGetAttachedShadersHook;
    else if (!strcmp(name, "glGetAttribLocation")) p = (void *)glGetAttribLocationHook;
    else if (!strcmp(name, "glGetError")) p = (void *)glGetErrorHook;
    else if (!strcmp(name, "glGetProgramInfoLog")) p = (void *)glGetProgramInfoLogHook;
    else if (!strcmp(name, "glGetProgramiv")) p = (void *)glGetProgramivHook;
    else if (!strcmp(name, "glGetShaderSource")) p = (void *)glGetShaderSourceHook;
    else if (!strcmp(name, "glGetShaderiv")) p = (void *)glGetShaderivHook;
    else if (!strcmp(name, "glGetUniformLocation")) p = (void *)glGetUniformLocationHook;
    else if (!strcmp(name, "glGetVertexAttribPointerv")) p = (void *)glGetVertexAttribPointervHook;
    else if (!strcmp(name, "glGetVertexAttribiv")) p = (void *)glGetVertexAttribivHook;
    else if (!strcmp(name, "glBlitFramebuffer")) { real_glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)p; p = (void *)glBlitFramebufferProcHook; }
    else if (!strcmp(name, "glBlitFramebufferANGLE")) { real_glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)p; p = (void *)glBlitFramebufferProcHook; }
    else if (!strcmp(name, "glGenVertexArraysOES")) { real_glGenVertexArraysOES = (PFNGLGENVERTEXARRAYSOESPROC)p; p = (void *)glGenVertexArraysOESProcHook; }
    else if (!strcmp(name, "glBindVertexArrayOES")) { real_glBindVertexArrayOES = (PFNGLBINDVERTEXARRAYOESPROC)p; p = (void *)glBindVertexArrayOESProcHook; }
    else if (!strcmp(name, "glDeleteVertexArraysOES")) { real_glDeleteVertexArraysOES = (PFNGLDELETEVERTEXARRAYSOESPROC)p; p = (void *)glDeleteVertexArraysOESProcHook; }
    else if (!strcmp(name, "glCopyTexImage2D")) p = (void *)glCopyTexImage2DHook;
    else if (!strcmp(name, "glCreateShader")) p = (void *)glCreateShaderHook;
    else if (!strcmp(name, "glShaderSource")) p = (void *)glShaderSourceHook;
    else if (!strcmp(name, "glCompileShader")) p = (void *)glCompileShaderHook;
    else if (!strcmp(name, "glCreateProgram")) p = (void *)glCreateProgramHook;
    else if (!strcmp(name, "glAttachShader")) p = (void *)glAttachShaderHook;
    else if (!strcmp(name, "glBindAttribLocation")) p = (void *)glBindAttribLocationHook;
    else if (!strcmp(name, "glLinkProgram")) p = (void *)glLinkProgramHook;
    else if (!strcmp(name, "glReleaseShaderCompiler")) p = (void *)glReleaseShaderCompilerHook;
    else if (!strcmp(name, "glTexParameteri")) p = (void *)glTexParameteriHook;
    else if (!strcmp(name, "glUniform1fv")) p = (void *)glUniform1fvHook;
    else if (!strcmp(name, "glUniform1i")) p = (void *)glUniform1iHook;
    else if (!strcmp(name, "glUniform2fv")) p = (void *)glUniform2fvHook;
    else if (!strcmp(name, "glUniform3fv")) p = (void *)glUniform3fvHook;
    else if (!strcmp(name, "glUniform4fv")) p = (void *)glUniform4fvHook;
    else if (!strcmp(name, "glUseProgram")) p = (void *)glUseProgramHook;
    else if (!strcmp(name, "glValidateProgram")) p = (void *)glValidateProgramHook;
    else if (!strcmp(name, "glVertexAttribPointer")) p = (void *)glVertexAttribPointerHook;
    else if (!strcmp(name, "glEnableVertexAttribArray")) p = (void *)glEnableVertexAttribArrayHook;
    else if (!strcmp(name, "glDisableVertexAttribArray")) p = (void *)glDisableVertexAttribArrayHook;
    else if (!strcmp(name, "glCompressedTexImage2D")) p = (void *)glCompressedTexImage2DHook;
    else if (!strcmp(name, "glTexImage2D")) p = (void *)glTexImage2DHook;
    else if (!strcmp(name, "glClear")) p = (void *)glClearHook;
    else if (!strcmp(name, "glClearColor")) p = (void *)glClearColorHook;
    else if (!strcmp(name, "glDrawElements")) p = (void *)glDrawElementsHook;
    else if (!strcmp(name, "glDrawArrays")) p = (void *)glDrawArraysHook;
    else if (!strcmp(name, "glGetIntegerv")) p = (void *)glGetIntegervHook;
    else if (!strcmp(name, "glGetString")) p = (void *)glGetStringHook;
    else if (!strcmp(name, "glViewport")) p = (void *)glViewportHook;
  }
  debugPrintf("EGL: eglGetProcAddress(%s) -> %p%s\n", name ? name : "(null)", p,
              name && p ? " (wrapped if known)" : "");
  return p;
}

void glViewportHook(GLint x, GLint y, GLsizei width, GLsizei height) {
  static int nlog = 0;
  if (!begin_gl_call("glViewport"))
    return;
  glViewport(x, y, width, height);
  CtxShadow *s = current_shadow();
  s->vp[0] = x; s->vp[1] = y; s->vp[2] = width; s->vp[3] = height;
  s->vp_set = 1;
  if (nlog < 12) { nlog++; debugPrintf("GL: glViewport(%d, %d, %d, %d)\n", x, y, width, height); }
  end_gl_call();
}

// ---------------------------------------------------------------------------
// glGetString: never hand the game a NULL (DetermineDeviceSpecs dereferences
// the results without checking).
// ---------------------------------------------------------------------------

const GLubyte *glGetStringHook(GLenum name) {
  if (!begin_gl_call("glGetString"))
    goto fallback;
  const GLubyte *s = glGetString(name);
  if (s) {
    if (name == GL_VENDOR || name == GL_RENDERER || name == GL_VERSION)
      debugPrintf("GL: glGetString(0x%x) = %s\n", name, (const char *)s);
    end_gl_call();
    return s;
  }
  end_gl_call();
fallback:
  debugPrintf("GL: glGetString(0x%x) = NULL! no current context? using fallback\n", name);
  switch (name) {
    case GL_VENDOR:   return (const GLubyte *)"NVIDIA";
    case GL_RENDERER: return (const GLubyte *)"NVIDIA Tegra";
    case GL_VERSION:  return (const GLubyte *)"OpenGL ES 2.0";
    case 0x8B8C:      return (const GLubyte *)"OpenGL ES GLSL ES 1.00"; // GL_SHADING_LANGUAGE_VERSION
    case GL_EXTENSIONS:
      return (const GLubyte *)"GL_OES_compressed_ETC1_RGB8_texture "
                              "GL_OES_depth24 GL_OES_packed_depth_stencil "
                              "GL_OES_depth_texture GL_EXT_texture_compression_dxt1";
    default:          return (const GLubyte *)"";
  }
}
