/* jni_fake.h -- fake JNI environment for libTTapp.so
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __JNI_FAKE_H__
#define __JNI_FAKE_H__

// The fake JNIEnv / JavaVM handed to the game. Both are pointers to a pointer
// to a function table, matching the real JNI ABI the game calls through.
extern void *fake_env;
extern void *fake_vm;

void jni_init(void);

// helpers for building the Java objects the startup natives expect
void *jni_make_string(const char *utf);
void *jni_make_string_array(int n, const char **strs);

#endif
