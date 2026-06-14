/* libc_shim.c -- bionic-compatible libc wrappers for the 2.1.131 libs
 *
 * libGame.so and libc++_shared.so are linked against bionic. Where the
 * bionic and newlib ABIs differ (struct layouts, flag values, missing
 * functions) we provide converting wrappers here; everything that matches
 * is passed straight through from imports.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <malloc.h>
#include <wchar.h>
#include <wctype.h>
#include <time.h>
#include <sys/stat.h>
#include <switch.h>

#include "config.h"
#include "util.h"
#include "so_util.h"
#include "libc_shim.h"

// ---------------------------------------------------------------------------
// fortify (_chk) wrappers: ignore the object-size argument
// ---------------------------------------------------------------------------

void *__memcpy_chk_fake(void *dst, const void *src, size_t n, size_t dstlen) {
  (void)dstlen;
  return memcpy(dst, src, n);
}

void *__memmove_chk_fake(void *dst, const void *src, size_t n, size_t dstlen) {
  (void)dstlen;
  return memmove(dst, src, n);
}

void *__memset_chk_fake(void *dst, int c, size_t n, size_t dstlen) {
  (void)dstlen;
  return memset(dst, c, n);
}

char *__strrchr_chk_fake(const char *s, int c, size_t slen) {
  (void)slen;
  return strrchr(s, c);
}

long __read_chk_fake(int fd, void *buf, size_t count, size_t buf_size) {
  (void)buf_size;
  return read(fd, buf, count);
}

char *__strcat_chk_fake(char *dst, const char *src, size_t dstlen) {
  (void)dstlen;
  return strcat(dst, src);
}

char *__strchr_chk_fake(const char *s, int c, size_t slen) {
  (void)slen;
  return strchr(s, c);
}

char *__strcpy_chk_fake(char *dst, const char *src, size_t dstlen) {
  (void)dstlen;
  return strcpy(dst, src);
}

size_t __strlen_chk_fake(const char *s, size_t slen) {
  (void)slen;
  return strlen(s);
}

char *__strncat_chk_fake(char *dst, const char *src, size_t n, size_t dstlen) {
  (void)dstlen;
  return strncat(dst, src, n);
}

char *__strncpy_chk_fake(char *dst, const char *src, size_t n, size_t dstlen) {
  (void)dstlen;
  return strncpy(dst, src, n);
}

char *__strncpy_chk2_fake(char *dst, const char *src, size_t n, size_t dstlen, size_t srclen) {
  (void)dstlen; (void)srclen;
  return strncpy(dst, src, n);
}

int __vsnprintf_chk_fake(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, va_list va) {
  (void)flag; (void)slen;
  return vsnprintf(s, maxlen, fmt, va);
}

int __vsprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, va_list va) {
  (void)flag; (void)slen;
  return vsprintf(s, fmt, va);
}

// ---------------------------------------------------------------------------
// misc bionic functions
// ---------------------------------------------------------------------------

int __system_property_get_fake(const char *name, char *value) {
  (void)name;
  value[0] = '\0';
  return 0;
}

unsigned long getauxval_fake(unsigned long type) {
  (void)type;
  return 0;
}

int gettid_fake(void) {
  u64 thread_id = 1;
  if (R_SUCCEEDED(svcGetThreadId(&thread_id, CUR_THREAD_HANDLE)) && thread_id)
    return (int)(thread_id & 0x7fffffff);
  return 1;
}

#define ARM64_SYS_GETTID 178

long syscall_fake(long number, ...) {
  switch (number) {
    case ARM64_SYS_GETTID:
      return gettid_fake();
  }
  debugPrintf("libc: syscall(%ld) -> ENOSYS\n", number);
  errno = ENOSYS;
  return -1;
}

void sincosf_fake(float x, float *s, float *c) {
  *s = sinf(x);
  *c = cosf(x);
}

int sched_get_priority_max_fake(int policy) {
  (void)policy;
  return 0;
}

void android_set_abort_message_fake(const char *msg) {
  debugPrintf("abort message: %s\n", msg ? msg : "(null)");
}

size_t __ctype_get_mb_cur_max_fake(void) {
  return 1;
}

int __register_atfork_fake(void) {
  return 0;
}

int __cxa_thread_atexit_impl_fake(void (*fn)(void *), void *arg, void *dso) {
  // threads never exit cleanly in this port; leak instead of running dtors
  (void)fn; (void)arg; (void)dso;
  return 0;
}

// bionic sysconf constants
#define BIONIC_SC_PAGESIZE 39
#define BIONIC_SC_PAGE_SIZE 40
#define BIONIC_SC_NPROCESSORS_CONF 96
#define BIONIC_SC_NPROCESSORS_ONLN 97
#define BIONIC_SC_PHYS_PAGES 98

long sysconf_fake(int name) {
  switch (name) {
    case BIONIC_SC_PAGESIZE:
    case BIONIC_SC_PAGE_SIZE:
      return 0x1000;
    case BIONIC_SC_NPROCESSORS_CONF:
    case BIONIC_SC_NPROCESSORS_ONLN:
      return 3;
    case BIONIC_SC_PHYS_PAGES:
      return (3ll * 1024 * 1024 * 1024) / 0x1000;
    default:
      debugPrintf("libc: sysconf(%d) -> -1\n", name);
      return -1;
  }
}

long pathconf_fake(const char *path, int name) {
  (void)path; (void)name;
  return -1;
}

// High-resolution clock for all clock ids. The game's frame timer
// (NuTimeGetTicks) calls clock_gettime(CLOCK_REALTIME) and busy-waits in
// NuFrameEnd until the microsecond delta reaches the frame budget -- so the
// clock MUST advance at real-time rate with sub-millisecond resolution.
// newlib's CLOCK_REALTIME on the Switch is RTC-backed and effectively static
// here, which froze that loop forever (the post-audio 100%-CPU hang). We back
// every clock id with the 19.2 MHz system tick, like the Vita port uses
// sceKernelGetProcessTimeWide. A fixed epoch base keeps tv_sec plausible for
// any absolute-time consumer; it cancels out of the deltas the game computes.
#define FAKE_EPOCH_BASE 1700000000ull // ~2023-11, seconds

int clock_gettime_fake(int clk_id, struct timespec *tp) {
  (void)clk_id;
  if (!tp)
    return -1;
  // the NuFrameEnd busy-wait spins here without GL or blocking; make the
  // spin a hand-over service point or it can hold the context forever
  extern void egl_gl_service_handover(void);
  egl_gl_service_handover();
  static u64 freq = 0;
  if (!freq)
    freq = armGetSystemTickFreq(); // 19200000 on the Switch
  const u64 tick = armGetSystemTick();
  tp->tv_sec = (time_t)(FAKE_EPOCH_BASE + tick / freq);
  tp->tv_nsec = (long)(((tick % freq) * 1000000000ull) / freq);
  return 0;
}

// ---------------------------------------------------------------------------
// path remapping
//
// The startup natives hand the game Android-absolute paths (INTERNAL_PATH and
// the four asset-pack .dat paths). The game concatenates and opens these, so
// every filesystem entry point rewrites that prefix onto the game directory
// (the process cwd, which the launcher sets to the .nro's folder). Mirrors
// lswtcs-vita's fix_path().
// ---------------------------------------------------------------------------

const char *fix_path(const char *path) {
  static _Thread_local char buf[2][1024];
  static _Thread_local int which = 0;

  if (!path || path[0] != '/')
    return path; // already relative; nothing to do

  // the four asset packs collapse to a single file in the game directory
  if (!strcmp(path, ASSET_PACK_AUDIO_PATH))    return "Audio.dat";
  if (!strcmp(path, ASSET_PACK_LEVELS_PATH))   return "Levels.dat";
  if (!strcmp(path, ASSET_PACK_OTHERS_PATH))   return "Others.dat";
  if (!strcmp(path, ASSET_PACK_TEXTURES_PATH)) return "Textures.dat";

  // generic: strip the internal-path prefix and anchor the rest to cwd
  const char *rest = strstr(path, INTERNAL_PATH);
  if (rest) {
    rest += strlen(INTERNAL_PATH);
    if (*rest == '/')
      rest++;
    char *out = buf[which];
    which ^= 1;
    if (*rest)
      snprintf(out, sizeof(buf[0]), "./%s", rest);
    else
      snprintf(out, sizeof(buf[0]), ".");
    return out;
  }

  // legacy sdcard paths baked into the engine (save.here, androidTextures...)
  rest = strstr(path, "/TTGames/");
  if (rest) {
    char *out = buf[which];
    which ^= 1;
    snprintf(out, sizeof(buf[0]), ".%s", rest);
    return out;
  }

  return path;
}

// ---------------------------------------------------------------------------
// open() flag translation (bionic/linux -> newlib)
// ---------------------------------------------------------------------------

#define LINUX_O_CREAT  0100
#define LINUX_O_EXCL   0200
#define LINUX_O_TRUNC  01000
#define LINUX_O_APPEND 02000
#define LINUX_O_NONBLOCK 04000

static int convert_open_flags(int flags) {
  int out = flags & 3; // O_RDONLY/O_WRONLY/O_RDWR match
  if (flags & LINUX_O_CREAT)  out |= O_CREAT;
  if (flags & LINUX_O_EXCL)   out |= O_EXCL;
  if (flags & LINUX_O_TRUNC)  out |= O_TRUNC;
  if (flags & LINUX_O_APPEND) out |= O_APPEND;
  return out;
}

int open_fake(const char *path, int flags, ...) {
  int mode = 0666;
  if (flags & LINUX_O_CREAT) {
    va_list va;
    va_start(va, flags);
    mode = va_arg(va, int);
    va_end(va);
  }
  return open(fix_path(path), convert_open_flags(flags), mode);
}

// bionic's fortified open with no variadic mode (read/existing files)
int open2_fake(const char *path, int flags) {
  return open(fix_path(path), convert_open_flags(flags), 0666);
}

int openat_fake(int dirfd, const char *path, int flags, ...) {
  (void)dirfd; // assume AT_FDCWD or absolute paths
  int mode = 0666;
  if (flags & LINUX_O_CREAT) {
    va_list va;
    va_start(va, flags);
    mode = va_arg(va, int);
    va_end(va);
  }
  return open(fix_path(path), convert_open_flags(flags), mode);
}

int unlinkat_fake(int dirfd, const char *path, int flags) {
  (void)dirfd; (void)flags;
  return unlink(fix_path(path));
}

int mkdir_fake(const char *path, unsigned int mode) {
  return mkdir(fix_path(path), mode);
}

int remove_fake(const char *path) {
  return remove(fix_path(path));
}

int rename_fake(const char *from, const char *to) {
  // both sides need remapping; fix_path uses two rotating buffers so a single
  // call to each is safe before we hand them to rename()
  const char *f = fix_path(from);
  const char *t = fix_path(to);
  // Horizon's filesystem RenameFile FAILS if the destination already exists,
  // whereas POSIX rename() atomically replaces it. The game saves atomically
  // (write "<save>.incomplete", then rename over "<save>"), so without
  // clearing the existing target first every save after the first silently
  // fails and all progress is lost on reload. remove() gives POSIX semantics.
  remove(t);
  return rename(f, t);
}

int chdir_fake(const char *path) {
  return chdir(fix_path(path));
}

// ---------------------------------------------------------------------------
// struct stat conversion (bionic aarch64 layout)
// ---------------------------------------------------------------------------

struct bionic_timespec {
  int64_t tv_sec;
  int64_t tv_nsec;
};

struct bionic_stat {
  uint64_t st_dev;
  uint64_t st_ino;
  uint32_t st_mode;
  uint32_t st_nlink;
  uint32_t st_uid;
  uint32_t st_gid;
  uint64_t st_rdev;
  uint64_t __pad1;
  int64_t st_size;
  int32_t st_blksize;
  int32_t __pad2;
  int64_t st_blocks;
  struct bionic_timespec st_atim;
  struct bionic_timespec st_mtim;
  struct bionic_timespec st_ctim;
  uint32_t __unused4;
  uint32_t __unused5;
};

static void convert_stat(const struct stat *in, struct bionic_stat *out) {
  memset(out, 0, sizeof(*out));
  out->st_dev = in->st_dev;
  out->st_ino = in->st_ino;
  out->st_mode = in->st_mode;
  out->st_nlink = in->st_nlink;
  out->st_uid = in->st_uid;
  out->st_gid = in->st_gid;
  out->st_rdev = in->st_rdev;
  out->st_size = in->st_size;
  out->st_blksize = in->st_blksize;
  out->st_blocks = in->st_blocks;
  out->st_atim.tv_sec = in->st_atime;
  out->st_mtim.tv_sec = in->st_mtime;
  out->st_ctim.tv_sec = in->st_ctime;
}

int stat_fake(const char *path, struct bionic_stat *st) {
  struct stat real;
  const int ret = stat(path, &real);
  if (ret == 0)
    convert_stat(&real, st);
  return ret;
}

int fstat_fake(int fd, struct bionic_stat *st) {
  struct stat real;
  const int ret = fstat(fd, &real);
  if (ret == 0)
    convert_stat(&real, st);
  return ret;
}

int lstat_fake(const char *path, struct bionic_stat *st) {
  return stat_fake(path, st);
}

// ---------------------------------------------------------------------------
// dirent conversion (bionic dirent64 layout)
// ---------------------------------------------------------------------------

struct bionic_dirent {
  uint64_t d_ino;
  int64_t d_off;
  uint16_t d_reclen;
  uint8_t d_type;
  char d_name[256];
};

void *readdir_fake(void *dirp) {
  static struct bionic_dirent out; // NOTE: not thread-safe
  struct dirent *e = readdir((DIR *)dirp);
  if (!e)
    return NULL;
  memset(&out, 0, sizeof(out));
  out.d_ino = e->d_ino;
  out.d_reclen = sizeof(out);
  out.d_type = e->d_type;
  snprintf(out.d_name, sizeof(out.d_name), "%s", e->d_name);
  return &out;
}

// ---------------------------------------------------------------------------
// locale: ignore the locale argument and use the C locale versions
// ---------------------------------------------------------------------------

void *newlocale_fake(int mask, const char *locale, void *base) {
  (void)mask; (void)locale; (void)base;
  return (void *)1;
}

void freelocale_fake(void *loc) {
  (void)loc;
}

void *uselocale_fake(void *loc) {
  (void)loc;
  return (void *)1;
}

#define WRAP_ISW_L(fn) int fn##_l_fake(int wc, void *loc) { (void)loc; return fn(wc); }
WRAP_ISW_L(iswalpha)
WRAP_ISW_L(iswblank)
WRAP_ISW_L(iswcntrl)
WRAP_ISW_L(iswdigit)
WRAP_ISW_L(iswlower)
WRAP_ISW_L(iswprint)
WRAP_ISW_L(iswpunct)
WRAP_ISW_L(iswspace)
WRAP_ISW_L(iswupper)
WRAP_ISW_L(iswxdigit)
WRAP_ISW_L(towlower)
WRAP_ISW_L(towupper)

int strcoll_l_fake(const char *a, const char *b, void *loc) {
  (void)loc;
  return strcoll(a, b);
}

size_t strxfrm_l_fake(char *dst, const char *src, size_t n, void *loc) {
  (void)loc;
  return strxfrm(dst, src, n);
}

size_t strftime_l_fake(char *s, size_t max, const char *fmt, const void *tm, void *loc) {
  (void)loc;
  return strftime(s, max, fmt, (const struct tm *)tm);
}

long double strtold_l_fake(const char *s, char **end, void *loc) {
  (void)loc;
  return strtold(s, end);
}

long long strtoll_l_fake(const char *s, char **end, int base, void *loc) {
  (void)loc;
  return strtoll(s, end, base);
}

unsigned long long strtoull_l_fake(const char *s, char **end, int base, void *loc) {
  (void)loc;
  return strtoull(s, end, base);
}

int wcscoll_l_fake(const wchar_t *a, const wchar_t *b, void *loc) {
  (void)loc;
  return wcscoll(a, b);
}

size_t wcsxfrm_l_fake(wchar_t *dst, const wchar_t *src, size_t n, void *loc) {
  (void)loc;
  return wcsxfrm(dst, src, n);
}

size_t mbsnrtowcs_fake(wchar_t *dst, const char **src, size_t nms, size_t len, void *ps) {
  (void)ps;
  // ascii-ish naive conversion
  size_t i = 0;
  const char *s = *src;
  while (i < nms && s[i] && (!dst || i < len)) {
    if (dst) dst[i] = (unsigned char)s[i];
    i++;
  }
  if (dst && i < len) {
    dst[i] = 0;
    *src = NULL;
  }
  return i;
}

size_t wcsnrtombs_fake(char *dst, const wchar_t **src, size_t nwc, size_t len, void *ps) {
  (void)ps;
  size_t i = 0;
  const wchar_t *s = *src;
  while (i < nwc && s[i] && (!dst || i < len)) {
    if (dst) dst[i] = (char)s[i];
    i++;
  }
  if (dst && i < len) {
    dst[i] = 0;
    *src = NULL;
  }
  return i;
}

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------

int posix_memalign_fake(void **out, size_t align, size_t size) {
  void *p = memalign(align, size);
  if (!p)
    return ENOMEM;
  *out = p;
  return 0;
}

// ---------------------------------------------------------------------------
// filesystem odds and ends
// ---------------------------------------------------------------------------

char *realpath_fake(const char *path, char *resolved) {
  if (!resolved)
    resolved = malloc(0x1000);
  strcpy(resolved, path);
  return resolved;
}

int strerror_r_fake(int err, char *buf, size_t len) {
  snprintf(buf, len, "%s", strerror(err));
  return 0;
}

int statvfs_fake(const char *path, void *buf) {
  (void)path;
  memset(buf, 0, 0x70);
  return 0;
}

// ---------------------------------------------------------------------------
// stdio over the fake bionic __sF (stdin/stdout/stderr)
// libc++_shared initializes std::cout/cerr against &__sF[1]/&__sF[2];
// these wrappers absorb accesses to those fake FILEs and forward the rest
// ---------------------------------------------------------------------------

uint8_t fake_sF[3][0x100]; // referenced by imports.c too

static int is_fake_file(const void *f) {
  const uint8_t *p = f;
  const uint8_t *base = (const uint8_t *)fake_sF;
  return p >= base && p < base + sizeof(fake_sF);
}

size_t fwrite_fake(const void *ptr, size_t size, size_t n, FILE *f) {
  if (is_fake_file(f)) {
#ifdef DEBUG_LOG
    static char buf[0x400];
    const size_t total = size * n < sizeof(buf) - 1 ? size * n : sizeof(buf) - 1;
    memcpy(buf, ptr, total);
    buf[total] = '\0';
    debugPrintf("stdio: %s", buf);
#endif
    return n;
  }
  return fwrite(ptr, size, n, f);
}

size_t fread_fake(void *ptr, size_t size, size_t n, FILE *f) {
  if (is_fake_file(f))
    return 0;
  return fread(ptr, size, n, f);
}

int fputc_fake(int c, FILE *f) {
  if (is_fake_file(f))
    return c;
  return fputc(c, f);
}

int fputs_fake(const char *s, FILE *f) {
  if (is_fake_file(f)) {
    debugPrintf("stdio: %s", s);
    return 0;
  }
  return fputs(s, f);
}

int fflush_fake(FILE *f) {
  if (is_fake_file(f) || f == NULL)
    return 0;
  return fflush(f);
}

int fclose_fake(FILE *f) {
  if (is_fake_file(f))
    return 0;
  return fclose(f);
}

int ferror_fake(FILE *f) {
  if (is_fake_file(f))
    return 0;
  return ferror(f);
}

int fileno_fake(FILE *f) {
  if (is_fake_file(f))
    return ((const uint8_t *)f - &fake_sF[0][0]) / 0x100;
  return fileno(f);
}

int fprintf_fake(FILE *f, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  int ret;
  if (is_fake_file(f)) {
#ifdef DEBUG_LOG
    static char buf[0x400];
    ret = vsnprintf(buf, sizeof(buf), fmt, va);
    debugPrintf("stdio: %s", buf);
#else
    ret = 0;
#endif
  } else {
    ret = vfprintf(f, fmt, va);
  }
  va_end(va);
  return ret;
}

int vfprintf_fake(FILE *f, const char *fmt, va_list va) {
  if (is_fake_file(f)) {
#ifdef DEBUG_LOG
    static char buf[0x400];
    int ret = vsnprintf(buf, sizeof(buf), fmt, va);
    debugPrintf("stdio: %s", buf);
    return ret;
#else
    return 0;
#endif
  }
  return vfprintf(f, fmt, va);
}

int fseek_fake(FILE *f, long off, int whence) {
  if (is_fake_file(f))
    return -1;
  return fseek(f, off, whence);
}

int getc_fake(FILE *f) {
  if (is_fake_file(f))
    return -1; // EOF
  return getc(f);
}

int ungetc_fake(int c, FILE *f) {
  if (is_fake_file(f))
    return -1;
  return ungetc(c, f);
}

void setbuf_fake(FILE *f, char *buf) {
  if (is_fake_file(f))
    return;
  setbuf(f, buf);
}

// ---------------------------------------------------------------------------
// AAsset emulation: read "APK assets" straight from the game directory
// ---------------------------------------------------------------------------

typedef struct {
  FILE *f;
  long size;
} Asset;

void *AAssetManager_fromJava_fake(void *env, void *mgr) {
  (void)env; (void)mgr;
  return (void *)1; // any non-NULL token
}

// fopen with a large stream buffer for the big game archives: the engine
// streams the multi-hundred-MB .dat packs with many small reads/seeks, and
// fsdev round trips dominate without buffering.
FILE *fopen_fake(const char *path, const char *mode) {
  const char *p = fix_path(path);
  FILE *f = fopen(p, mode);
  if (f && strchr(mode, 'r')) {
    const char *ext = strrchr(p, '.');
    if (ext && strcasecmp(ext, ".dat") == 0)
      setvbuf(f, NULL, _IOFBF, 256 * 1024);
  }
  if (!f)
    debugPrintf("fopen(%s => %s, %s) FAILED\n", path, p, mode);
  else
    debugPrintf("fopen(%s, %s) ok\n", p, mode);
  return f;
}

// ---------------------------------------------------------------------------
// AAssetManager: APK-bundled assets live in an "assets/" subfolder of the
// game directory (mirrors lswtcs-vita). Most game data is read through fopen
// on the .dat packs instead; missing AAssets are tolerated by the engine.
// ---------------------------------------------------------------------------

void *AAssetManager_open_fake(void *mgr, const char *path, int mode) {
  (void)mgr; (void)mode;
  char full[1024];
  snprintf(full, sizeof(full), "assets/%s", path);
  FILE *f = fopen(full, "rb");
  debugPrintf("AAsset: open(%s) -> %s\n", full, f ? "ok" : "MISSING");
  if (!f)
    return NULL;
  setvbuf(f, NULL, _IOFBF, 16 * 1024);
  Asset *a = malloc(sizeof(*a));
  a->f = f;
  fseek(f, 0, SEEK_END);
  a->size = ftell(f);
  fseek(f, 0, SEEK_SET);
  return a;
}

void AAsset_close_fake(void *asset) {
  Asset *a = asset;
  if (a) {
    fclose(a->f);
    free(a);
  }
}

int AAsset_read_fake(void *asset, void *buf, size_t count) {
  Asset *a = asset;
  return a ? (int)fread(buf, 1, count, a->f) : -1;
}

long AAsset_seek_fake(void *asset, long off, int whence) {
  Asset *a = asset;
  if (!a || fseek(a->f, off, whence) < 0)
    return -1;
  return ftell(a->f);
}

int64_t AAsset_seek64_fake(void *asset, int64_t off, int whence) {
  return AAsset_seek_fake(asset, (long)off, whence);
}

long AAsset_getLength_fake(void *asset) {
  Asset *a = asset;
  return a ? a->size : 0;
}

int64_t AAsset_getLength64_fake(void *asset) {
  Asset *a = asset;
  return a ? a->size : 0;
}

long AAsset_getRemainingLength_fake(void *asset) {
  Asset *a = asset;
  return a ? a->size - ftell(a->f) : 0;
}

int64_t AAsset_getRemainingLength64_fake(void *asset) {
  Asset *a = asset;
  return a ? a->size - ftell(a->f) : 0;
}

// ---------------------------------------------------------------------------
// ANativeWindow -> NWindow mapping
// ---------------------------------------------------------------------------

void *ANativeWindow_fromSurface_fake(void *env, void *surface) {
  (void)env; (void)surface;
  NWindow *win = nwindowGetDefault();
  nwindowSetDimensions(win, screen_width, screen_height);
  debugPrintf("ANativeWindow_fromSurface -> %p (%dx%d)\n", win, screen_width, screen_height);
  return win;
}

int ANativeWindow_getWidth_fake(void *win) {
  (void)win;
  return screen_width;
}

int ANativeWindow_getHeight_fake(void *win) {
  (void)win;
  return screen_height;
}

void ANativeWindow_release_fake(void *win) {
  (void)win;
}

int ANativeWindow_setBuffersGeometry_fake(void *win, int w, int h, int format) {
  (void)format;
  debugPrintf("ANativeWindow_setBuffersGeometry(%d, %d)\n", w, h);
  if (w > 0 && h > 0)
    nwindowSetDimensions((NWindow *)win, w, h);
  return 0;
}

// ---------------------------------------------------------------------------
// pthread extras: rwlocks and semaphores via pointer indirection
// (bionic types are plain structs the game allocates; we stash a pointer
// to the real object in their first bytes, like the mutex fakes)
// ---------------------------------------------------------------------------

typedef struct {
  RwLock lock;
} FakeRwLock;

static FakeRwLock *get_rwlock(void **storage) {
  if (!*storage) {
    FakeRwLock *l = calloc(1, sizeof(*l));
    rwlockInit(&l->lock);
    *storage = l;
  }
  return *storage;
}

int pthread_rwlock_rdlock_fake(void **rw) {
  rwlockReadLock(&get_rwlock(rw)->lock);
  return 0;
}

int pthread_rwlock_wrlock_fake(void **rw) {
  rwlockWriteLock(&get_rwlock(rw)->lock);
  return 0;
}

int pthread_rwlock_unlock_fake(void **rw) {
  FakeRwLock *l = get_rwlock(rw);
  // libnx needs to know which way it was locked
  if (rwlockIsWriteLockHeldByCurrentThread(&l->lock))
    rwlockWriteUnlock(&l->lock);
  else
    rwlockReadUnlock(&l->lock);
  return 0;
}

typedef struct {
  Semaphore sem;
} FakeSem;

int sem_init_fake(void **s, int pshared, unsigned int value) {
  (void)pshared;
  FakeSem *fs = calloc(1, sizeof(*fs));
  semaphoreInit(&fs->sem, value);
  *s = fs;
  return 0;
}

int sem_destroy_fake(void **s) {
  if (s && *s) {
    free(*s);
    *s = NULL;
  }
  return 0;
}

int sem_post_fake(void **s) {
  if (s && *s)
    semaphoreSignal(&((FakeSem *)*s)->sem);
  return 0;
}

int sem_wait_fake(void **s) {
  if (s && *s)
    semaphoreWait(&((FakeSem *)*s)->sem);
  return 0;
}

int sem_trywait_fake(void **s) {
  if (s && *s && semaphoreTryWait(&((FakeSem *)*s)->sem))
    return 0;
  errno = EAGAIN;
  return -1;
}

int sem_getvalue_fake(void **s, int *val) {
  if (s && *s)
    *val = (int)((FakeSem *)*s)->sem.count;
  else
    *val = 0;
  return 0;
}

int pthread_attr_getstacksize_fake(const void *attr, size_t *size) {
  (void)attr;
  *size = 512 * 1024;
  return 0;
}

int pthread_attr_getschedparam_fake(const void *attr, void *param) {
  (void)attr;
  memset(param, 0, 8);
  return 0;
}
