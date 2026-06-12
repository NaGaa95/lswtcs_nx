/* keycodes.h -- the subset of Android KeyEvent codes the game's input layer
 * recognises. The wrapper pushes these raw codes through nativeOnKeyDown/Up;
 * NuInputDevicePS::GetGamePadButtonIndex maps them to game buttons.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __KEYCODES_H__
#define __KEYCODES_H__

#define AKEYCODE_HOME          3
#define AKEYCODE_DPAD_UP       19
#define AKEYCODE_DPAD_DOWN     20
#define AKEYCODE_DPAD_LEFT     21
#define AKEYCODE_DPAD_RIGHT    22
#define AKEYCODE_BUTTON_A      96
#define AKEYCODE_BUTTON_B      97
#define AKEYCODE_BUTTON_X      99
#define AKEYCODE_BUTTON_Y      100
#define AKEYCODE_BUTTON_L1     102
#define AKEYCODE_BUTTON_R1     103
#define AKEYCODE_BUTTON_L2     104
#define AKEYCODE_BUTTON_R2     105
#define AKEYCODE_BUTTON_THUMBL 106
#define AKEYCODE_BUTTON_THUMBR 107
#define AKEYCODE_BUTTON_START  108
#define AKEYCODE_BUTTON_SELECT 109

#endif
