/* jni_fake.c -- fake JNI environment for libTTapp.so
 *
 * LEGO Star Wars TCS talks to a tiny Java surface: a handful of static helper
 * methods (Flurry analytics, privacy/ToS links, music check, country code)
 * and two constants (SDK_INT, WINDOW_SERVICE). The startup natives are driven
 * from main.c; everything the game calls back through JNI is dispatched here
 * by name. Method/field signatures were taken from gm666q/lswtcs-vita.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "config.h"
#include "util.h"
#include "jni_fake.h"

#define JNI_OK 0
#define JNI_VERSION_1_6 0x00010006

typedef uint64_t juint;

// ---------------------------------------------------------------------------
// fake object model
// ---------------------------------------------------------------------------

enum {
  TAG_OBJECT = 0x4f424a31, // 'OBJ1'
  TAG_STRING = 0x53545231, // 'STR1'
  TAG_OBJARR = 0x4f415231, // 'OAR1'
  TAG_ID     = 0x4d494431, // 'MID1'
};

typedef struct {
  uint32_t tag;
  char label[96];
} FakeObject;

typedef struct {
  uint32_t tag;
  char *utf;
} FakeString;

typedef struct {
  uint32_t tag;
  int len;
  void **items;
} FakeObjArray;

// method and field IDs are pointers to these records; calls dispatch by name
typedef struct {
  uint32_t tag;
  char name[80];
  char sig[80];
} FakeID;

void *jni_make_object(const char *label) {
  FakeObject *o = calloc(1, sizeof(*o));
  o->tag = TAG_OBJECT;
  strncpy(o->label, label, sizeof(o->label) - 1);
  return o;
}

void *jni_make_string(const char *utf) {
  FakeString *s = calloc(1, sizeof(*s));
  s->tag = TAG_STRING;
  s->utf = strdup(utf ? utf : "");
  return s;
}

void *jni_make_string_array(int n, const char **strs) {
  FakeObjArray *a = calloc(1, sizeof(*a));
  a->tag = TAG_OBJARR;
  a->len = n;
  a->items = calloc(n ? n : 1, sizeof(void *));
  for (int i = 0; i < n; i++)
    a->items[i] = jni_make_string(strs[i]);
  return a;
}

static const char *obj_str(void *jstr) {
  FakeString *s = jstr;
  if (s && s->tag == TAG_STRING)
    return s->utf;
  return "<not-a-string>";
}

// 2-letter country code derived from the active locale tag (e.g. "fr-FR"->"FR")
static const char *country_code(void) {
  static char cc[4];
  const char *tag = config_locale_str();
  const char *dash = strchr(tag, '-');
  if (dash && dash[1]) {
    cc[0] = dash[1];
    cc[1] = dash[2] ? dash[2] : '\0';
    cc[2] = '\0';
    return cc;
  }
  return "US";
}

// ---------------------------------------------------------------------------
// method/field ID pool
// ---------------------------------------------------------------------------

#define MAX_IDS 128
static FakeID id_pool[MAX_IDS];
static int id_count = 0;

static FakeID *get_id(const char *name, const char *sig) {
  for (int i = 0; i < id_count; i++)
    if (!strcmp(id_pool[i].name, name) && !strcmp(id_pool[i].sig, sig))
      return &id_pool[i];
  if (id_count >= MAX_IDS) {
    debugPrintf("JNI: id pool exhausted!\n");
    return &id_pool[0];
  }
  FakeID *id = &id_pool[id_count++];
  id->tag = TAG_ID;
  strncpy(id->name, name, sizeof(id->name) - 1);
  strncpy(id->sig, sig, sizeof(id->sig) - 1);
  return id;
}

// ---------------------------------------------------------------------------
// method dispatch (by name)
// ---------------------------------------------------------------------------

static juint call_boolean(const char *name, va_list va) {
  (void)va;
  if (!strcmp(name, "IsMusicActive"))
    return 0; // no background music from the OS; let the game play its own
  debugPrintf("JNI: CallBooleanMethod(%s) -> false\n", name);
  return 0;
}

static void *call_object(const char *name, va_list va) {
  (void)va;
  if (!strcmp(name, "getCountryCode")) {
    debugPrintf("JNI: getCountryCode -> %s\n", country_code());
    return jni_make_string(country_code());
  }
  debugPrintf("JNI: CallObjectMethod(%s) -> null\n", name);
  return NULL;
}

static void call_void(const char *name, va_list va) {
  if (!strcmp(name, "FlurryEvent")) {
    void *s = va_arg(va, void *);
    debugPrintf("JNI: FlurryEvent(%s)\n", s ? obj_str(s) : "(null)");
    return;
  }
  if (!strcmp(name, "OpenPrivacyPolicy") || !strcmp(name, "OpenTermsOfServices")) {
    // no web browser on the Switch homebrew; ignore
    debugPrintf("JNI: %s ignored\n", name);
    return;
  }
  debugPrintf("JNI: CallVoidMethod(%s) ignored\n", name);
}

static juint get_int_field(const char *name) {
  if (!strcmp(name, "SDK_INT"))
    return ANDROID_SDK_INT;
  debugPrintf("JNI: GetIntField(%s) -> 0\n", name);
  return 0;
}

static void *get_object_field(const char *name) {
  if (!strcmp(name, "WINDOW_SERVICE"))
    return jni_make_string("window");
  debugPrintf("JNI: GetObjectField(%s) -> null\n", name);
  return NULL;
}

// ---------------------------------------------------------------------------
// JNIEnv function table
// ---------------------------------------------------------------------------

static juint j_GetVersion(void *env) { (void)env; return JNI_VERSION_1_6; }

static void *j_FindClass(void *env, const char *name) {
  (void)env;
  debugPrintf("JNI: FindClass(%s)\n", name);
  return jni_make_object(name);
}

static void *j_GetMethodID(void *env, void *cls, const char *name, const char *sig) {
  (void)env; (void)cls;
  debugPrintf("JNI: GetMethodID(%s %s)\n", name, sig);
  return get_id(name, sig);
}

static void *j_GetFieldID(void *env, void *cls, const char *name, const char *sig) {
  (void)env; (void)cls;
  debugPrintf("JNI: GetFieldID(%s %s)\n", name, sig);
  return get_id(name, sig);
}

static void *j_GetObjectClass(void *env, void *obj) {
  (void)env; (void)obj;
  return jni_make_object("class");
}

static void *j_NewGlobalRef(void *env, void *obj) { (void)env; return obj; }
static void *j_NewLocalRef(void *env, void *obj) { (void)env; return obj; }
static juint j_ret0_2(void *env, void *a) { (void)env; (void)a; return 0; }
static juint j_ret0_3(void *env, void *a, void *b) { (void)env; (void)a; (void)b; return 0; }

// --- Call<type>Method --------------------------------------------------------

static juint j_CallBooleanMethodV(void *env, void *obj, FakeID *id, va_list va) {
  (void)env; (void)obj; return call_boolean(id->name, va);
}
static juint j_CallBooleanMethod(void *env, void *obj, FakeID *id, ...) {
  va_list va; va_start(va, id);
  juint r = call_boolean(id->name, va);
  va_end(va); return r;
}

static void *j_CallObjectMethodV(void *env, void *obj, FakeID *id, va_list va) {
  (void)env; (void)obj; return call_object(id->name, va);
}
static void *j_CallObjectMethod(void *env, void *obj, FakeID *id, ...) {
  va_list va; va_start(va, id);
  void *r = call_object(id->name, va);
  va_end(va); return r;
}

static void j_CallVoidMethodV(void *env, void *obj, FakeID *id, va_list va) {
  (void)env; (void)obj; call_void(id->name, va);
}
static void j_CallVoidMethod(void *env, void *obj, FakeID *id, ...) {
  va_list va; va_start(va, id);
  call_void(id->name, va);
  va_end(va);
}

static juint j_CallIntMethodV(void *env, void *obj, FakeID *id, va_list va) {
  (void)env; (void)obj; (void)va;
  debugPrintf("JNI: CallIntMethod(%s) -> 0\n", id->name);
  return 0;
}
static juint j_CallIntMethod(void *env, void *obj, FakeID *id, ...) {
  (void)env; (void)obj;
  debugPrintf("JNI: CallIntMethod(%s) -> 0\n", id->name);
  return 0;
}

// static variants share the dispatchers (we don't distinguish receivers)
static void *j_CallStaticObjectMethodV(void *env, void *cls, FakeID *id, va_list va) {
  (void)env; (void)cls; return call_object(id->name, va);
}
static void *j_CallStaticObjectMethod(void *env, void *cls, FakeID *id, ...) {
  va_list va; va_start(va, id);
  void *r = call_object(id->name, va);
  va_end(va); return r;
}
static juint j_CallStaticBooleanMethodV(void *env, void *cls, FakeID *id, va_list va) {
  (void)env; (void)cls; return call_boolean(id->name, va);
}
static juint j_CallStaticBooleanMethod(void *env, void *cls, FakeID *id, ...) {
  va_list va; va_start(va, id);
  juint r = call_boolean(id->name, va);
  va_end(va); return r;
}
static void j_CallStaticVoidMethodV(void *env, void *cls, FakeID *id, va_list va) {
  (void)env; (void)cls; call_void(id->name, va);
}
static void j_CallStaticVoidMethod(void *env, void *cls, FakeID *id, ...) {
  va_list va; va_start(va, id);
  call_void(id->name, va);
  va_end(va);
}
static juint j_CallStaticIntMethodV(void *env, void *cls, FakeID *id, va_list va) {
  (void)env; (void)cls; (void)va;
  debugPrintf("JNI: CallStaticIntMethod(%s) -> 0\n", id->name);
  return 0;
}
static juint j_CallStaticIntMethod(void *env, void *cls, FakeID *id, ...) {
  (void)env; (void)cls;
  debugPrintf("JNI: CallStaticIntMethod(%s) -> 0\n", id->name);
  return 0;
}

// --- fields ------------------------------------------------------------------

static void *j_GetObjectField(void *env, void *obj, FakeID *id) {
  (void)env; (void)obj; return get_object_field(id->name);
}
static juint j_GetIntField(void *env, void *obj, FakeID *id) {
  (void)env; (void)obj; return get_int_field(id->name);
}

// --- strings -----------------------------------------------------------------

static void *j_NewStringUTF(void *env, const char *utf) {
  (void)env; return jni_make_string(utf);
}
static const char *j_GetStringUTFChars(void *env, void *jstr, uint8_t *is_copy) {
  (void)env; if (is_copy) *is_copy = 0; return obj_str(jstr);
}
static void j_ReleaseStringUTFChars(void *env, void *jstr, const char *utf) {
  (void)env; (void)jstr; (void)utf;
}
static juint j_GetStringUTFLength(void *env, void *jstr) {
  (void)env; return strlen(obj_str(jstr));
}
static juint j_GetStringLength(void *env, void *jstr) {
  (void)env; return strlen(obj_str(jstr));
}

// --- arrays ------------------------------------------------------------------

static juint j_GetArrayLength(void *env, void *arr) {
  (void)env;
  FakeObjArray *a = arr;
  if (a && a->tag == TAG_OBJARR) return a->len;
  return 0;
}
static void *j_GetObjectArrayElement(void *env, void *arr, int idx) {
  (void)env;
  FakeObjArray *a = arr;
  if (a && a->tag == TAG_OBJARR && idx >= 0 && idx < a->len)
    return a->items[idx];
  return jni_make_string("");
}
static void *j_NewObjectArray(void *env, int len, void *cls, void *init) {
  (void)env; (void)cls;
  FakeObjArray *a = calloc(1, sizeof(*a));
  a->tag = TAG_OBJARR;
  a->len = len;
  a->items = calloc(len ? len : 1, sizeof(void *));
  for (int i = 0; i < len; i++) a->items[i] = init;
  return a;
}

// --- misc --------------------------------------------------------------------

static juint j_RegisterNatives(void *env, void *cls, void *methods, int n) {
  (void)env; (void)cls; (void)methods;
  debugPrintf("JNI: RegisterNatives(%d) ignored\n", n);
  return 0;
}
static juint j_GetJavaVM(void *env, void **vm) {
  (void)env; *vm = fake_vm; return JNI_OK;
}
static juint j_ExceptionCheck(void *env) { (void)env; return 0; }
static void *j_ExceptionOccurred(void *env) { (void)env; return NULL; }
static void j_void_1(void *env) { (void)env; }
static void j_DeleteRef(void *env, void *obj) { (void)env; (void)obj; }
static juint j_PushLocalFrame(void *env, int cap) { (void)env; (void)cap; return 0; }
static void *j_PopLocalFrame(void *env, void *result) { (void)env; return result; }

static juint j_unimplemented(void) {
  debugPrintf("JNI: call to unimplemented function slot\n");
  return 0;
}

// ---------------------------------------------------------------------------
// table assembly (indices per the JNI specification)
// ---------------------------------------------------------------------------

static void *env_table[233];
static void **env_table_ptr = env_table;
void *fake_env = &env_table_ptr;

static juint vm_DestroyJavaVM(void *vm) { (void)vm; return JNI_OK; }
static juint vm_AttachCurrentThread(void *vm, void **env, void *args) {
  (void)vm; (void)args; if (env) *env = fake_env; return JNI_OK;
}
static juint vm_DetachCurrentThread(void *vm) { (void)vm; return JNI_OK; }
static juint vm_GetEnv(void *vm, void **env, int version) {
  (void)vm; (void)version; if (env) *env = fake_env; return JNI_OK;
}

static void *vm_table[8];
static void **vm_table_ptr = vm_table;
void *fake_vm = &vm_table_ptr;

void jni_init(void) {
  for (int i = 0; i < 233; i++)
    env_table[i] = (void *)j_unimplemented;

  env_table[4]   = (void *)j_GetVersion;
  env_table[6]   = (void *)j_FindClass;
  env_table[15]  = (void *)j_ExceptionOccurred;
  env_table[16]  = (void *)j_void_1; // ExceptionDescribe
  env_table[17]  = (void *)j_void_1; // ExceptionClear
  env_table[19]  = (void *)j_PushLocalFrame;
  env_table[20]  = (void *)j_PopLocalFrame;
  env_table[21]  = (void *)j_NewGlobalRef;
  env_table[22]  = (void *)j_DeleteRef; // DeleteGlobalRef
  env_table[23]  = (void *)j_DeleteRef; // DeleteLocalRef
  env_table[24]  = (void *)j_ret0_3;    // IsSameObject
  env_table[25]  = (void *)j_NewLocalRef;
  env_table[26]  = (void *)j_ret0_2;    // EnsureLocalCapacity
  env_table[31]  = (void *)j_GetObjectClass;
  env_table[33]  = (void *)j_GetMethodID;
  env_table[34]  = (void *)j_CallObjectMethod;
  env_table[35]  = (void *)j_CallObjectMethodV;
  env_table[37]  = (void *)j_CallBooleanMethod;
  env_table[38]  = (void *)j_CallBooleanMethodV;
  env_table[49]  = (void *)j_CallIntMethod;
  env_table[50]  = (void *)j_CallIntMethodV;
  env_table[61]  = (void *)j_CallVoidMethod;
  env_table[62]  = (void *)j_CallVoidMethodV;
  env_table[94]  = (void *)j_GetFieldID;
  env_table[95]  = (void *)j_GetObjectField;
  env_table[100] = (void *)j_GetIntField;
  env_table[113] = (void *)j_GetMethodID; // GetStaticMethodID
  env_table[114] = (void *)j_CallStaticObjectMethod;
  env_table[115] = (void *)j_CallStaticObjectMethodV;
  env_table[117] = (void *)j_CallStaticBooleanMethod;
  env_table[118] = (void *)j_CallStaticBooleanMethodV;
  env_table[129] = (void *)j_CallStaticIntMethod;
  env_table[130] = (void *)j_CallStaticIntMethodV;
  env_table[141] = (void *)j_CallStaticVoidMethod;
  env_table[142] = (void *)j_CallStaticVoidMethodV;
  env_table[144] = (void *)j_GetFieldID; // GetStaticFieldID
  env_table[145] = (void *)j_GetObjectField; // GetStaticObjectField
  env_table[150] = (void *)j_GetIntField;    // GetStaticIntField
  env_table[164] = (void *)j_GetStringLength;
  env_table[167] = (void *)j_NewStringUTF;
  env_table[168] = (void *)j_GetStringUTFLength;
  env_table[169] = (void *)j_GetStringUTFChars;
  env_table[170] = (void *)j_ReleaseStringUTFChars;
  env_table[171] = (void *)j_GetArrayLength;
  env_table[172] = (void *)j_NewObjectArray;
  env_table[173] = (void *)j_GetObjectArrayElement;
  env_table[215] = (void *)j_RegisterNatives;
  env_table[216] = (void *)j_ret0_2; // UnregisterNatives
  env_table[217] = (void *)j_ret0_2; // MonitorEnter
  env_table[218] = (void *)j_ret0_2; // MonitorExit
  env_table[219] = (void *)j_GetJavaVM;
  env_table[228] = (void *)j_ExceptionCheck;

  vm_table[3] = (void *)vm_DestroyJavaVM;
  vm_table[4] = (void *)vm_AttachCurrentThread;
  vm_table[5] = (void *)vm_DetachCurrentThread;
  vm_table[6] = (void *)vm_GetEnv;
  vm_table[7] = (void *)vm_AttachCurrentThread; // AttachCurrentThreadAsDaemon

  debugPrintf("JNI: fake environment initialized (env=%p vm=%p)\n", fake_env, fake_vm);
}
