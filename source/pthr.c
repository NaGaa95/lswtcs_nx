/* pthr.c -- bionic<->newlib pthread wrappers for libTTapp.so
 *
 * Ported from gm666q/lswtcs-vita (reimpl/pthr.c), adapted for devkitA64/libnx.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <switch.h>

#include "pthr.h"
#include "util.h"

// set in the trampoline when the game's renderThread_main starts; the GL
// ownership layer lets this thread keep the real context across parks, so its
// wait shims below must service handover in short slices instead of blocking
static __thread int tls_is_render_thread = 0;

int pthr_is_render_thread(void) {
  return tls_is_render_thread;
}

#define BIONIC_PTHREAD_MUTEX_INITIALIZER            0
#define BIONIC_PTHREAD_RECURSIVE_MUTEX_INITIALIZER  0x4000
#define BIONIC_PTHREAD_ERRORCHECK_MUTEX_INITIALIZER 0x8000

// ---------------------------------------------------------------------------
// fake RW TLS: AArch64 -fstack-protector loads the canary relative to
// TPIDR_EL0, which libnx leaves pointing at read-only/zero storage. Each game
// thread gets a small zeroed block installed in TPIDR_EL0 so those loads (and
// any other bionic TLS-relative reads) land on valid, writable memory. The
// block is intentionally leaked: it must stay live for the thread's lifetime.
// ---------------------------------------------------------------------------

void pthr_install_fake_tls(void) {
  uint8_t *tls = calloc(1, 0x200);
  armSetTlsRw(tls);
}

void pthr_ensure_fake_tls(void) {
  if (armGetTlsRw() == NULL)
    pthr_install_fake_tls();
}

// ---------------------------------------------------------------------------
// lazy first-use initialization of game-owned sync objects: the hot path is
// one acquire-load of the magic word; only init and destroy take init_lock
// ---------------------------------------------------------------------------

#define PTHR_MUTEX_MAGIC 0x4D58544Du // "MTXM"
#define PTHR_COND_MAGIC  0x444E434Du // "CNDM"

static Mutex init_lock;

static int attr_static_init(pthread_attr_t_bionic *attr) {
  if (attr->magic != 0x42424242) {
    attr->magic = 0x42424242;
    attr->real_ptr = malloc(sizeof(pthread_attr_t));
    return pthread_attr_init(attr->real_ptr);
  }
  return 0;
}

static int mutex_static_init(pthread_mutex_t_bionic *mutex, const pthread_mutexattr_t *attr) {
  // pairs with the release store below so real_ptr is visible once magic is
  if (__atomic_load_n(&mutex->magic, __ATOMIC_ACQUIRE) == PTHR_MUTEX_MAGIC)
    return 0;

  mutexLock(&init_lock);
  if (__atomic_load_n(&mutex->magic, __ATOMIC_RELAXED) == PTHR_MUTEX_MAGIC) {
    mutexUnlock(&init_lock); // another thread won the first-use race
    return 0;
  }

  int kind = PTHREAD_MUTEX_NORMAL;
  if (attr) {
    pthread_mutexattr_gettype((pthread_mutexattr_t *)attr, &kind);
  } else {
    // the kind word of a statically initialized bionic mutex (overlaps the
    // low half of real_ptr, which we haven't written yet)
    switch (*(int *)mutex) {
      case BIONIC_PTHREAD_RECURSIVE_MUTEX_INITIALIZER:  kind = PTHREAD_MUTEX_RECURSIVE;  break;
      case BIONIC_PTHREAD_ERRORCHECK_MUTEX_INITIALIZER: kind = PTHREAD_MUTEX_ERRORCHECK; break;
      default:                                          kind = PTHREAD_MUTEX_NORMAL;     break;
    }
  }

  pthread_mutex_t *real = malloc(sizeof(pthread_mutex_t));

  pthread_mutexattr_t ma;
  pthread_mutexattr_init(&ma);
  pthread_mutexattr_settype(&ma, kind);
  int ret = pthread_mutex_init(real, &ma);
  pthread_mutexattr_destroy(&ma);

  if (ret == 0) {
    mutex->real_ptr = real;
    __atomic_store_n(&mutex->magic, PTHR_MUTEX_MAGIC, __ATOMIC_RELEASE);
  } else {
    free(real);
    debugPrintf("pthr: mutex init for %p failed (%d)\n", (void *)mutex, ret);
  }
  mutexUnlock(&init_lock);
  return ret;
}

static int cond_static_init(pthread_cond_t_bionic *cond, const pthread_condattr_t *attr) {
  if (__atomic_load_n(&cond->magic, __ATOMIC_ACQUIRE) == PTHR_COND_MAGIC)
    return 0;

  mutexLock(&init_lock);
  if (__atomic_load_n(&cond->magic, __ATOMIC_RELAXED) == PTHR_COND_MAGIC) {
    mutexUnlock(&init_lock);
    return 0;
  }

  pthread_cond_t *real = malloc(sizeof(pthread_cond_t));
  int ret = pthread_cond_init(real, attr);

  if (ret == 0) {
    cond->real_ptr = real;
    __atomic_store_n(&cond->magic, PTHR_COND_MAGIC, __ATOMIC_RELEASE);
  } else {
    free(real);
    debugPrintf("pthr: cond init for %p failed (%d)\n", (void *)cond, ret);
  }
  mutexUnlock(&init_lock);
  return ret;
}

// ---------------------------------------------------------------------------
// core affinity: libnx puts every new thread on the same default core, so the
// game's render/loader/worker threads all pile onto one CPU. When a thread
// busy-waits for a peer that shares its core, the peer never runs and the
// whole thing pins one core at 100% (the post-audio black-screen hang). The
// Vita port pins AndroidMain/renderThread/NuThread to distinct cores; we
// round-robin every spawned game thread across the cores Horizon allows us.
// ---------------------------------------------------------------------------

static Mutex core_lock;
static int core_list[4];
static int core_count = 0;
static unsigned core_rr = 0;
static uintptr_t role_android_main = 0;
static uintptr_t role_render_thread = 0;
static uintptr_t role_nu_thread = 0;

static void core_init_once(void) {
  if (core_count)
    return;
  u64 mask = 0;
  if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)) || mask == 0)
    mask = 0x7; // fallback: cores 0,1,2 (typical homebrew allotment)
  for (int c = 0; c < 4; c++)
    if (mask & (1u << c))
      core_list[core_count++] = c;
  debugPrintf("pthr: %d cores available (mask 0x%x)\n", core_count, (unsigned)mask);
}

// number of leading cores reserved for the hot game roles (AndroidMain,
// renderThread, NuThread); everything else is kept off them
#define ROLE_CORES 3

static void assign_core_slot(unsigned slot) {
  mutexLock(&core_lock);
  core_init_once();
  const int core = core_list[slot % core_count];
  mutexUnlock(&core_lock);
  // preferred core + single-core affinity mask = hard pin, like the Vita pins
  svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
}

// background threads (audio, resource loaders, GC, etc.) round-robin over the
// cores NOT reserved for the hot roles. Previously they round-robined over ALL
// cores, so an audio/loader thread could land on the render thread's core and
// starve it -- the SFX-induced frame stutter with the GPU not maxed. With 4
// cores the roles take 0/1/2 and everything else runs on core 3.
static void assign_core_bg(void) {
  mutexLock(&core_lock);
  core_init_once();
  int bg_start = (core_count > ROLE_CORES) ? ROLE_CORES : 0;
  int bg_count = core_count - bg_start;
  if (bg_count <= 0) { bg_start = 0; bg_count = core_count; }
  const int core = core_list[bg_start + (core_rr++ % (unsigned)bg_count)];
  mutexUnlock(&core_lock);
  svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
}

// pin the calling thread to a background core. For threads our vendored code
// spawns directly through newlib pthread_create (the SDL audio mixer and the
// OpenSL play-callback thread) -- they never pass through the trampoline, so
// without this they'd default onto a hot game core and stutter the renderer.
void pthr_pin_bg_core(void) {
  assign_core_bg();
}

void pthr_set_role_symbols(uintptr_t android_main, uintptr_t render_thread,
                           uintptr_t nu_thread) {
  role_android_main = android_main;
  role_render_thread = render_thread;
  role_nu_thread = nu_thread;
  debugPrintf("pthr: role symbols AndroidMain=%p renderThread=%p NuThread=%p\n",
              (void *)role_android_main, (void *)role_render_thread,
              (void *)role_nu_thread);
}

static const char *role_for_start(void *(*start)(void *), unsigned *slot) {
  const uintptr_t addr = (uintptr_t)start;
  if (addr && addr == role_android_main) {
    *slot = 0;
    return "AndroidMain";
  }
  if (addr && addr == role_render_thread) {
    *slot = 1;
    return "renderThread";
  }
  if (addr && addr == role_nu_thread) {
    *slot = 2;
    return "NuThread";
  }
  return NULL;
}

// ---------------------------------------------------------------------------
// thread creation (installs the fake TLS before running game code)
// ---------------------------------------------------------------------------

typedef struct {
  void *(*start)(void *);
  void *arg;
} ThreadStart;

static void *thread_trampoline(void *p) {
  ThreadStart s = *(ThreadStart *)p;
  free(p);
  pthr_install_fake_tls();
  unsigned slot = 0;
  const char *role = role_for_start(s.start, &slot);
  if (role) {
    assign_core_slot(slot);
    if (slot == 1)
      tls_is_render_thread = 1;
    static int role_logs = 0;
    if (role_logs++ < 12)
      debugPrintf("pthr: %s thread pinned to role slot %u\n", role, slot);
  } else {
    assign_core_bg();
  }
  void *ret = s.start(s.arg);
  // unconditional: a thread exiting while owning the GL context would orphan
  // it forever (EGL bindings are per-thread; nobody else can release it)
  extern void egl_gl_ownership_release(void);
  egl_gl_ownership_release();
  return ret;
}

int pthread_create_soloader(pthread_t *thread, const pthread_attr_t_bionic *attr,
                            void *(*start)(void *), void *param) {
  ThreadStart *s = malloc(sizeof(*s));
  s->start = start;
  s->arg = param;

  pthread_attr_t a;
  pthread_attr_init(&a);
  // the engine's worker/render threads recurse deeply; give them headroom
  pthread_attr_setstacksize(&a, 2 * 1024 * 1024);
  if (attr) {
    attr_static_init((pthread_attr_t_bionic *)attr);
    size_t want = 0;
    if (attr->real_ptr && pthread_attr_getstacksize(attr->real_ptr, &want) == 0 &&
        want > 2 * 1024 * 1024)
      pthread_attr_setstacksize(&a, want);
  }

  int ret = pthread_create(thread, &a, thread_trampoline, s);
  pthread_attr_destroy(&a);
  if (ret != 0)
    free(s);
  return ret;
}

int pthread_join_soloader(pthread_t thread, void **value_ptr) { return pthread_join(thread, value_ptr); }
int pthread_detach_soloader(pthread_t thread) { return pthread_detach(thread); }
pthread_t pthread_self_soloader(void) { return pthread_self(); }

int pthread_equal_soloader(pthread_t t1, pthread_t t2) {
  if (t1 == t2) return 1;
  if (!t1 || !t2) return 0;
  return pthread_equal(t1, t2);
}

int pthread_getschedparam_soloader(pthread_t thread, int *policy, struct sched_param *param) {
  // newlib on devkitA64 doesn't expose pthread_getschedparam; the game only
  // reads these to echo them back, so reporting a default schedule is fine
  (void)thread;
  if (policy) *policy = 0; // SCHED_OTHER
  if (param) param->sched_priority = 0;
  return 0;
}

int pthread_once_soloader(volatile int *once_control, void (*init_routine)(void)) {
  if (!once_control || !init_routine)
    return -1;
  if (__sync_lock_test_and_set(once_control, 1) == 0)
    (*init_routine)();
  return 0;
}

// ---------------------------------------------------------------------------
// mutex / cond / attr
// ---------------------------------------------------------------------------

int pthread_mutexattr_init_soloader(pthread_mutexattr_t *attr) { return pthread_mutexattr_init(attr); }
int pthread_mutexattr_settype_soloader(pthread_mutexattr_t *attr, int type) { return pthread_mutexattr_settype(attr, type); }
int pthread_mutexattr_destroy_soloader(pthread_mutexattr_t *attr) { return pthread_mutexattr_destroy(attr); }

int pthread_mutex_init_soloader(pthread_mutex_t_bionic *uid, const pthread_mutexattr_t *attr) {
  if (!uid) return EINVAL;
  return mutex_static_init(uid, attr);
}

int pthread_mutex_destroy_soloader(pthread_mutex_t_bionic *mutex) {
  if (!mutex) return 0;
  mutexLock(&init_lock);
  if (__atomic_load_n(&mutex->magic, __ATOMIC_RELAXED) != PTHR_MUTEX_MAGIC) {
    mutexUnlock(&init_lock);
    return 0;
  }
  __atomic_store_n(&mutex->magic, 0, __ATOMIC_RELEASE);
  pthread_mutex_t *real = mutex->real_ptr;
  mutex->real_ptr = NULL;
  mutexUnlock(&init_lock);
  int ret = pthread_mutex_destroy(real);
  free(real);
  return ret;
}

int pthread_mutex_lock_soloader(pthread_mutex_t_bionic *mutex) {
  if (!mutex) return EINVAL;
  mutex_static_init(mutex, NULL);
  // fast path: uncontended
  if (pthread_mutex_trylock(mutex->real_ptr) == 0)
    return 0;
  // contended: never block on a game mutex while holding the real GL context
  // (ABBA against the game's BeginCriticalSectionGL) -- park first
  extern void egl_gl_ownership_park(void);
  egl_gl_ownership_park();
  extern int egl_gl_thread_holds_context(void);
  if (!tls_is_render_thread || !egl_gl_thread_holds_context())
    return pthread_mutex_lock(mutex->real_ptr);
  // render thread still holding (park keeps it): the mutex holder might want
  // exactly that context, so poll with handover service instead of blocking
  extern void egl_gl_service_handover(void);
  for (;;) {
    if (pthread_mutex_trylock(mutex->real_ptr) == 0)
      return 0;
    egl_gl_service_handover();
    struct timespec ts = { 0, 100 * 1000 }; // 0.1 ms
    nanosleep(&ts, NULL);
  }
}

int pthread_mutex_trylock_soloader(pthread_mutex_t_bionic *mutex) {
  if (!mutex) return EINVAL;
  mutex_static_init(mutex, NULL);
  return pthread_mutex_trylock(mutex->real_ptr);
}

int pthread_mutex_unlock_soloader(pthread_mutex_t_bionic *mutex) {
  if (!mutex || !mutex->real_ptr) return EINVAL;
  return pthread_mutex_unlock(mutex->real_ptr);
}

int pthread_cond_init_soloader(pthread_cond_t_bionic *cond, const pthread_condattr_t *attr) {
  if (!cond) return EINVAL;
  return cond_static_init(cond, attr);
}

int pthread_cond_destroy_soloader(pthread_cond_t_bionic *cond) {
  if (!cond) return 0;
  mutexLock(&init_lock);
  if (__atomic_load_n(&cond->magic, __ATOMIC_RELAXED) != PTHR_COND_MAGIC) {
    mutexUnlock(&init_lock);
    return 0;
  }
  __atomic_store_n(&cond->magic, 0, __ATOMIC_RELEASE);
  pthread_cond_t *real = cond->real_ptr;
  cond->real_ptr = NULL;
  mutexUnlock(&init_lock);
  int ret = pthread_cond_destroy(real);
  free(real);
  return ret;
}

int pthread_cond_signal_soloader(pthread_cond_t_bionic *cond) {
  if (!cond) return EINVAL;
  cond_static_init(cond, NULL);
  return pthread_cond_signal(cond->real_ptr);
}

int pthread_cond_broadcast_soloader(pthread_cond_t_bionic *cond) {
  if (!cond) return EINVAL;
  cond_static_init(cond, NULL);
  return pthread_cond_broadcast(cond->real_ptr);
}

// render-thread wait slice: while it parks holding the GL context, its waits
// poll for handover requests at this granularity
#define RENDER_WAIT_SLICE_NS (4 * 1000 * 1000)

static void timespec_add_ns(struct timespec *ts, long ns) {
  ts->tv_nsec += ns;
  while (ts->tv_nsec >= 1000000000L) {
    ts->tv_sec += 1;
    ts->tv_nsec -= 1000000000L;
  }
}

static int timespec_before(const struct timespec *a, const struct timespec *b) {
  if (a->tv_sec != b->tv_sec)
    return a->tv_sec < b->tv_sec;
  return a->tv_nsec < b->tv_nsec;
}

int pthread_cond_wait_soloader(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex) {
  if (!cond || !mutex) return EINVAL;
  cond_static_init(cond, NULL);
  mutex_static_init(mutex, NULL);
  // about to park: hand back the GL context so other threads can render
  extern void egl_gl_ownership_park(void);
  egl_gl_ownership_park();
  extern int egl_gl_thread_holds_context(void);
  if (!tls_is_render_thread || !egl_gl_thread_holds_context())
    return pthread_cond_wait(cond->real_ptr, mutex->real_ptr);
  // render thread still holding the GL binding: wait ONE short slice and
  // report a timeout as a spurious wakeup (legal; predicate loops re-check).
  // Never loop internally: the game may signal without holding the mutex,
  // and a signal landing between laps is lost forever -- the one-shot
  // level-ready signal then leaves the renderer asleep for good.
  extern void egl_gl_service_handover(void);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  timespec_add_ns(&ts, RENDER_WAIT_SLICE_NS);
  int r = pthread_cond_timedwait(cond->real_ptr, mutex->real_ptr, &ts);
  if (r == ETIMEDOUT) {
    egl_gl_service_handover();
    return 0; // spurious wakeup (POSIX/bionic allow them)
  }
  return r;
}

int pthread_cond_timedwait_soloader(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex,
                                    struct timespec *abstime) {
  if (!cond || !mutex) return EINVAL;
  cond_static_init(cond, NULL);
  mutex_static_init(mutex, NULL);
  extern void egl_gl_ownership_park(void);
  egl_gl_ownership_park();
  extern int egl_gl_thread_holds_context(void);
  if (!tls_is_render_thread || !abstime || !egl_gl_thread_holds_context())
    return pthread_cond_timedwait(cond->real_ptr, mutex->real_ptr, abstime);
  // same single-slice spurious-wakeup scheme as pthread_cond_wait_soloader;
  // the caller's real deadline is only ever reported once it actually passes
  extern void egl_gl_service_handover(void);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  timespec_add_ns(&ts, RENDER_WAIT_SLICE_NS);
  const int final_slice = !timespec_before(&ts, abstime);
  int r = pthread_cond_timedwait(cond->real_ptr, mutex->real_ptr,
                                 final_slice ? abstime : &ts);
  if (r == ETIMEDOUT && !final_slice) {
    egl_gl_service_handover();
    return 0; // spurious wakeup before the caller's deadline
  }
  return r;
}

int pthread_attr_init_soloader(pthread_attr_t_bionic *attr) {
  if (!attr) return EINVAL;
  return attr_static_init(attr);
}

int pthread_attr_destroy_soloader(pthread_attr_t_bionic *attr) {
  if (!attr || attr->magic != 0x42424242) return 0;
  int ret = pthread_attr_destroy(attr->real_ptr);
  free(attr->real_ptr);
  attr->magic = 0;
  return ret;
}

int pthread_attr_setdetachstate_soloader(pthread_attr_t_bionic *attr, int state) {
  if (!attr) return -1;
  attr_static_init(attr);
  return pthread_attr_setdetachstate(attr->real_ptr, state);
}

int pthread_attr_setstacksize_soloader(pthread_attr_t_bionic *attr, size_t stacksize) {
  if (!attr) return -1;
  attr_static_init(attr);
  return pthread_attr_setstacksize(attr->real_ptr, stacksize);
}
