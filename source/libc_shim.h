/* libc_shim.h -- bionic-compatible libc wrappers for the 2.1.131 libs
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __LIBC_SHIM_H__
#define __LIBC_SHIM_H__

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>

// fortify
void *__memcpy_chk_fake(void *dst, const void *src, size_t n, size_t dstlen);
void *__memmove_chk_fake(void *dst, const void *src, size_t n, size_t dstlen);
void *__memset_chk_fake(void *dst, int c, size_t n, size_t dstlen);
char *__strcat_chk_fake(char *dst, const char *src, size_t dstlen);
char *__strchr_chk_fake(const char *s, int c, size_t slen);
char *__strrchr_chk_fake(const char *s, int c, size_t slen);
char *__strcpy_chk_fake(char *dst, const char *src, size_t dstlen);
size_t __strlen_chk_fake(const char *s, size_t slen);
char *__strncat_chk_fake(char *dst, const char *src, size_t n, size_t dstlen);
char *__strncpy_chk_fake(char *dst, const char *src, size_t n, size_t dstlen);
char *__strncpy_chk2_fake(char *dst, const char *src, size_t n, size_t dstlen, size_t srclen);
int __vsnprintf_chk_fake(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, va_list va);
int __vsprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, va_list va);
long __read_chk_fake(int fd, void *buf, size_t count, size_t buf_size);

// path remapping: rewrites the game's Android-absolute paths onto the game
// directory (cwd). Safe on any path; returns either the input or one of a
// small set of rotating buffers.
const char *fix_path(const char *path);

// misc bionic
int __system_property_get_fake(const char *name, char *value);
unsigned long getauxval_fake(unsigned long type);
int gettid_fake(void);
long syscall_fake(long number, ...);
void sincosf_fake(float x, float *s, float *c);
int sched_get_priority_max_fake(int policy);
void android_set_abort_message_fake(const char *msg);
size_t __ctype_get_mb_cur_max_fake(void);
int __register_atfork_fake(void);
int __cxa_thread_atexit_impl_fake(void (*fn)(void *), void *arg, void *dso);
long sysconf_fake(int name);
long pathconf_fake(const char *path, int name);
struct timespec;
int clock_gettime_fake(int clk_id, struct timespec *tp);

// fs (all remap android-absolute paths through fix_path)
int open_fake(const char *path, int flags, ...);
int open2_fake(const char *path, int flags); // bionic __open_2
int openat_fake(int dirfd, const char *path, int flags, ...);
int unlinkat_fake(int dirfd, const char *path, int flags);
int mkdir_fake(const char *path, unsigned int mode);
int remove_fake(const char *path);
int rename_fake(const char *from, const char *to);
int chdir_fake(const char *path);
struct bionic_stat;
int stat_fake(const char *path, struct bionic_stat *st);
int fstat_fake(int fd, struct bionic_stat *st);
int lstat_fake(const char *path, struct bionic_stat *st);
void *readdir_fake(void *dirp);
char *realpath_fake(const char *path, char *resolved);
int strerror_r_fake(int err, char *buf, size_t len);
int statvfs_fake(const char *path, void *buf);

// locale
void *newlocale_fake(int mask, const char *locale, void *base);
void freelocale_fake(void *loc);
void *uselocale_fake(void *loc);
int iswalpha_l_fake(int wc, void *loc);
int iswblank_l_fake(int wc, void *loc);
int iswcntrl_l_fake(int wc, void *loc);
int iswdigit_l_fake(int wc, void *loc);
int iswlower_l_fake(int wc, void *loc);
int iswprint_l_fake(int wc, void *loc);
int iswpunct_l_fake(int wc, void *loc);
int iswspace_l_fake(int wc, void *loc);
int iswupper_l_fake(int wc, void *loc);
int iswxdigit_l_fake(int wc, void *loc);
int towlower_l_fake(int wc, void *loc);
int towupper_l_fake(int wc, void *loc);
int strcoll_l_fake(const char *a, const char *b, void *loc);
size_t strxfrm_l_fake(char *dst, const char *src, size_t n, void *loc);
size_t strftime_l_fake(char *s, size_t max, const char *fmt, const void *tm, void *loc);
long double strtold_l_fake(const char *s, char **end, void *loc);
long long strtoll_l_fake(const char *s, char **end, int base, void *loc);
unsigned long long strtoull_l_fake(const char *s, char **end, int base, void *loc);
int wcscoll_l_fake(const wchar_t *a, const wchar_t *b, void *loc);
size_t wcsxfrm_l_fake(wchar_t *dst, const wchar_t *src, size_t n, void *loc);
size_t mbsnrtowcs_fake(wchar_t *dst, const char **src, size_t nms, size_t len, void *ps);
size_t wcsnrtombs_fake(char *dst, const wchar_t **src, size_t nwc, size_t len, void *ps);

// memory
int posix_memalign_fake(void **out, size_t align, size_t size);

// stdio over fake __sF
extern uint8_t fake_sF[3][0x100];
size_t fwrite_fake(const void *ptr, size_t size, size_t n, FILE *f);
size_t fread_fake(void *ptr, size_t size, size_t n, FILE *f);
int fputc_fake(int c, FILE *f);
int fputs_fake(const char *s, FILE *f);
int fflush_fake(FILE *f);
int fclose_fake(FILE *f);
int ferror_fake(FILE *f);
int fileno_fake(FILE *f);
int fprintf_fake(FILE *f, const char *fmt, ...);
int vfprintf_fake(FILE *f, const char *fmt, va_list va);
int fseek_fake(FILE *f, long off, int whence);
int getc_fake(FILE *f);
int ungetc_fake(int c, FILE *f);
void setbuf_fake(FILE *f, char *buf);

// buffered fopen for game archives
FILE *fopen_fake(const char *path, const char *mode);

// AAsset emulation
void *AAssetManager_fromJava_fake(void *env, void *mgr);
void *AAssetManager_open_fake(void *mgr, const char *path, int mode);
void AAsset_close_fake(void *a);
int AAsset_read_fake(void *a, void *buf, size_t count);
long AAsset_seek_fake(void *a, long off, int whence);
int64_t AAsset_seek64_fake(void *a, int64_t off, int whence);
long AAsset_getLength_fake(void *a);
int64_t AAsset_getLength64_fake(void *a);
long AAsset_getRemainingLength_fake(void *a);
int64_t AAsset_getRemainingLength64_fake(void *a);

// ANativeWindow -> NWindow
void *ANativeWindow_fromSurface_fake(void *env, void *surface);
int ANativeWindow_getWidth_fake(void *win);
int ANativeWindow_getHeight_fake(void *win);
void ANativeWindow_release_fake(void *win);
int ANativeWindow_setBuffersGeometry_fake(void *win, int w, int h, int format);

// pthread extras
int pthread_rwlock_rdlock_fake(void **rw);
int pthread_rwlock_wrlock_fake(void **rw);
int pthread_rwlock_unlock_fake(void **rw);
int sem_init_fake(void **s, int pshared, unsigned int value);
int sem_destroy_fake(void **s);
int sem_post_fake(void **s);
int sem_wait_fake(void **s);
int sem_trywait_fake(void **s);
int sem_getvalue_fake(void **s, int *val);
int pthread_attr_getstacksize_fake(const void *attr, size_t *size);
int pthread_attr_getschedparam_fake(const void *attr, void *param);

#endif
