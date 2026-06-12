/* main.c
 *
 * LEGO Star Wars: The Complete Saga v2.0.2.02 (Android, arm64) on the Switch.
 *
 * libTTapp.so uses the TTActivity bootstrap: the Java side pushes lifecycle
 * and input through JNI entry points, and the native code owns its own render
 * and worker threads. We replicate that startup sequence here against a fake
 * JNI environment, then pump controller/touch input from the main thread
 * while the game runs itself. The sequence and identity values were taken
 * from gm666q/lswtcs-vita (the ARMv7 build of the same game).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <switch.h>

#include "config.h"
#include "util.h"
#include "error.h"
#include "so_util.h"
#include "hooks.h"
#include "imports.h"
#include "jni_fake.h"
#include "pthr.h"
#include "keycodes.h"

static void *heap_so_base = NULL;
static size_t heap_so_limit = 0;

so_module game_mod; // libTTapp.so

// fake JNI handles passed to the activity natives
#define ACTIVITY_CLASS ((void *)0x42424242)
#define FAKE_SURFACE   ((void *)0x24242424)
#define FAKE_ASSETMGR  ((void *)0x24242424)

// separate the newlib heap from the .so load region
void __libnx_initheap(void) {
  void *addr;
  size_t size = 0, fake_heap_size = 0;
  size_t mem_available = 0, mem_used = 0;

  if (envHasHeapOverride()) {
    addr = envGetHeapOverrideAddr();
    size = envGetHeapOverrideSize();
  } else {
    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (mem_available > mem_used + 0x200000)
      size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
    if (size == 0)
      size = 0x2000000 * 16;
    Result rc = svcSetHeapSize(&addr, size);
    if (R_FAILED(rc))
      diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
  }

  // The newlib heap backs BOTH the game's malloc and mesa's host-side
  // allocations (notably the nouveau GLSL compiler, which is memory hungry).
  // Reserve only a fixed slice for the loaded .so image + headroom and give
  // ALL the rest to newlib. The old fixed cap (384 MB) starved a full level
  // load + shader compile -> GL_OUT_OF_MEMORY and heap-corruption crashes on
  // "new game". This adapts to whatever RAM the launch mode hands us.
  const size_t so_reserve = (size_t)SO_REGION_MB * 1024 * 1024;
  fake_heap_size = (size > so_reserve + so_reserve / 2) ? (size - so_reserve)
                                                        : (size / 2);

  extern char *fake_heap_start;
  extern char *fake_heap_end;
  fake_heap_start = (char *)addr;
  fake_heap_end   = (char *)addr + fake_heap_size;

  heap_so_base = (char *)addr + fake_heap_size;
  heap_so_base = (void *)ALIGN_MEM((uintptr_t)heap_so_base, 0x1000);
  heap_so_limit = (char *)addr + size - (char *)heap_so_base;
}

static void check_syscalls(void) {
  if (!envIsSyscallHinted(0x77))
    fatal_error("svcMapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x78))
    fatal_error("svcUnmapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x73))
    fatal_error("svcSetProcessMemoryPermission is unavailable.");
  if (envGetOwnProcessHandle() == INVALID_HANDLE)
    fatal_error("Own process handle is unavailable.");
}

static void check_data(void) {
  const char *files[] = { SO_NAME, "Audio.dat", "Levels.dat", "Others.dat", "Textures.dat" };
  struct stat st;
  for (unsigned i = 0; i < sizeof(files) / sizeof(*files); ++i) {
    if (stat(files[i], &st) < 0)
      fatal_error("Could not find\n%s.\nCheck your data files in /switch/lswtcs/.", files[i]);
  }
}

static void set_screen_size(int w, int h) {
  if (w <= 0 || h <= 0 || w > 1920 || h > 1080) {
    if (appletGetOperationMode() == AppletOperationMode_Console) {
      screen_width = 1920; screen_height = 1080;
    } else {
      screen_width = 1280; screen_height = 720;
    }
  } else {
    screen_width = w; screen_height = h;
  }
  debugPrintf("screen mode: %dx%d\n", screen_width, screen_height);
}

// ---------------------------------------------------------------------------
// TTActivity JNI entry points (resolved from libTTapp.so)
// ---------------------------------------------------------------------------

static struct {
  void (*nativeSetGamePadConnected)(void *env, void *cls, int connected);
  void (*nativeCacheJNIVars)(void *env, void *cls);
  void (*nativeSetManufacturer)(void *env, void *cls, void *jstr);
  void (*nativeSetModel)(void *env, void *cls, void *jstr);
  void (*nativeSetObbInfo)(void *env, void *cls, int mainVer, int mainSize, int patchVer, int patchSize, void *jverName, int forceETC1);
  void (*nativeAddAssetsPath)(void *env, void *cls, void *jstrArray);
  void (*nativeSetCaps)(void *env, void *cls, int caps);
  void (*nativeSetPath)(void *env, void *cls, void *jstr);
  void (*nativeSetLanguage)(void *env, void *cls, void *jstr);
  void (*nativeSetAndroidVersion)(void *env, void *cls, void *jstr);
  void (*nativeSetAssetManager)(void *env, void *cls, void *assetMgr);
  void (*nativeOnCreate)(void *env, void *cls);
  void (*nativeOnStart)(void *env, void *cls);
  void (*nativeOnResume)(void *env, void *cls);
  void (*nativeOnPause)(void *env, void *cls);
  void (*nativeOnStop)(void *env, void *cls);
  void (*nativeSetSurface)(void *env, void *cls, void *surface);
  void (*nativeSetScreenDimesions)(void *env, void *cls, float xmm, float ymm);
  void (*nativeOnWindowFocusChanged)(void *env, void *cls, int focused);
  void (*nativeOnKeyDown)(void *env, void *cls, int keycode);
  void (*nativeOnKeyUp)(void *env, void *cls, int keycode);
  void (*nativeOnTouchDown)(void *env, void *cls, int pid, int idx, float x, float y);
  void (*nativeOnTouchMove)(void *env, void *cls, int pid, int idx, float x, float y);
  void (*nativeOnTouchUp)(void *env, void *cls, int pid, int idx);
  void (*nativeUpdateGamepadAxisValues)(void *env, void *cls, float hatX, float hatY, float lX, float lY, float rX, float rY);
} act;

static int (*JNI_OnLoad)(void *vm, void *reserved);

#define RESOLVE_ACT(field, sym) \
  act.field = (void *)so_find_addr_rx(&game_mod, sym)

static void resolve_entry_points(void) {
  RESOLVE_ACT(nativeSetGamePadConnected, "Java_com_tt_tech_CheckGamepadStatus_nativeSetGamePadConnected");
  RESOLVE_ACT(nativeCacheJNIVars,        "Java_com_tt_tech_TTActivity_nativeCacheJNIVars");
  RESOLVE_ACT(nativeSetManufacturer,     "Java_com_tt_tech_TTActivity_nativeSetManufacturer");
  RESOLVE_ACT(nativeSetModel,            "Java_com_tt_tech_TTActivity_nativeSetModel");
  RESOLVE_ACT(nativeSetObbInfo,          "Java_com_tt_tech_TTActivity_nativeSetObbInfo");
  RESOLVE_ACT(nativeAddAssetsPath,       "Java_com_tt_tech_TTActivity_nativeAddAssetsPath");
  RESOLVE_ACT(nativeSetCaps,             "Java_com_tt_tech_TTActivity_nativeSetCaps");
  RESOLVE_ACT(nativeSetPath,             "Java_com_tt_tech_TTActivity_nativeSetPath");
  RESOLVE_ACT(nativeSetLanguage,         "Java_com_tt_tech_TTActivity_nativeSetLanguage");
  RESOLVE_ACT(nativeSetAndroidVersion,   "Java_com_tt_tech_TTActivity_nativeSetAndroidVersion");
  RESOLVE_ACT(nativeSetAssetManager,     "Java_com_tt_tech_TTActivity_nativeSetAssetManager");
  RESOLVE_ACT(nativeOnCreate,            "Java_com_tt_tech_TTActivity_nativeOnCreate");
  RESOLVE_ACT(nativeOnStart,             "Java_com_tt_tech_TTActivity_nativeOnStart");
  RESOLVE_ACT(nativeOnResume,            "Java_com_tt_tech_TTActivity_nativeOnResume");
  RESOLVE_ACT(nativeOnPause,             "Java_com_tt_tech_TTActivity_nativeOnPause");
  RESOLVE_ACT(nativeOnStop,              "Java_com_tt_tech_TTActivity_nativeOnStop");
  RESOLVE_ACT(nativeSetSurface,          "Java_com_tt_tech_TTActivity_nativeSetSurface");
  RESOLVE_ACT(nativeSetScreenDimesions,  "Java_com_tt_tech_TTActivity_nativeSetScreenDimesions");
  RESOLVE_ACT(nativeOnWindowFocusChanged,"Java_com_tt_tech_TTActivity_nativeOnWindowFocusChanged");
  RESOLVE_ACT(nativeOnKeyDown,           "Java_com_tt_tech_TTActivity_nativeOnKeyDown");
  RESOLVE_ACT(nativeOnKeyUp,             "Java_com_tt_tech_TTActivity_nativeOnKeyUp");
  RESOLVE_ACT(nativeOnTouchDown,         "Java_com_tt_tech_TTActivity_nativeOnTouchDown");
  RESOLVE_ACT(nativeOnTouchMove,         "Java_com_tt_tech_TTActivity_nativeOnTouchMove");
  RESOLVE_ACT(nativeOnTouchUp,           "Java_com_tt_tech_TTActivity_nativeOnTouchUp");
  RESOLVE_ACT(nativeUpdateGamepadAxisValues, "Java_com_tt_tech_TTActivity_nativeUpdateGamepadAxisValues");
  JNI_OnLoad = (void *)so_find_addr_rx(&game_mod, "JNI_OnLoad");
}

// ---------------------------------------------------------------------------
// startup sequence (mirrors lswtcs-vita tt_activity_on_create / main)
// ---------------------------------------------------------------------------

static void run_startup_sequence(void) {
  debugPrintf("JNI_OnLoad\n");
  JNI_OnLoad(fake_vm, NULL);

  act.nativeSetGamePadConnected(fake_env, ACTIVITY_CLASS, 1);

  // onCreate: identity, obb info, asset paths, then start the game
  act.nativeCacheJNIVars(fake_env, ACTIVITY_CLASS);
  act.nativeSetManufacturer(fake_env, ACTIVITY_CLASS, jni_make_string(ANDROID_MANUFACTURER));
  act.nativeSetModel(fake_env, ACTIVITY_CLASS, jni_make_string(ANDROID_MODEL));
  act.nativeSetObbInfo(fake_env, ACTIVITY_CLASS, OBB_MAIN_VERSION, OBB_MAIN_SIZE,
                       OBB_PATCH_VERSION, OBB_PATCH_SIZE,
                       jni_make_string(APK_VERSION_NAME), OBB_FORCE_ETC1);

  const char *asset_paths[] = {
    ASSET_PACK_AUDIO_PATH, ASSET_PACK_LEVELS_PATH,
    ASSET_PACK_OTHERS_PATH, ASSET_PACK_TEXTURES_PATH,
  };
  act.nativeAddAssetsPath(fake_env, ACTIVITY_CLASS, jni_make_string_array(4, asset_paths));
  act.nativeSetCaps(fake_env, ACTIVITY_CLASS, 0);

  act.nativeSetPath(fake_env, ACTIVITY_CLASS, jni_make_string(INTERNAL_PATH));
  act.nativeSetLanguage(fake_env, ACTIVITY_CLASS, jni_make_string(config_locale_str()));
  act.nativeSetAndroidVersion(fake_env, ACTIVITY_CLASS, jni_make_string(ANDROID_VERSION_RELEASE));
  act.nativeSetAssetManager(fake_env, ACTIVITY_CLASS, FAKE_ASSETMGR);
  debugPrintf("nativeOnCreate\n");
  act.nativeOnCreate(fake_env, ACTIVITY_CLASS);

  act.nativeOnStart(fake_env, ACTIVITY_CLASS);
  act.nativeOnResume(fake_env, ACTIVITY_CLASS);

  // surface created + changed
  act.nativeSetSurface(fake_env, ACTIVITY_CLASS, FAKE_SURFACE);
  act.nativeSetSurface(fake_env, ACTIVITY_CLASS, FAKE_SURFACE);
  act.nativeSetScreenDimesions(fake_env, ACTIVITY_CLASS, SCREEN_PHYS_W_MM, SCREEN_PHYS_H_MM);
  act.nativeOnWindowFocusChanged(fake_env, ACTIVITY_CLASS, 1);
  debugPrintf("startup sequence complete\n");
}

// ---------------------------------------------------------------------------
// input pump (main thread)
// ---------------------------------------------------------------------------

typedef struct { u64 hid; int keycode; } PadMap;

// positional mapping: Switch bottom face button is B, so B -> Android A, etc.
static const PadMap pad_map[] = {
  { HidNpadButton_B,      AKEYCODE_BUTTON_A },
  { HidNpadButton_A,      AKEYCODE_BUTTON_B },
  { HidNpadButton_Y,      AKEYCODE_BUTTON_X },
  { HidNpadButton_X,      AKEYCODE_BUTTON_Y },
  { HidNpadButton_L,      AKEYCODE_BUTTON_L1 },
  { HidNpadButton_R,      AKEYCODE_BUTTON_R1 },
  { HidNpadButton_ZL,     AKEYCODE_BUTTON_L2 },
  { HidNpadButton_ZR,     AKEYCODE_BUTTON_R2 },
  { HidNpadButton_StickL, AKEYCODE_BUTTON_THUMBL },
  { HidNpadButton_StickR, AKEYCODE_BUTTON_THUMBR },
  { HidNpadButton_Plus,   AKEYCODE_BUTTON_START },
  { HidNpadButton_Minus,  AKEYCODE_BUTTON_SELECT },
};

static PadState pad;
static u64 pad_prev = 0;

// Switch sticks are circle-clamped (a full diagonal reads ~0.71 per axis)
// but the game expects square axes like the d-pad's (±1, ±1) -- without this
// remap, diagonal movement crawls at ~70% speed. Radial circle->square map:
// pure axes unchanged, scaling linear in deflection.
static void stick_circle_to_square(float *x, float *y) {
  const float ax = fabsf(*x), ay = fabsf(*y);
  const float m = (ax > ay) ? ax : ay;
  if (m < 1e-6f)
    return;
  const float s = sqrtf(*x * *x + *y * *y) / m;
  *x *= s;
  *y *= s;
  if (*x > 1.0f) *x = 1.0f; else if (*x < -1.0f) *x = -1.0f;
  if (*y > 1.0f) *y = 1.0f; else if (*y < -1.0f) *y = -1.0f;
}

static void update_gamepad(void) {
  padUpdate(&pad);
  const u64 down = padGetButtons(&pad);
  const u64 changed = down ^ pad_prev;

  for (unsigned i = 0; i < sizeof(pad_map) / sizeof(*pad_map); i++) {
    if (changed & pad_map[i].hid) {
      if (down & pad_map[i].hid)
        act.nativeOnKeyDown(fake_env, ACTIVITY_CLASS, pad_map[i].keycode);
      else
        act.nativeOnKeyUp(fake_env, ACTIVITY_CLASS, pad_map[i].keycode);
    }
  }
  pad_prev = down;

  const float scale = 1.f / 32767.0f;
  const HidAnalogStickState ls = padGetStickPos(&pad, 0);
  const HidAnalogStickState rs = padGetStickPos(&pad, 1);

  // Android Y axes point down; Switch sticks point up -> negate
  float lx = (float)ls.x * scale;
  float ly = (float)ls.y * -scale;
  float rx = (float)rs.x * scale;
  float ry = (float)rs.y * -scale;
  stick_circle_to_square(&lx, &ly);
  stick_circle_to_square(&rx, &ry);

  float hatX = 0.0f, hatY = 0.0f;
  if (down & HidNpadButton_Left)  hatX -= 1.0f;
  if (down & HidNpadButton_Right) hatX += 1.0f;
  if (down & HidNpadButton_Up)    hatY -= 1.0f;
  if (down & HidNpadButton_Down)  hatY += 1.0f;

  act.nativeUpdateGamepadAxisValues(fake_env, ACTIVITY_CLASS, hatX, hatY, lx, ly, rx, ry);
}

#define MAX_TOUCHES 8

typedef struct { int active; u32 finger_id; float x, y; } TouchSlot;
static TouchSlot touch_prev[MAX_TOUCHES];

static int touch_slot_find(u32 id) {
  for (int i = 0; i < MAX_TOUCHES; i++)
    if (touch_prev[i].active && touch_prev[i].finger_id == id) return i;
  return -1;
}
static int touch_slot_alloc(void) {
  for (int i = 0; i < MAX_TOUCHES; i++)
    if (!touch_prev[i].active) return i;
  return -1;
}

static void update_touch(void) {
  HidTouchScreenState state = { 0 };
  if (!hidGetTouchScreenStates(&state, 1))
    return;

  // libnx reports panel coords in 1280x720; scale to the render surface
  const float sx = (float)screen_width / 1280.0f;
  const float sy = (float)screen_height / 720.0f;

  int seen[MAX_TOUCHES] = { 0 };
  for (int i = 0; i < state.count; i++) {
    const HidTouchState *t = &state.touches[i];
    const float x = (float)t->x * sx;
    const float y = (float)t->y * sy;
    int slot = touch_slot_find(t->finger_id);
    if (slot < 0) {
      slot = touch_slot_alloc();
      if (slot < 0) continue;
      touch_prev[slot].active = 1;
      touch_prev[slot].finger_id = t->finger_id;
      act.nativeOnTouchDown(fake_env, ACTIVITY_CLASS, slot, slot, x, y);
    } else if (x != touch_prev[slot].x || y != touch_prev[slot].y) {
      act.nativeOnTouchMove(fake_env, ACTIVITY_CLASS, slot, slot, x, y);
    }
    touch_prev[slot].x = x;
    touch_prev[slot].y = y;
    seen[slot] = 1;
  }

  for (int slot = 0; slot < MAX_TOUCHES; slot++) {
    if (touch_prev[slot].active && !seen[slot]) {
      act.nativeOnTouchUp(fake_env, ACTIVITY_CLASS, slot, slot);
      touch_prev[slot].active = 0;
    }
  }
}

int main(void) {
  cpu_boost(1);

  if (read_config(CONFIG_NAME) < 0)
    write_config(CONFIG_NAME);

  check_syscalls();
  check_data();
  set_screen_size(config.screen_width, config.screen_height);

  extern char *fake_heap_start;
  const unsigned heap_mb =
      (unsigned)(((char *)heap_so_base - fake_heap_start) / (1024 * 1024));
  debugPrintf("heap: newlib %u MB, lib base %p, lib region %u MB\n",
              heap_mb, heap_so_base, (unsigned)(heap_so_limit / (1024 * 1024)));

  // launched as an applet (album/hbmenu without title override) we only get
  // ~0.5 GB -- the game needs far more and would die in a confusing OOM crash
  // mid-load. Fail up front with instructions instead.
  if (heap_mb < 1500)
    fatal_error("Not enough memory (%u MB).\n\n"
                "Launch hbmenu over a game (hold R while\n"
                "starting any installed title), then start\n"
                "this port from there.", heap_mb);

  if (so_load(&game_mod, SO_NAME, heap_so_base, heap_so_limit) < 0)
    fatal_error("Could not load\n%s.", SO_NAME);

  update_imports();
  so_relocate(&game_mod);
  so_resolve(&game_mod, dynlib_functions, dynlib_numfunctions, 1);

  patch_game();

  resolve_entry_points();

  so_finalize(&game_mod);
  so_flush_caches(&game_mod);

  // the main thread needs the fake TLS (TPIDR_EL0 stack-guard cookie) BEFORE
  // any game code runs -- and the init_array below is game code (libc++
  // static constructors read the cookie; crash log #2 was this exact read)
  pthr_install_fake_tls();

  so_execute_init_array(&game_mod);
  so_free_temp(&game_mod);

  jni_init();
  run_startup_sequence();

  // the first EGL bring-up ran inside the JNI natives above, on THIS thread,
  // which never calls GL again -- hand the context back or the game starves
  extern void egl_gl_ownership_release(void);
  egl_gl_ownership_release();

  padConfigureInput(8, HidNpadStyleSet_NpadStandard);
  padInitializeAny(&pad);
  hidInitializeTouchScreen();

  extern volatile int egl_swap_count; // hooks/egl.c; also gates cpu boost off
#ifdef DEBUG_LOG
  // remaining counters only feed the debug heartbeat and deadmen
  extern volatile int egl_makecurrent_count;
  extern volatile int egl_real_makecurrent_count;
  extern volatile int egl_owner_release_count;
  extern volatile int egl_park_keep_count;
  extern volatile int egl_park_release_count;
  extern volatile int gl_draw_count;
  extern volatile int gl_draw_window_count;
  extern volatile int gl_draw_pbuffer_count;
  extern volatile int gl_draw_pbuffer_skip_count;
  int last_swaps = 0, last_mc = 0, last_real_mc = 0, last_owner_rel = 0;
  int last_draw = 0, last_draw_win = 0, last_draw_pbuf = 0, last_draw_skip = 0;
  int last_park_keep = 0, last_park_rel = 0;
  int stall_beats = 0; // consecutive heartbeats with zero swaps (in-game deadman)
#endif
  u64 ticks = 0;
  int boosting = 1; // cpu_boost(1) was set at the top of main for the boot load

  while (appletMainLoop()) {
    ++ticks;
    update_gamepad();
    update_touch();
    // ~120 Hz input polling; the game renders on its own thread
    svcSleepThread(1000000000ull / 120);

    // if a JNI native run from this loop (onPause/onResume) touched GL and
    // re-acquired a context, hand it over as soon as a game thread asks
    egl_gl_service_handover();

    // boot loading is over once the game has presented a while; it starts
    // swapping during the logo/credits sequence, so ~300 frames covers that
    // stretch (~5-6s) before returning to normal clocks
    if (boosting && egl_swap_count >= 300) {
      cpu_boost(0);
      boosting = 0;
      debugPrintf("boot finished (%d swaps): cpu boost off\n", egl_swap_count);
    }

#ifdef DEBUG_LOG
    // heartbeat: shows whether the render thread is presenting and whether the
    // eglMakeCurrent path is spinning (big delta with no swaps == stuck loader)
    if ((ticks % 1200) == 0) { // every ~10s
      const int swaps = egl_swap_count, mc = egl_makecurrent_count;
      const int real_mc = egl_real_makecurrent_count, owner_rel = egl_owner_release_count;
      const int dr = gl_draw_count, dr_win = gl_draw_window_count;
      const int dr_pbuf = gl_draw_pbuffer_count, dr_skip = gl_draw_pbuffer_skip_count;
      const int pk = egl_park_keep_count, pr = egl_park_release_count;
      debugPrintf("heartbeat: %llu ticks, swaps %d (%+d), makeCurrent %d (%+d), realMC %d (%+d), releases %d (%+d), park keep/rel %+d/%+d, draws %d (%+d) [win %+d pbuf %+d skip %+d] in last 10s\n",
                  (unsigned long long)ticks,
                  swaps, swaps - last_swaps,
                  mc, mc - last_mc,
                  real_mc, real_mc - last_real_mc,
                  owner_rel, owner_rel - last_owner_rel,
                  pk - last_park_keep,
                  pr - last_park_rel,
                  dr, dr - last_draw,
                  dr_win - last_draw_win,
                  dr_pbuf - last_draw_pbuf,
                  dr_skip - last_draw_skip);
      // in-game deadman: swaps frozen for two heartbeats (~20s) after having
      // rendered -> crash so Atmosphère dumps every thread's stack
      if (swaps == last_swaps && swaps > 100)
        stall_beats++;
      else
        stall_beats = 0;
      if (stall_beats >= 2) {
        debugPrintf("deadman: render stalled in-game (swaps frozen at %d for %d heartbeats) -> forcing crash for full thread dump\n",
                    swaps, stall_beats);
        *(volatile u32 *)0 = 0xDEADD00D;
      }
      last_swaps = swaps;
      last_mc = mc;
      last_real_mc = real_mc;
      last_owner_rel = owner_rel;
      last_park_keep = pk;
      last_park_rel = pr;
      last_draw = dr;
      last_draw_win = dr_win;
      last_draw_pbuf = dr_pbuf;
      last_draw_skip = dr_skip;
    }

    // boot deadman: no real rendering after 30s -> crash on purpose so
    // Atmosphère writes a report with every thread's PC and stack
    if (ticks == 3600 && egl_swap_count < 10) {
      debugPrintf("deadman: boot stalled (swaps=%d, makeCurrent=%d) -> forcing crash for full thread dump\n",
                  egl_swap_count, egl_makecurrent_count);
      *(volatile u32 *)0 = 0xDEADDEAD;
    }
#endif
  }

  act.nativeOnPause(fake_env, ACTIVITY_CLASS);
  act.nativeOnStop(fake_env, ACTIVITY_CLASS);

  extern void NX_NORETURN __libnx_exit(int rc);
  __libnx_exit(0);
  return 0;
}
