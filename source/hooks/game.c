/* game.c -- engine-level patches for libTTapp.so
 *
 * The game needs a few small binary-level fixes on Switch: the stock
 * NuInputDevicePS::GetGamePadButtonIndex only recognises a subset of Android
 * keycodes, and SetLevelSfxBits can run before its level-spline table is ready
 * on the Android lifecycle path we emulate here. Threading/TLS is handled in
 * pthr.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdint.h>
#include <string.h>
#include <switch.h>

#include "../util.h"
#include "../so_util.h"
#include "../hooks.h"
#include "../keycodes.h"
#include "../pthr.h"

extern so_module game_mod; // defined in main.c

typedef void (*WorldInfoFunc)(void *world_info);

static WorldInfoFunc orig_SetLevelSfxBits;
static WorldInfoFunc LevelSplines_InitForLevel;

enum {
  WORLDINFO_HEAP_CURSOR_OFF = 0x108,
  WORLDINFO_LEVEL_SPLINES_OFF = 0x2b48,
};

// If the Android binary asks for level SFX before its level-spline table is
// present, keep the original routine on a valid all-null table instead of
// letting it index through NULL. The real initializer overwrites this later if
// the level load path reaches it.
static void *empty_level_spline_table[64];

static int patch_relocation_slot(so_module *mod, const char *symbol, uintptr_t replacement) {
  int patched = 0;

  for (int i = 0; i < mod->elf_hdr->e_shnum; i++) {
    const char *sh_name = mod->shstrtab + mod->sec_hdr[i].sh_name;
    if (strcmp(sh_name, ".rela.plt") != 0 && strcmp(sh_name, ".rela.dyn") != 0)
      continue;

    Elf64_Rela *rels = (Elf64_Rela *)((uintptr_t)mod->load_base + mod->sec_hdr[i].sh_addr);
    const size_t count = mod->sec_hdr[i].sh_size / sizeof(*rels);
    for (size_t j = 0; j < count; j++) {
      Elf64_Sym *sym = &mod->syms[ELF64_R_SYM(rels[j].r_info)];
      const char *name = mod->dynstrtab + sym->st_name;
      const unsigned type = ELF64_R_TYPE(rels[j].r_info);
      if ((type == R_AARCH64_JUMP_SLOT || type == R_AARCH64_GLOB_DAT) &&
          strcmp(name, symbol) == 0) {
        uintptr_t *slot = (uintptr_t *)((uintptr_t)mod->load_base + rels[j].r_offset);
        *slot = replacement + rels[j].r_addend;
        patched++;
      }
    }
  }

  return patched;
}

static void SetLevelSfxBits_safe(void *world_info) {
  if (!orig_SetLevelSfxBits || !world_info)
    return;

  void **level_splines = (void **)((uint8_t *)world_info + WORLDINFO_LEVEL_SPLINES_OFF);
  if (*level_splines == NULL) {
    const void *heap_cursor = *(void **)((uint8_t *)world_info + WORLDINFO_HEAP_CURSOR_OFF);
    if (LevelSplines_InitForLevel && heap_cursor)
      LevelSplines_InitForLevel(world_info);

    if (*level_splines == NULL) {
      *level_splines = empty_level_spline_table;
      debugPrintf("SetLevelSfxBits: using empty level-spline table for world %p\n", world_info);
    } else {
      debugPrintf("SetLevelSfxBits: initialized missing level-spline table for world %p\n", world_info);
    }
  }

  orig_SetLevelSfxBits(world_info);
}

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

  orig_SetLevelSfxBits =
    (WorldInfoFunc)so_try_find_addr_rx(&game_mod, "_Z15SetLevelSfxBitsP11WORLDINFO_s");
  LevelSplines_InitForLevel =
    (WorldInfoFunc)so_try_find_addr_rx(&game_mod, "_Z25LevelSplines_InitForLevelP11WORLDINFO_s");
  if (orig_SetLevelSfxBits) {
    const int patched = patch_relocation_slot(&game_mod, "_Z15SetLevelSfxBitsP11WORLDINFO_s",
                                              (uintptr_t)SetLevelSfxBits_safe);
    debugPrintf("patched SetLevelSfxBits relocation slots: %d\n", patched);
  } else {
    debugPrintf("SetLevelSfxBits not found; cannot install level-spline crash guard\n");
  }

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
