/* game.c -- engine-level patches for libTTapp.so
 *
 * The only patch the game needs for correctness is the gamepad button mapping:
 * the stock NuInputDevicePS::GetGamePadButtonIndex only recognises a subset of
 * Android keycodes, so we replace it with the full Switch mapping (mirrors
 * gm666q/lswtcs-vita's patch.c). Threading/TLS is handled in pthr.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdint.h>
#include <switch.h>

#include "../util.h"
#include "../so_util.h"
#include "../hooks.h"
#include "../keycodes.h"
#include "../pthr.h"

extern so_module game_mod; // defined in main.c

// libTTapp.so's internal gamepad button bitmask (from the game's own enum,
// verified against the lswtcs-vita port for build 20202)
enum {
  GAMEPAD_ACTION  = 0x00000080,
  GAMEPAD_JUMP    = 0x00000040,
  GAMEPAD_SPECIAL = 0x00000020,
  GAMEPAD_TAG     = 0x00000010,
  GAMEPAD_L1      = 0x00000004,
  GAMEPAD_R1      = 0x00000008,
  GAMEPAD_L2      = 0x00000001,
  GAMEPAD_R2      = 0x00000002,
  GAMEPAD_L3      = 0x00000200,
  GAMEPAD_R3      = 0x00000400,
  GAMEPAD_START   = 0x00000800,
};

// NuInputDevicePS::GetGamePadButtonIndex(int androidKeyCode, int *isSuccess)
static unsigned int GetGamePadButtonIndex(int android_code, int *is_success) {
  *is_success = 1;
  switch (android_code) {
    case AKEYCODE_HOME:
    case AKEYCODE_BUTTON_START:  return GAMEPAD_START;
    case AKEYCODE_DPAD_UP:
    case AKEYCODE_DPAD_DOWN:
    case AKEYCODE_DPAD_LEFT:
    case AKEYCODE_DPAD_RIGHT:    return 0; // d-pad is read as an axis
    case AKEYCODE_BUTTON_A:      return GAMEPAD_JUMP;
    case AKEYCODE_BUTTON_B:      return GAMEPAD_SPECIAL;
    case AKEYCODE_BUTTON_X:      return GAMEPAD_ACTION;
    case AKEYCODE_BUTTON_Y:      return GAMEPAD_TAG;
    case AKEYCODE_BUTTON_L1:     return GAMEPAD_L1;
    case AKEYCODE_BUTTON_R1:     return GAMEPAD_R1;
    case AKEYCODE_BUTTON_L2:     return GAMEPAD_L2;
    case AKEYCODE_BUTTON_R2:     return GAMEPAD_R2;
    case AKEYCODE_BUTTON_THUMBL: return GAMEPAD_L3;
    case AKEYCODE_BUTTON_THUMBR: return GAMEPAD_R3;
    default:
      *is_success = 0;
      return 0;
  }
}

void patch_game(void) {
  pthr_set_role_symbols(
    so_try_find_addr_rx(&game_mod, "_Z11AndroidMainPv"),
    so_try_find_addr_rx(&game_mod, "_Z17renderThread_mainPv"),
    so_try_find_addr_rx(&game_mod, "_ZN8NuThread10ThreadMainEPv"));

  // patch through the writable load_base mirror: patch_game runs before
  // so_finalize, when the executable-side mapping doesn't exist yet (writing
  // to the _rx address here is what caused the first on-hardware crash)
  uintptr_t addr = so_try_find_addr(&game_mod, "_ZN15NuInputDevicePS21GetGamePadButtonIndexEiPi");
  if (addr) {
    hook_arm64(addr, (uintptr_t)GetGamePadButtonIndex);
    debugPrintf("patched GetGamePadButtonIndex @ %p\n", (void *)addr);
  } else {
    debugPrintf("GetGamePadButtonIndex not found; using stock mapping\n");
  }
}
