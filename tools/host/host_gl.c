/* tools/host/host_gl.c -- the port's GLES 1 table (lab_gl.h) served by the
 * Mac's legacy OpenGL (2.1, fixed function: ES 1 is its subset), in an
 * offscreen CGL context, for tools/preview.c. MIT. */
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>

static void orthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { glOrtho(l, r, b, t, n, f); }
static void clear_depthf(GLfloat d) { glClearDepth(d); }

/* the order of LAB_GL_FUNCS in lab_gl.h */
#define HOST_GL_FUNCS(X)                                                                          \
  X(glGetIntegerv) X(glGetFloatv) X(glGetBooleanv) X(glGetPointerv) X(glIsEnabled) X(glEnable)   \
  X(glDisable) X(glMatrixMode) X(glLoadIdentity) X(glLoadMatrixf) X(orthof) X(glViewport)        \
  X(glActiveTexture) X(glClientActiveTexture) X(glBindTexture) X(glTexEnvi) X(glGetTexEnviv)     \
  X(glTexParameteri) X(glBindBuffer) X(glEnableClientState) X(glDisableClientState)             \
  X(glVertexPointer) X(glTexCoordPointer) X(glColorPointer) X(glColor4f) X(glDrawArrays)         \
  X(glColorMask) X(glBlendFunc) X(glReadPixels) X(glGetError) X(glGenTextures) X(glDeleteTextures)\
  X(glTexImage2D) X(glTexSubImage2D) X(glPixelStorei) X(glClear) X(glClearColor) X(glScissor)    \
  X(glFinish) X(glFlush) X(glDepthMask) X(glShadeModel) X(glGetString) X(glGenFramebuffersEXT)   \
  X(glBindFramebufferEXT) X(glFramebufferTexture2DEXT) X(glCheckFramebufferStatusEXT)            \
  X(glDeleteFramebuffersEXT) X(glGenRenderbuffersEXT) X(glBindRenderbufferEXT)                   \
  X(glRenderbufferStorageEXT) X(glFramebufferRenderbufferEXT) X(glDeleteRenderbuffersEXT)        \
  X(clear_depthf) X(glFrontFace)

int host_gl_count(void) {
  int n = 0;
#define C(f) n++;
  HOST_GL_FUNCS(C)
#undef C
  return n;
}

void host_gl_table(void **out) {
  int i = 0;
#define F(f) out[i++] = (void *)f;
  HOST_GL_FUNCS(F)
#undef F
}

int host_gl_init(void) {
  CGLPixelFormatAttribute attrs[] = {kCGLPFAAccelerated, kCGLPFAOpenGLProfile,
                                     (CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy, kCGLPFAColorSize,
                                     (CGLPixelFormatAttribute)24, (CGLPixelFormatAttribute)0};
  CGLPixelFormatObj pf;
  GLint n = 0;
  if (CGLChoosePixelFormat(attrs, &pf, &n) != kCGLNoError || !pf) {
    CGLPixelFormatAttribute soft[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy,
                                      (CGLPixelFormatAttribute)0};
    if (CGLChoosePixelFormat(soft, &pf, &n) != kCGLNoError || !pf)
      return -1;
  }
  CGLContextObj ctx;
  if (CGLCreateContext(pf, NULL, &ctx) != kCGLNoError)
    return -1;
  CGLDestroyPixelFormat(pf);
  CGLSetCurrentContext(ctx);
  printf("[host gl] %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  return 0;
}
