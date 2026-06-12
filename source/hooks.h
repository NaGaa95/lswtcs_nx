#ifndef __HOOKS_H__
#define __HOOKS_H__

#include <EGL/egl.h>
#include <GLES2/gl2.h>

// applies engine-level patches to the loaded libTTapp.so (control mapping)
void patch_game(void);

// hooks/egl.c: release the sticky GL-context ownership before a game thread
// blocks (cond wait / sleep), so parked threads can't starve the renderer
void egl_gl_ownership_park(void);

// hooks/egl.c: hand the context over if another thread asked for it -- for
// threads that keep running without calling GL or blocking (busy-spin loops,
// the wrapper main loop); one TLS check when nothing is pending
void egl_gl_service_handover(void);

// instrumented EGL/GL wrappers (hooks/egl.c)
EGLDisplay eglGetDisplayHook(EGLNativeDisplayType display_id);
EGLBoolean eglInitializeHook(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean eglBindAPIHook(EGLenum api);
EGLBoolean eglChooseConfigHook(EGLDisplay dpy, const EGLint *attrib_list,
                               EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLContext eglCreateContextHook(EGLDisplay dpy, EGLConfig config,
                                EGLContext share_context, const EGLint *attrib_list);
EGLSurface eglCreateWindowSurfaceHook(EGLDisplay dpy, EGLConfig config,
                                      EGLNativeWindowType win, const EGLint *attrib_list);
EGLSurface eglCreatePbufferSurfaceHook(EGLDisplay dpy, EGLConfig config,
                                       const EGLint *attrib_list);
EGLBoolean eglDestroySurfaceHook(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglMakeCurrentHook(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLBoolean eglQuerySurfaceHook(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value);
EGLBoolean eglSwapBuffersHook(EGLDisplay dpy, EGLSurface surface);
const GLubyte *glGetStringHook(GLenum name);
void glViewportHook(GLint x, GLint y, GLsizei width, GLsizei height);
void glBindFramebufferHook(GLenum target, GLuint framebuffer);
void glBindBufferHook(GLenum target, GLuint buffer);
GLenum glCheckFramebufferStatusHook(GLenum target);
void glGetIntegervHook(GLenum pname, GLint *data);
void glCopyTexImage2DHook(GLenum target, GLint level, GLenum internalformat,
                          GLint x, GLint y, GLsizei width, GLsizei height,
                          GLint border);
GLuint glCreateShaderHook(GLenum type);
void glShaderSourceHook(GLuint shader, GLsizei count, const GLchar *const *string,
                        const GLint *length);
void glCompileShaderHook(GLuint shader);
GLuint glCreateProgramHook(void);
void glAttachShaderHook(GLuint program, GLuint shader);
void glBindAttribLocationHook(GLuint program, GLuint index, const GLchar *name);
void glLinkProgramHook(GLuint program);
void glUseProgramHook(GLuint program);
void glVertexAttribPointerHook(GLuint index, GLint size, GLenum type,
                               GLboolean normalized, GLsizei stride,
                               const void *pointer);
void glEnableVertexAttribArrayHook(GLuint index);
void glDisableVertexAttribArrayHook(GLuint index);
void glCompressedTexImage2DHook(GLenum target, GLint level, GLenum internalformat,
                                GLsizei width, GLsizei height, GLint border,
                                GLsizei imageSize, const void *data);
void glTexImage2DHook(GLenum target, GLint level, GLint internalformat, GLsizei width,
                      GLsizei height, GLint border, GLenum format, GLenum type,
                      const void *pixels);
void glActiveTextureHook(GLenum texture);
void glBindTextureHook(GLenum target, GLuint texture);
void glBlendEquationSeparateHook(GLenum modeRGB, GLenum modeAlpha);
void glBlendFuncSeparateHook(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
void glBufferDataHook(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void glBufferSubDataHook(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
void glCullFaceHook(GLenum mode);
void glDeleteBuffersHook(GLsizei n, const GLuint *buffers);
void glDeleteFramebuffersHook(GLsizei n, const GLuint *framebuffers);
void glDeleteProgramHook(GLuint program);
void glDeleteRenderbuffersHook(GLsizei n, const GLuint *renderbuffers);
void glDeleteShaderHook(GLuint shader);
void glDeleteTexturesHook(GLsizei n, const GLuint *textures);
void glDepthFuncHook(GLenum func);
void glDepthMaskHook(GLboolean flag);
void glDisableHook(GLenum cap);
void glEnableHook(GLenum cap);
void glFinishHook(void);
void glFlushHook(void);
void glFrontFaceHook(GLenum mode);
void glGenBuffersHook(GLsizei n, GLuint *buffers);
void glGenFramebuffersHook(GLsizei n, GLuint *framebuffers);
void glGenTexturesHook(GLsizei n, GLuint *textures);
void glGenerateMipmapHook(GLenum target);
void glGetActiveAttribHook(GLuint program, GLuint index, GLsizei bufSize,
                           GLsizei *length, GLint *size, GLenum *type, GLchar *name);
void glGetActiveUniformHook(GLuint program, GLuint index, GLsizei bufSize,
                            GLsizei *length, GLint *size, GLenum *type, GLchar *name);
void glGetAttachedShadersHook(GLuint program, GLsizei maxCount,
                              GLsizei *count, GLuint *shaders);
GLint glGetAttribLocationHook(GLuint program, const GLchar *name);
GLenum glGetErrorHook(void);
void glGetProgramInfoLogHook(GLuint program, GLsizei bufSize,
                             GLsizei *length, GLchar *infoLog);
void glGetProgramivHook(GLuint program, GLenum pname, GLint *params);
void glGetShaderSourceHook(GLuint shader, GLsizei bufSize,
                           GLsizei *length, GLchar *source);
void glGetShaderivHook(GLuint shader, GLenum pname, GLint *params);
GLint glGetUniformLocationHook(GLuint program, const GLchar *name);
void glGetVertexAttribPointervHook(GLuint index, GLenum pname, void **pointer);
void glGetVertexAttribivHook(GLuint index, GLenum pname, GLint *params);
void glReleaseShaderCompilerHook(void);
void glTexParameteriHook(GLenum target, GLenum pname, GLint param);
void glUniform1fvHook(GLint location, GLsizei count, const GLfloat *value);
void glUniform1iHook(GLint location, GLint v0);
void glUniform2fvHook(GLint location, GLsizei count, const GLfloat *value);
void glUniform3fvHook(GLint location, GLsizei count, const GLfloat *value);
void glUniform4fvHook(GLint location, GLsizei count, const GLfloat *value);
void glValidateProgramHook(GLuint program);
void glClearHook(GLbitfield mask);
void glClearColorHook(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glDrawElementsHook(GLenum mode, GLsizei count, GLenum type, const void *indices);
void glDrawArraysHook(GLenum mode, GLint first, GLsizei count);
void *eglGetProcAddressHook(const char *name);

#endif
