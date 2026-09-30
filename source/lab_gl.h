/* lab_gl.h -- the GLES 1.1 calls the port makes itself, through pointers
 * looked up once from Mesa (dcr_gl_lookup): the program links libGLESv2's
 * dispatch only, and the ES 1 entry points come from the same glapi. MIT. */
#ifndef LAB_GL_H
#define LAB_GL_H
#define GL_GLEXT_PROTOTYPES 1
#include <GLES/gl.h>
#include <GLES/glext.h>

#define LAB_GL_FUNCS(X)                                                                          \
  X(GetIntegerv) X(GetFloatv) X(GetBooleanv) X(GetPointerv) X(IsEnabled) X(Enable) X(Disable)   \
  X(MatrixMode) X(LoadIdentity) X(LoadMatrixf) X(Orthof) X(Viewport) X(ActiveTexture)           \
  X(ClientActiveTexture) X(BindTexture) X(TexEnvi) X(GetTexEnviv) X(TexParameteri)              \
  X(BindBuffer) X(EnableClientState) X(DisableClientState) X(VertexPointer) X(TexCoordPointer)  \
  X(ColorPointer) X(Color4f) X(DrawArrays) X(ColorMask) X(BlendFunc) X(ReadPixels) X(GetError)  \
  X(GenTextures) X(DeleteTextures) X(TexImage2D) X(TexSubImage2D) X(PixelStorei) X(Clear)       \
  X(ClearColor) X(Scissor) X(Finish) X(Flush) X(DepthMask) X(ShadeModel) X(GetString)           \
  X(GenFramebuffersOES) X(BindFramebufferOES) X(FramebufferTexture2DOES)                        \
  X(CheckFramebufferStatusOES) X(DeleteFramebuffersOES) X(GenRenderbuffersOES)                  \
  X(BindRenderbufferOES) X(RenderbufferStorageOES) X(FramebufferRenderbufferOES)                \
  X(DeleteRenderbuffersOES) X(ClearDepthf) X(FrontFace)

typedef struct {
#define LAB_GL_PTR(n) __typeof__(gl##n) *n;
  LAB_GL_FUNCS(LAB_GL_PTR)
#undef LAB_GL_PTR
} LabGL;

extern LabGL lgl;
/* Look them all up (after the context exists): 0 ok, -1 one is missing. */
int lab_gl_load(void);

#endif
