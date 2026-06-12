/* fps.c -- on-screen FPS counter (config.show_fps)
 *
 * Draws with its own tiny GLES2 program and glyph atlas, raw GL only (never
 * through the import hooks), and restores every piece of state it touches so
 * the game's render state and the wrapper's per-context shadows stay intact.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <GLES2/gl2.h>

#include "config.h"
#include "util.h"
#include "font_atlas.h"

static const char fps_vshader_src[] =
  "attribute vec2 aPos;\n"
  "attribute vec2 aUV;\n"
  "uniform vec2 uOff;\n"
  "varying vec2 vUV;\n"
  "void main() {\n"
  "  vUV = aUV;\n"
  "  gl_Position = vec4(aPos + uOff, 0.0, 1.0);\n"
  "}\n";

static const char fps_fshader_src[] =
  "precision mediump float;\n"
  "uniform sampler2D texFont;\n"
  "uniform vec4 uColor;\n"
  "varying vec2 vUV;\n"
  "void main() {\n"
  "  gl_FragColor = vec4(uColor.rgb, uColor.a * texture2D(texFont, vUV).r);\n"
  "}\n";

static struct {
  int ready;
  int failed; // latched: don't retry shader setup every frame
  GLuint prog;
  GLint loc_pos, loc_uv, loc_tex, loc_off, loc_color;
  GLuint tex;
  int uploaded;
  u64 window_start;
  u32 frames;
  char text[8];
} fps;

static GLuint fps_compile(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok)
    debugPrintf("fps: shader compile failed\n");
  return s;
}

static int fps_init(void) {
  if (fps.ready)
    return 1;
  if (fps.failed)
    return 0;
  const GLuint vs = fps_compile(GL_VERTEX_SHADER, fps_vshader_src);
  const GLuint fs = fps_compile(GL_FRAGMENT_SHADER, fps_fshader_src);
  fps.prog = glCreateProgram();
  glAttachShader(fps.prog, vs);
  glAttachShader(fps.prog, fs);
  glLinkProgram(fps.prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(fps.prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    debugPrintf("fps: program link failed, counter disabled\n");
    glDeleteProgram(fps.prog);
    fps.prog = 0;
    fps.failed = 1;
    return 0;
  }
  fps.loc_pos = glGetAttribLocation(fps.prog, "aPos");
  fps.loc_uv = glGetAttribLocation(fps.prog, "aUV");
  fps.loc_tex = glGetUniformLocation(fps.prog, "texFont");
  fps.loc_off = glGetUniformLocation(fps.prog, "uOff");
  fps.loc_color = glGetUniformLocation(fps.prog, "uColor");
  glGenTextures(1, &fps.tex);
  fps.ready = 1;
  return 1;
}

// uploads the glyph atlas on first use; the caller has already saved the
// texture binding and unpack alignment it perturbs
static void fps_atlas_ready(void) {
  if (fps.uploaded)
    return;
  glBindTexture(GL_TEXTURE_2D, fps.tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, FONT_ATLAS_W, FONT_ATLAS_H, 0,
               GL_LUMINANCE, GL_UNSIGNED_BYTE, font_atlas);
  fps.uploaded = 1;
}

// two triangles per glyph into verts (x,y,u,v interleaved)
static int fps_emit(const char *text, float x, float y, float gw, float gh,
                    GLfloat *verts) {
  int quads = 0;
  const int len = (int)strlen(text);
  for (int j = 0; j < len; j++) {
    const char c = text[j];
    if (c == ' ')
      continue;
    const int idx = c - FONT_FIRST;
    const float u0 = (float)((idx % FONT_COLS) * FONT_CELL_W) / (float)FONT_ATLAS_W;
    const float v0 = (float)((idx / FONT_COLS) * FONT_CELL_H) / (float)FONT_ATLAS_H;
    const float u1 = u0 + (float)FONT_CELL_W / (float)FONT_ATLAS_W;
    const float v1 = v0 + (float)FONT_CELL_H / (float)FONT_ATLAS_H;
    const float gx = x + j * gw;
    const float x0 = gx * 2.0f / (float)screen_width - 1.0f;
    const float x1 = (gx + gw) * 2.0f / (float)screen_width - 1.0f;
    const float y0 = 1.0f - y * 2.0f / (float)screen_height;
    const float y1 = 1.0f - (y + gh) * 2.0f / (float)screen_height;
    const GLfloat quad[24] = {
      x0, y0, u0, v0,  x1, y0, u1, v0,  x0, y1, u0, v1,
      x1, y0, u1, v0,  x1, y1, u1, v1,  x0, y1, u0, v1,
    };
    memcpy(verts + quads * 24, quad, sizeof(quad));
    quads++;
  }
  return quads;
}

void fps_render(void) {
  const u64 now = armGetSystemTick();
  const u64 freq = armGetSystemTickFreq();
  fps.frames++;
  if (!fps.window_start)
    fps.window_start = now;
  if (now - fps.window_start >= freq / 2) {
    const float rate = (float)fps.frames * (float)freq / (float)(now - fps.window_start);
    snprintf(fps.text, sizeof(fps.text), "%.0f", rate);
    fps.frames = 0;
    fps.window_start = now;
  }

  if (!fps.text[0] || !fps_init())
    return;

  const float gh = (float)screen_height / 30.0f;
  const float gw = gh * (float)FONT_CELL_W / (float)FONT_CELL_H;
  static GLfloat verts[8 * 24];
  const int quads = fps_emit(fps.text, 10.0f, 8.0f, gw, gh, verts);
  if (!quads)
    return;

  // save everything we touch; the draw targets the default framebuffer
  GLint prev_prog, prev_active, prev_tex0, prev_array_buf, prev_fbo;
  GLint prev_viewport[4], prev_align;
  GLint bsrc_rgb, bdst_rgb, bsrc_a, bdst_a;
  GLint pos_enabled = 0, uv_enabled = 0;
  const GLboolean prev_blend = glIsEnabled(GL_BLEND);
  const GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
  const GLboolean prev_cull = glIsEnabled(GL_CULL_FACE);
  glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buf);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_VIEWPORT, prev_viewport);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align);
  glGetIntegerv(GL_BLEND_SRC_RGB, &bsrc_rgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &bdst_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsrc_a);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &bdst_a);
  glGetVertexAttribiv(fps.loc_pos, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &pos_enabled);
  glGetVertexAttribiv(fps.loc_uv, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &uv_enabled);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex0);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  fps_atlas_ready();
  glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glViewport(0, 0, screen_width, screen_height);
  glUseProgram(fps.prog);
  glBindTexture(GL_TEXTURE_2D, fps.tex);
  glUniform1i(fps.loc_tex, 0);
  glEnableVertexAttribArray(fps.loc_pos);
  glEnableVertexAttribArray(fps.loc_uv);
  glVertexAttribPointer(fps.loc_pos, 2, GL_FLOAT, GL_FALSE, 16, verts);
  glVertexAttribPointer(fps.loc_uv, 2, GL_FLOAT, GL_FALSE, 16, verts + 2);

  // drop shadow first, then the text itself
  glUniform2f(fps.loc_off, 3.0f / (float)screen_width, -3.0f / (float)screen_height);
  glUniform4f(fps.loc_color, 0.0f, 0.0f, 0.0f, 0.9f);
  glDrawArrays(GL_TRIANGLES, 0, quads * 6);
  glUniform2f(fps.loc_off, 0.0f, 0.0f);
  glUniform4f(fps.loc_color, 1.0f, 1.0f, 1.0f, 1.0f);
  glDrawArrays(GL_TRIANGLES, 0, quads * 6);

  if (!pos_enabled)
    glDisableVertexAttribArray(fps.loc_pos);
  if (!uv_enabled)
    glDisableVertexAttribArray(fps.loc_uv);
  glBlendFuncSeparate(bsrc_rgb, bdst_rgb, bsrc_a, bdst_a);
  if (!prev_blend) glDisable(GL_BLEND);
  if (prev_depth) glEnable(GL_DEPTH_TEST);
  if (prev_scissor) glEnable(GL_SCISSOR_TEST);
  if (prev_cull) glEnable(GL_CULL_FACE);
  glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex0);
  glActiveTexture((GLenum)prev_active);
  glUseProgram((GLuint)prev_prog);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev_array_buf);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
}
