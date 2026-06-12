/* fps.h -- on-screen FPS counter (config.show_fps)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __FPS_H__
#define __FPS_H__

// counts presented frames and draws the rate in the top left corner,
// refreshed twice a second. Must be called with the real GL context bound,
// right before eglSwapBuffers; saves and restores all GL state it touches.
void fps_render(void);

#endif
