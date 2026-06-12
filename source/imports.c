/* imports.c -- libTTapp.so import resolution
 *
 * Every undefined dynamic symbol of libTTapp.so (286 of them) is bound here.
 * GL/EGL go straight to the native mesa/nouveau drivers, audio to the vendored
 * OpenSL ES, threads/locale/fs through the bionic shims, and the rest to
 * newlib. Cross-checked against gm666q/lswtcs-vita's dynlib.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <math.h>
#include <time.h>
#include <locale.h>
#include <errno.h>
#include <sys/stat.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <switch.h>

#include "config.h"
#include "so_util.h"
#include "util.h"
#include "libc_shim.h"
#include "pthr.h"
#include "hooks.h"

// crt/newlib-provided symbols we forward by address
extern uintptr_t __cxa_atexit;
extern uintptr_t __stack_chk_fail;

// SL_IID_* and slCreateEngine come from the OpenSLES headers + vendored lib.

FILE *stderr_fake = NULL; // declared in imports.h; libTTapp uses __sF instead

// ---------------------------------------------------------------------------
// small local shims
// ---------------------------------------------------------------------------

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  (void)prio;
#ifdef DEBUG_LOG
  va_list va;
  static char buf[0x1000];
  va_start(va, fmt);
  vsnprintf(buf, sizeof(buf), fmt, va);
  va_end(va);
  debugPrintf("%s: %s\n", tag ? tag : "?", buf);
#else
  (void)tag; (void)fmt;
#endif
  return 0;
}

static void sincos_fake(double x, double *s, double *c) {
  *s = sin(x);
  *c = cos(x);
}

static int sched_yield_fake(void) {
  // don't hold the GL context across a yield (spin-waiters starve peers);
  // service afterwards for requests that arrived during it
  egl_gl_ownership_park();
  svcSleepThread(0);
  egl_gl_service_handover();
  return 0;
}

// sleeping is a parking point: hand the GL context back first, and service
// after waking for requests that arrived mid-sleep
static int nanosleep_park(const struct timespec *req, struct timespec *rem) {
  egl_gl_ownership_park();
  int r = nanosleep(req, rem);
  egl_gl_service_handover();
  return r;
}

// logged wrapper so debug.log shows the game's audio engine bring-up
static SLresult slCreateEngineHook(SLObjectItf *pEngine, SLuint32 numOptions,
                                   const SLEngineOption *pEngineOptions,
                                   SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
                                   const SLboolean *pInterfaceRequired) {
  debugPrintf("SL: slCreateEngine(pEngine=%p, numOptions=%u, pOpts=%p, numInterfaces=%u)\n",
              (void *)pEngine, (unsigned)numOptions, (void *)pEngineOptions, (unsigned)numInterfaces);
  for (unsigned i = 0; i < numOptions && pEngineOptions; i++)
    debugPrintf("SL:   option[%u] feature=%u data=%u\n", i,
                (unsigned)pEngineOptions[i].feature, (unsigned)pEngineOptions[i].data);
  for (unsigned i = 0; i < numInterfaces && pInterfaceIds; i++)
    debugPrintf("SL:   iface[%u] id=%p required=%d\n", i, (void *)pInterfaceIds[i],
                pInterfaceRequired ? pInterfaceRequired[i] : -1);
  SLresult r = slCreateEngine(pEngine, numOptions, pEngineOptions,
                              numInterfaces, pInterfaceIds, pInterfaceRequired);
  debugPrintf("SL: slCreateEngine -> 0x%x\n", (unsigned)r);
  return r;
}

// ---------------------------------------------------------------------------
// import table
// ---------------------------------------------------------------------------

DynLibFunction dynlib_functions[] = {
  // --- runtime / fortify ---------------------------------------------------
  { "__android_log_print", (uintptr_t)&__android_log_print },
  { "__ctype_get_mb_cur_max", (uintptr_t)&__ctype_get_mb_cur_max_fake },
  { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
  { "__cxa_finalize", (uintptr_t)&ret0 },
  { "__errno", (uintptr_t)&__errno },
  { "__memcpy_chk", (uintptr_t)&__memcpy_chk_fake },
  { "__memmove_chk", (uintptr_t)&__memmove_chk_fake },
  { "__memset_chk", (uintptr_t)&__memset_chk_fake },
  { "__open_2", (uintptr_t)&open2_fake },
  { "__read_chk", (uintptr_t)&__read_chk_fake },
  { "__sF", (uintptr_t)&fake_sF },
  { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
  { "__strcat_chk", (uintptr_t)&__strcat_chk_fake },
  { "__strcpy_chk", (uintptr_t)&__strcpy_chk_fake },
  { "__strlen_chk", (uintptr_t)&__strlen_chk_fake },
  { "__strrchr_chk", (uintptr_t)&__strrchr_chk_fake },
  { "__vsnprintf_chk", (uintptr_t)&__vsnprintf_chk_fake },
  { "__vsprintf_chk", (uintptr_t)&__vsprintf_chk_fake },
  { "android_set_abort_message", (uintptr_t)&android_set_abort_message_fake },

  // --- AAsset / ANativeWindow ---------------------------------------------
  { "AAsset_close", (uintptr_t)&AAsset_close_fake },
  { "AAsset_getLength", (uintptr_t)&AAsset_getLength_fake },
  { "AAsset_read", (uintptr_t)&AAsset_read_fake },
  { "AAsset_seek", (uintptr_t)&AAsset_seek_fake },
  { "AAssetManager_fromJava", (uintptr_t)&AAssetManager_fromJava_fake },
  { "AAssetManager_open", (uintptr_t)&AAssetManager_open_fake },
  { "ANativeWindow_fromSurface", (uintptr_t)&ANativeWindow_fromSurface_fake },
  { "ANativeWindow_getHeight", (uintptr_t)&ANativeWindow_getHeight_fake },
  { "ANativeWindow_getWidth", (uintptr_t)&ANativeWindow_getWidth_fake },
  { "ANativeWindow_setBuffersGeometry", (uintptr_t)&ANativeWindow_setBuffersGeometry_fake },

  // --- math ----------------------------------------------------------------
  { "acosf", (uintptr_t)&acosf },
  { "asinf", (uintptr_t)&asinf },
  { "atan2f", (uintptr_t)&atan2f },
  { "atanf", (uintptr_t)&atanf },
  { "cosf", (uintptr_t)&cosf },
  { "exp", (uintptr_t)&exp },
  { "expf", (uintptr_t)&expf },
  { "ldexp", (uintptr_t)&ldexp },
  { "ldexpf", (uintptr_t)&ldexpf },
  { "log", (uintptr_t)&log },
  { "log10f", (uintptr_t)&log10f },
  { "logf", (uintptr_t)&logf },
  { "pow", (uintptr_t)&pow },
  { "powf", (uintptr_t)&powf },
  { "sin", (uintptr_t)&sin },
  { "sincos", (uintptr_t)&sincos_fake },
  { "sincosf", (uintptr_t)&sincosf_fake },
  { "sinf", (uintptr_t)&sinf },

  // --- memory / stdlib -----------------------------------------------------
  { "abort", (uintptr_t)&abort },
  { "calloc", (uintptr_t)&calloc },
  { "exit", (uintptr_t)&exit },
  { "free", (uintptr_t)&free },
  { "malloc", (uintptr_t)&malloc },
  { "memchr", (uintptr_t)&memchr },
  { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy },
  { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },
  { "posix_memalign", (uintptr_t)&posix_memalign_fake },
  { "qsort", (uintptr_t)&qsort },
  { "rand", (uintptr_t)&rand },
  { "realloc", (uintptr_t)&realloc },
  { "srand", (uintptr_t)&srand },
  { "strtod", (uintptr_t)&strtod },
  { "strtof", (uintptr_t)&strtof },
  { "strtol", (uintptr_t)&strtol },
  { "strtold", (uintptr_t)&strtold },
  { "strtold_l", (uintptr_t)&strtold_l_fake },
  { "strtoll", (uintptr_t)&strtoll },
  { "strtoll_l", (uintptr_t)&strtoll_l_fake },
  { "strtoul", (uintptr_t)&strtoul },
  { "strtoull", (uintptr_t)&strtoull },
  { "strtoull_l", (uintptr_t)&strtoull_l_fake },

  // --- strings -------------------------------------------------------------
  { "strcat", (uintptr_t)&strcat },
  { "strchr", (uintptr_t)&strchr },
  { "strcmp", (uintptr_t)&strcmp },
  { "strcoll", (uintptr_t)&strcoll },
  { "strcpy", (uintptr_t)&strcpy },
  { "strerror_r", (uintptr_t)&strerror_r_fake },
  { "strlen", (uintptr_t)&strlen },
  { "strncpy", (uintptr_t)&strncpy },
  { "strrchr", (uintptr_t)&strrchr },
  { "strxfrm", (uintptr_t)&strxfrm },

  // --- ctype ---------------------------------------------------------------
  { "islower", (uintptr_t)&islower },
  { "isupper", (uintptr_t)&isupper },
  { "isxdigit", (uintptr_t)&isxdigit },
  { "tolower", (uintptr_t)&tolower },
  { "toupper", (uintptr_t)&toupper },
  { "towlower", (uintptr_t)&towlower },
  { "towupper", (uintptr_t)&towupper },

  // --- wide char / wctype --------------------------------------------------
  { "btowc", (uintptr_t)&btowc },
  { "iswalpha", (uintptr_t)&iswalpha },
  { "iswblank", (uintptr_t)&iswblank },
  { "iswcntrl", (uintptr_t)&iswcntrl },
  { "iswdigit", (uintptr_t)&iswdigit },
  { "iswlower", (uintptr_t)&iswlower },
  { "iswprint", (uintptr_t)&iswprint },
  { "iswpunct", (uintptr_t)&iswpunct },
  { "iswspace", (uintptr_t)&iswspace },
  { "iswupper", (uintptr_t)&iswupper },
  { "iswxdigit", (uintptr_t)&iswxdigit },
  { "mbrlen", (uintptr_t)&mbrlen },
  { "mbrtowc", (uintptr_t)&mbrtowc },
  { "mbsnrtowcs", (uintptr_t)&mbsnrtowcs_fake },
  { "mbsrtowcs", (uintptr_t)&mbsrtowcs },
  { "mbtowc", (uintptr_t)&mbtowc },
  { "wcrtomb", (uintptr_t)&wcrtomb },
  { "wcscoll", (uintptr_t)&wcscoll },
  { "wcslen", (uintptr_t)&wcslen },
  { "wcsnrtombs", (uintptr_t)&wcsnrtombs_fake },
  { "wcstod", (uintptr_t)&wcstod },
  { "wcstof", (uintptr_t)&wcstof },
  { "wcstol", (uintptr_t)&wcstol },
  { "wcstold", (uintptr_t)&wcstold },
  { "wcstoll", (uintptr_t)&wcstoll },
  { "wcstoul", (uintptr_t)&wcstoul },
  { "wcstoull", (uintptr_t)&wcstoull },
  { "wcsxfrm", (uintptr_t)&wcsxfrm },
  { "wctob", (uintptr_t)&wctob },
  { "wmemchr", (uintptr_t)&wmemchr },
  { "wmemcmp", (uintptr_t)&wmemcmp },
  { "wmemcpy", (uintptr_t)&wmemcpy },
  { "wmemmove", (uintptr_t)&wmemmove },
  { "wmemset", (uintptr_t)&wmemset },

  // --- locale --------------------------------------------------------------
  { "freelocale", (uintptr_t)&freelocale_fake },
  { "localeconv", (uintptr_t)&localeconv },
  { "newlocale", (uintptr_t)&newlocale_fake },
  { "setlocale", (uintptr_t)&setlocale },
  { "uselocale", (uintptr_t)&uselocale_fake },

  // --- stdio ---------------------------------------------------------------
  { "fclose", (uintptr_t)&fclose_fake },
  { "fflush", (uintptr_t)&fflush_fake },
  { "fopen", (uintptr_t)&fopen_fake },
  { "fprintf", (uintptr_t)&fprintf_fake },
  { "fputc", (uintptr_t)&fputc_fake },
  { "fread", (uintptr_t)&fread_fake },
  { "fseek", (uintptr_t)&fseek_fake },
  { "ftell", (uintptr_t)&ftell },
  { "fwrite", (uintptr_t)&fwrite_fake },
  { "getc", (uintptr_t)&getc_fake },
  { "printf", (uintptr_t)&debugPrintf },
  { "putc", (uintptr_t)&putc },
  { "putchar", (uintptr_t)&putchar },
  { "puts", (uintptr_t)&puts },
  { "snprintf", (uintptr_t)&snprintf },
  { "sscanf", (uintptr_t)&sscanf },
  { "swprintf", (uintptr_t)&swprintf },
  { "ungetc", (uintptr_t)&ungetc_fake },
  { "vasprintf", (uintptr_t)&vasprintf },
  { "vfprintf", (uintptr_t)&vfprintf_fake },
  { "vsnprintf", (uintptr_t)&vsnprintf },
  { "vsprintf", (uintptr_t)&vsprintf },
  { "vsscanf", (uintptr_t)&vsscanf },

  // --- filesystem ----------------------------------------------------------
  { "chdir", (uintptr_t)&chdir_fake },
  { "close", (uintptr_t)&close },
  { "mkdir", (uintptr_t)&mkdir_fake },
  { "remove", (uintptr_t)&remove_fake },
  { "rename", (uintptr_t)&rename_fake },

  // --- syslog (no-op) ------------------------------------------------------
  { "closelog", (uintptr_t)&ret0 },
  { "openlog", (uintptr_t)&ret0 },
  { "syslog", (uintptr_t)&ret0 },

  // --- time / sched / misc syscalls ---------------------------------------
  { "clock_gettime", (uintptr_t)&clock_gettime_fake },
  { "dl_iterate_phdr", (uintptr_t)&so_dl_iterate_phdr },
  { "nanosleep", (uintptr_t)&nanosleep_park },
  { "sched_yield", (uintptr_t)&sched_yield_fake },
  { "strftime", (uintptr_t)&strftime },
  { "syscall", (uintptr_t)&syscall_fake },
  { "sysconf", (uintptr_t)&sysconf_fake },

  // --- EGL (native mesa, through the logging/compat hooks) -----------------
  { "eglBindAPI", (uintptr_t)&eglBindAPIHook },
  { "eglChooseConfig", (uintptr_t)&eglChooseConfigHook },
  { "eglCreateContext", (uintptr_t)&eglCreateContextHook },
  { "eglCreatePbufferSurface", (uintptr_t)&eglCreatePbufferSurfaceHook },
  { "eglCreateWindowSurface", (uintptr_t)&eglCreateWindowSurfaceHook },
  { "eglDestroySurface", (uintptr_t)&eglDestroySurfaceHook },
  { "eglGetConfigAttrib", (uintptr_t)&eglGetConfigAttrib },
  { "eglGetDisplay", (uintptr_t)&eglGetDisplayHook },
  { "eglGetError", (uintptr_t)&eglGetError },
  { "eglGetProcAddress", (uintptr_t)&eglGetProcAddressHook },
  { "eglInitialize", (uintptr_t)&eglInitializeHook },
  { "eglMakeCurrent", (uintptr_t)&eglMakeCurrentHook },
  { "eglQuerySurface", (uintptr_t)&eglQuerySurfaceHook },
  { "eglSwapBuffers", (uintptr_t)&eglSwapBuffersHook },

  // --- GLES2 (native mesa) -------------------------------------------------
  { "glActiveTexture", (uintptr_t)&glActiveTextureHook },
  { "glAttachShader", (uintptr_t)&glAttachShaderHook },
  { "glBindAttribLocation", (uintptr_t)&glBindAttribLocationHook },
  { "glBindBuffer", (uintptr_t)&glBindBufferHook },
  { "glBindFramebuffer", (uintptr_t)&glBindFramebufferHook },
  { "glBindTexture", (uintptr_t)&glBindTextureHook },
  { "glBlendEquationSeparate", (uintptr_t)&glBlendEquationSeparateHook },
  { "glBlendFuncSeparate", (uintptr_t)&glBlendFuncSeparateHook },
  { "glBufferData", (uintptr_t)&glBufferDataHook },
  { "glBufferSubData", (uintptr_t)&glBufferSubDataHook },
  { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatusHook },
  { "glClear", (uintptr_t)&glClearHook },
  { "glClearColor", (uintptr_t)&glClearColorHook },
  { "glCompileShader", (uintptr_t)&glCompileShaderHook },
  { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2DHook },
  { "glCopyTexImage2D", (uintptr_t)&glCopyTexImage2DHook },
  { "glCreateProgram", (uintptr_t)&glCreateProgramHook },
  { "glCreateShader", (uintptr_t)&glCreateShaderHook },
  { "glCullFace", (uintptr_t)&glCullFaceHook },
  { "glDeleteBuffers", (uintptr_t)&glDeleteBuffersHook },
  { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffersHook },
  { "glDeleteProgram", (uintptr_t)&glDeleteProgramHook },
  { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffersHook },
  { "glDeleteShader", (uintptr_t)&glDeleteShaderHook },
  { "glDeleteTextures", (uintptr_t)&glDeleteTexturesHook },
  { "glDepthFunc", (uintptr_t)&glDepthFuncHook },
  { "glDepthMask", (uintptr_t)&glDepthMaskHook },
  { "glDisable", (uintptr_t)&glDisableHook },
  { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArrayHook },
  { "glDrawArrays", (uintptr_t)&glDrawArraysHook },
  { "glDrawElements", (uintptr_t)&glDrawElementsHook },
  { "glEnable", (uintptr_t)&glEnableHook },
  { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArrayHook },
  { "glFinish", (uintptr_t)&glFinishHook },
  { "glFlush", (uintptr_t)&glFlushHook },
  { "glFrontFace", (uintptr_t)&glFrontFaceHook },
  { "glGenBuffers", (uintptr_t)&glGenBuffersHook },
  { "glGenFramebuffers", (uintptr_t)&glGenFramebuffersHook },
  { "glGenTextures", (uintptr_t)&glGenTexturesHook },
  { "glGenerateMipmap", (uintptr_t)&glGenerateMipmapHook },
  { "glGetActiveAttrib", (uintptr_t)&glGetActiveAttribHook },
  { "glGetActiveUniform", (uintptr_t)&glGetActiveUniformHook },
  { "glGetAttachedShaders", (uintptr_t)&glGetAttachedShadersHook },
  { "glGetAttribLocation", (uintptr_t)&glGetAttribLocationHook },
  { "glGetError", (uintptr_t)&glGetErrorHook },
  { "glGetIntegerv", (uintptr_t)&glGetIntegervHook },
  { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLogHook },
  { "glGetProgramiv", (uintptr_t)&glGetProgramivHook },
  { "glGetShaderSource", (uintptr_t)&glGetShaderSourceHook },
  { "glGetShaderiv", (uintptr_t)&glGetShaderivHook },
  { "glGetString", (uintptr_t)&glGetStringHook },
  { "glGetUniformLocation", (uintptr_t)&glGetUniformLocationHook },
  { "glGetVertexAttribPointerv", (uintptr_t)&glGetVertexAttribPointervHook },
  { "glGetVertexAttribiv", (uintptr_t)&glGetVertexAttribivHook },
  { "glLinkProgram", (uintptr_t)&glLinkProgramHook },
  { "glReleaseShaderCompiler", (uintptr_t)&glReleaseShaderCompilerHook },
  { "glShaderSource", (uintptr_t)&glShaderSourceHook },
  { "glTexImage2D", (uintptr_t)&glTexImage2DHook },
  { "glTexParameteri", (uintptr_t)&glTexParameteriHook },
  { "glUniform1fv", (uintptr_t)&glUniform1fvHook },
  { "glUniform1i", (uintptr_t)&glUniform1iHook },
  { "glUniform2fv", (uintptr_t)&glUniform2fvHook },
  { "glUniform3fv", (uintptr_t)&glUniform3fvHook },
  { "glUniform4fv", (uintptr_t)&glUniform4fvHook },
  { "glUseProgram", (uintptr_t)&glUseProgramHook },
  { "glValidateProgram", (uintptr_t)&glValidateProgramHook },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointerHook },
  { "glViewport", (uintptr_t)&glViewportHook },

  // --- OpenSL ES (vendored libOpenSLES) ------------------------------------
  { "SL_IID_ANDROIDSIMPLEBUFFERQUEUE", (uintptr_t)&SL_IID_ANDROIDSIMPLEBUFFERQUEUE },
  { "SL_IID_ENGINE", (uintptr_t)&SL_IID_ENGINE },
  { "SL_IID_ENGINECAPABILITIES", (uintptr_t)&SL_IID_ENGINECAPABILITIES },
  { "SL_IID_ENVIRONMENTALREVERB", (uintptr_t)&SL_IID_ENVIRONMENTALREVERB },
  { "SL_IID_PLAY", (uintptr_t)&SL_IID_PLAY },
  { "SL_IID_VOLUME", (uintptr_t)&SL_IID_VOLUME },
  { "slCreateEngine", (uintptr_t)&slCreateEngineHook },

  // --- pthread (bionic<->newlib wrappers in pthr.c) ------------------------
  { "pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy_soloader },
  { "pthread_attr_init", (uintptr_t)&pthread_attr_init_soloader },
  { "pthread_attr_setdetachstate", (uintptr_t)&pthread_attr_setdetachstate_soloader },
  { "pthread_attr_setschedparam", (uintptr_t)&ret0 },
  { "pthread_attr_setstacksize", (uintptr_t)&pthread_attr_setstacksize_soloader },
  { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_soloader },
  { "pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy_soloader },
  { "pthread_cond_init", (uintptr_t)&pthread_cond_init_soloader },
  { "pthread_cond_signal", (uintptr_t)&pthread_cond_signal_soloader },
  { "pthread_cond_timedwait", (uintptr_t)&pthread_cond_timedwait_soloader },
  { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_soloader },
  { "pthread_create", (uintptr_t)&pthread_create_soloader },
  { "pthread_detach", (uintptr_t)&pthread_detach_soloader },
  { "pthread_equal", (uintptr_t)&pthread_equal_soloader },
  { "pthread_exit", (uintptr_t)&pthread_exit },
  { "pthread_getschedparam", (uintptr_t)&pthread_getschedparam_soloader },
  { "pthread_getspecific", (uintptr_t)&pthread_getspecific },
  { "pthread_join", (uintptr_t)&pthread_join_soloader },
  { "pthread_key_create", (uintptr_t)&pthread_key_create },
  { "pthread_key_delete", (uintptr_t)&pthread_key_delete },
  { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_soloader },
  { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_soloader },
  { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_soloader },
  { "pthread_mutex_trylock", (uintptr_t)&pthread_mutex_trylock_soloader },
  { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_soloader },
  { "pthread_mutexattr_destroy", (uintptr_t)&pthread_mutexattr_destroy_soloader },
  { "pthread_mutexattr_init", (uintptr_t)&pthread_mutexattr_init_soloader },
  { "pthread_mutexattr_settype", (uintptr_t)&pthread_mutexattr_settype_soloader },
  { "pthread_once", (uintptr_t)&pthread_once_soloader },
  { "pthread_self", (uintptr_t)&pthread_self_soloader },
  { "pthread_setspecific", (uintptr_t)&pthread_setspecific },
};

size_t dynlib_numfunctions = sizeof(dynlib_functions) / sizeof(*dynlib_functions);

void update_imports(void) {
  // no config-driven hook swaps yet
}
