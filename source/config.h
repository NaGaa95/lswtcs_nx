/* config.h -- global configuration and config file handling
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

// Memory split (see __libnx_initheap): the loaded .so image gets this fixed
// reserve, the newlib heap (game malloc + mesa host allocations) gets ALL the
// rest of the Horizon heap. The .so is ~21 MB; 256 MB leaves ample headroom.
#define SO_REGION_MB 256

// LEGO Star Wars: The Complete Saga v2.0.2.02 (build 20202) ships as a single
// arm64 libTTapp.so with a statically linked C++ runtime (no donor needed).
#define SO_NAME "libTTapp.so"
#define CONFIG_NAME "config.txt"
#define LOG_NAME "debug.log"

// Diagnostic build switch: writes debug.log next to the .nro (mirrored over
// nxlink), enables the heartbeat/deadman instrumentation and GL probes.
// Per-line SD writes cost seconds of boot; uncomment only to chase a bug.
//#define DEBUG_LOG 1

// --- Android-side identity handed to the game (mirrors lswtcs-vita) ---------

#define ANDROID_MANUFACTURER "Nintendo"
#define ANDROID_MODEL "Switch"
#define ANDROID_VERSION_RELEASE "5.0.2"
#define APK_VERSION_NAME "2.0.2.02"

// android.os.Build.VERSION.SDK_INT for 5.0.2 (Lollipop)
#define ANDROID_SDK_INT 21

// obb_info.xml: a=mainVer b=mainSize c=patchVer d=patchSize g=forceETC1
#define OBB_MAIN_VERSION 2017
#define OBB_MAIN_SIZE 2
#define OBB_PATCH_VERSION 2017
#define OBB_PATCH_SIZE 0
#define OBB_FORCE_ETC1 1

// The internal path the game prefixes onto its files; every fs entry point in
// the libc shim rewrites this prefix (and the four asset-pack paths) onto the
// game directory (cwd). See fix_path() in libc_shim.c.
#define INTERNAL_PATH "/data/user/0/com.wb.lego.tcs/files"

#define ASSET_PACK_AUDIO_PATH    INTERNAL_PATH "/assetpacks/asset_Audio/20202/20202/assets/Audio.dat"
#define ASSET_PACK_LEVELS_PATH   INTERNAL_PATH "/assetpacks/asset_Levels/20202/20202/assets/Levels.dat"
#define ASSET_PACK_OTHERS_PATH   INTERNAL_PATH "/assetpacks/asset_Others/20202/20202/assets/Others.dat"
#define ASSET_PACK_TEXTURES_PATH INTERNAL_PATH "/assetpacks/asset_Textures/20202/20202/assets/Textures.dat"

// Physical panel size in mm reported through nativeSetScreenDimesions; the
// engine uses it for DPI-based UI/touch scaling. Switch handheld panel is a
// 6.2" 1280x720 (~237 dpi); pass that panel's physical dimensions.
#define SCREEN_PHYS_W_MM 136.7f
#define SCREEN_PHYS_H_MM 76.9f

// actual render/surface size (picked at runtime from docked state)
extern int screen_width;
extern int screen_height;

typedef struct {
  int screen_width;
  int screen_height;
  int language;        // -1 = follow system; else index into the lang table
  int show_fps;        // 1 = small FPS counter in the top left corner
} Config;

extern Config config;

int read_config(const char *file);
int write_config(const char *file);

// resolves config.language (or the system language when -1) to a BCP-47 tag
const char *config_locale_str(void);

#endif
