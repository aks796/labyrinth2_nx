/* lab_draw.c -- 2D drawing for the port's screens, in GLES 1.
 *
 * Between lab_draw_begin and lab_draw_end everything is in "units" (dp of
 * the portrait: 320 x 480, or window pixels for the composite), y down.
 * begin saves every piece of fixed-function state it changes and end puts
 * it back, because the engine shares the context and keeps its own state
 * from frame to frame (the same care as pvztouch_nx's gl_blit.c).
 *
 * Pictures come from the APK (res/drawable-hdpi-v4/<name>.png or .jpg, or
 * any path): decoded with stb_image, PREMULTIPLIED (Android draws bitmaps
 * with filtering and straight alpha; premultiplied blending avoids the dark
 * fringes GL's filtering gives straight alpha), cached by name. Text is
 * rasterised by lab_font.c into alpha textures, cached by (string, pixel
 * size, weight) and dropped when unused for a few seconds. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "lab.h"
#include "lab_gl.h"
#include "util.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

/* PVRTC 4 bpp (an iPad texture: a .pvr, version 2 header) -> RGBA8. Each
 * 4x4 block has two colours, A and B (RGB 5:5:5 / 5:5:4 opaque, or ARGB
 * 3:4:4:4 / 3:4:4:3), and two bits a pixel between them; the A and B of the
 * blocks around a pixel are blended bilinearly (block centres at 2, 2) and
 * the pixel's weight picks between the two: 0, 3/8, 5/8, 1 (or, with the
 * block's punch-through bit, 0, 1/2, 1/2, 1). Blocks are in Morton order. */
static void pvr_color(uint32_t c, int b, float out[4]) {
  if (b) {
    uint32_t v = c >> 16;
    if (v & 0x8000)
      out[0] = (float)((v >> 10) & 31) / 31, out[1] = (float)((v >> 5) & 31) / 31, out[2] = (float)(v & 31) / 31,
      out[3] = 1;
    else
      out[0] = (float)((v >> 8) & 15) / 15, out[1] = (float)((v >> 4) & 15) / 15, out[2] = (float)(v & 15) / 15,
      out[3] = (float)((v >> 12) & 7) / 7;
  } else {
    uint32_t v = c & 0xffff;
    if (v & 0x8000)
      out[0] = (float)((v >> 10) & 31) / 31, out[1] = (float)((v >> 5) & 31) / 31, out[2] = (float)((v >> 1) & 15) / 15,
      out[3] = 1;
    else
      out[0] = (float)((v >> 8) & 15) / 15, out[1] = (float)((v >> 4) & 15) / 15, out[2] = (float)((v >> 1) & 7) / 7,
      out[3] = (float)((v >> 12) & 7) / 7;
  }
}

static uint32_t morton(uint32_t x, uint32_t y, int bits) {
  uint32_t r = 0;
  for (int i = 0; i < bits; i++)
    r |= ((y >> i) & 1u) << (2 * i) | ((x >> i) & 1u) << (2 * i + 1);
  return r;
}

static uint8_t *pvrtc_decode(const uint8_t *d, size_t len, int *pw, int *ph) {
  if (len < 52 || memcmp(d + 44, "PVR!", 4))
    return NULL;
  uint32_t hl, h, w, flags;
  memcpy(&hl, d, 4), memcpy(&h, d + 4, 4), memcpy(&w, d + 8, 4), memcpy(&flags, d + 16, 4);
  if ((flags & 0xff) != 0x19 || w != h || w < 8 || w > 2048 || (w & (w - 1)) || hl + (size_t)w * h / 2 > len)
    return NULL; /* PVRTC 4 bpp, square, power of two only */
  const uint8_t *data = d + hl;
  int bw = (int)w / 4, bits = 0;
  while ((1 << bits) < bw)
    bits++;
  float *ca = malloc(sizeof(float) * 4 * (size_t)bw * (size_t)bw), *cb = malloc(sizeof(float) * 4 * (size_t)bw * (size_t)bw);
  uint8_t *out = malloc((size_t)w * h * 4);
  if (!ca || !cb || !out) {
    free(ca), free(cb), free(out);
    return NULL;
  }
  for (int by = 0; by < bw; by++)
    for (int bx = 0; bx < bw; bx++) {
      uint32_t c;
      memcpy(&c, data + (size_t)morton((uint32_t)bx, (uint32_t)by, bits) * 8 + 4, 4);
      pvr_color(c, 0, ca + ((size_t)by * bw + bx) * 4);
      pvr_color(c, 1, cb + ((size_t)by * bw + bx) * 4);
    }
  for (int y = 0; y < (int)h; y++) {
    float fy = ((float)y - 2) / 4;
    int y0 = (int)floorf(fy);
    float ty = fy - (float)y0;
    int ya = (y0 % bw + bw) % bw, yb = (ya + 1) % bw;
    for (int x = 0; x < (int)w; x++) {
      float fx = ((float)x - 2) / 4;
      int x0 = (int)floorf(fx);
      float tx = fx - (float)x0;
      int xa = (x0 % bw + bw) % bw, xb = (xa + 1) % bw;
      const uint8_t *blk = data + (size_t)morton((uint32_t)(x / 4), (uint32_t)(y / 4), bits) * 8;
      uint32_t mod, col;
      memcpy(&mod, blk, 4), memcpy(&col, blk + 4, 4);
      uint32_t m = (mod >> (2 * ((y & 3) * 4 + (x & 3)))) & 3;
      float wgt = (col & 1) ? (m == 0 ? 0.0f : m == 3 ? 1.0f : 0.5f) : (m == 0 ? 0.0f : m == 1 ? 0.375f : m == 2 ? 0.625f : 1.0f);
      uint8_t *o = out + ((size_t)y * w + x) * 4;
      for (int k = 0; k < 4; k++) {
        float a = (ca[((size_t)ya * bw + xa) * 4 + k] * (1 - tx) + ca[((size_t)ya * bw + xb) * 4 + k] * tx) * (1 - ty) +
                  (ca[((size_t)yb * bw + xa) * 4 + k] * (1 - tx) + ca[((size_t)yb * bw + xb) * 4 + k] * tx) * ty;
        float b = (cb[((size_t)ya * bw + xa) * 4 + k] * (1 - tx) + cb[((size_t)ya * bw + xb) * 4 + k] * tx) * (1 - ty) +
                  (cb[((size_t)yb * bw + xa) * 4 + k] * (1 - tx) + cb[((size_t)yb * bw + xb) * 4 + k] * tx) * ty;
        float v = (a * (1 - wgt) + b * wgt) * 255.0f + 0.5f;
        o[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
      }
      if ((col & 1) && m == 2)
        o[3] = 0; /* punch-through */
    }
  }
  free(ca), free(cb);
  *pw = (int)w, *ph = (int)h;
  return out;
}

uint8_t *lab_image_decode(const uint8_t *data, size_t len, int *w, int *h) {
  static int once;
  if (!once) {
    once = 1;
    /* iPhone PNGs (CgBI: BGRA, premultiplied, a bare deflate stream) */
    stbi_convert_iphone_png_to_rgb(1);
    stbi_set_unpremultiply_on_load(1);
  }
  uint8_t *p = pvrtc_decode(data, len, w, h);
  if (p)
    return p;
  int n = 0;
  return stbi_load_from_memory(data, (int)len, w, h, &n, 4);
}

/* ================================================================ state */
static float g_units_w = 320, g_units_h = 480; /* the current units */
static float g_scale = 1;                      /* target pixels per unit */
static int g_target_w, g_target_h;
static int g_depth;
static int g_npot = -1;

static const GLenum k_off[] = {GL_CULL_FACE,   GL_ALPHA_TEST, GL_DEPTH_TEST, GL_SCISSOR_TEST,
                               GL_STENCIL_TEST, GL_FOG,       GL_LIGHTING,   GL_COLOR_LOGIC_OP,
                               GL_CLIP_PLANE0,  GL_COLOR_MATERIAL, GL_POLYGON_OFFSET_FILL,
                               GL_DITHER, GL_NORMALIZE, GL_RESCALE_NORMAL};
#define NOFF (sizeof k_off / sizeof k_off[0])
#define MAX_UNITS 4

typedef struct {
  GLint size, type, stride, buffer;
  void *ptr;
} Array;

static struct {
  GLint active, client, units, mode, vp[4], bound, env, abuf, src, dst, scissor[4], shade;
  GLfloat proj[16], model[16], texm[16], color[4];
  GLboolean mask[4], off_was[NOFF], unit_tex[MAX_UNITS], unit_ta[MAX_UNITS], blend, tex, va, ca, na, ta, depth_mask;
  Array vtx, tc, col;
} S;

static void get_array(Array *a, GLenum size, GLenum type, GLenum stride, GLenum buffer, GLenum ptr) {
  lgl.GetIntegerv(size, &a->size);
  lgl.GetIntegerv(type, &a->type);
  lgl.GetIntegerv(stride, &a->stride);
  lgl.GetIntegerv(buffer, &a->buffer);
  lgl.GetPointerv(ptr, &a->ptr);
}

static void put_array(const Array *a, void (*fn)(GLint, GLenum, GLsizei, const void *)) {
  lgl.BindBuffer(GL_ARRAY_BUFFER, (GLuint)a->buffer);
  fn(a->size, (GLenum)a->type, a->stride, a->ptr);
}

static void enable_if(GLenum cap, GLboolean on) {
  if (on)
    lgl.Enable(cap);
  else
    lgl.Disable(cap);
}

static float g_org_x, g_org_y;

void lab_draw_begin(float units_w, float units_h, int target_w, int target_h) {
  lab_draw_begin_at(0, 0, units_w, units_h, target_w, target_h);
}

/* the target shows units x0 .. x0 + units_w (the iPad board's surface: the
 * game's 320-wide overlay space widened to 3:4, -20 .. 340) */
void lab_draw_begin_at(float x0, float y0, float units_w, float units_h, int target_w, int target_h) {
  if (g_depth++)
    return;
  g_org_x = x0, g_org_y = y0;
  g_units_w = units_w, g_units_h = units_h;
  g_target_w = target_w, g_target_h = target_h;
  g_scale = (float)target_w / units_w;
  if (g_npot < 0) {
    const char *ext = (const char *)lgl.GetString(GL_EXTENSIONS);
    g_npot = ext && (strstr(ext, "GL_OES_texture_npot") || strstr(ext, "GL_ARB_texture_non_power_of_two") ||
                     strstr(ext, "GL_APPLE_texture_2D_limited_npot"));
    debugPrintf("[draw] %s textures\n", g_npot ? "non-power-of-two" : "power-of-two padded");
  }
  while (lgl.GetError() != GL_NO_ERROR) /* the engine's, not ours */
    ;
  lgl.GetIntegerv(GL_ACTIVE_TEXTURE, &S.active);
  lgl.GetIntegerv(GL_MAX_TEXTURE_UNITS, &S.units);
  if (S.units > MAX_UNITS)
    S.units = MAX_UNITS;
  for (int i = 1; i < S.units; i++) {
    lgl.ActiveTexture(GL_TEXTURE0 + i);
    S.unit_tex[i] = lgl.IsEnabled(GL_TEXTURE_2D);
    if (S.unit_tex[i])
      lgl.Disable(GL_TEXTURE_2D);
  }
  lgl.ActiveTexture(GL_TEXTURE0);
  lgl.GetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &S.client);
  for (int i = 1; i < S.units; i++) { /* DrawArrays would read the other units' arrays too */
    lgl.ClientActiveTexture(GL_TEXTURE0 + i);
    S.unit_ta[i] = lgl.IsEnabled(GL_TEXTURE_COORD_ARRAY);
    if (S.unit_ta[i])
      lgl.DisableClientState(GL_TEXTURE_COORD_ARRAY);
  }
  lgl.ClientActiveTexture(GL_TEXTURE0);
  lgl.GetIntegerv(GL_MATRIX_MODE, &S.mode);
  lgl.GetFloatv(GL_PROJECTION_MATRIX, S.proj);
  lgl.GetFloatv(GL_MODELVIEW_MATRIX, S.model);
  lgl.GetFloatv(GL_TEXTURE_MATRIX, S.texm);
  lgl.GetIntegerv(GL_VIEWPORT, S.vp);
  lgl.GetIntegerv(GL_SCISSOR_BOX, S.scissor);
  lgl.GetBooleanv(GL_COLOR_WRITEMASK, S.mask);
  lgl.GetBooleanv(GL_DEPTH_WRITEMASK, &S.depth_mask);
  lgl.GetFloatv(GL_CURRENT_COLOR, S.color);
  lgl.GetIntegerv(GL_TEXTURE_BINDING_2D, &S.bound);
  lgl.GetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &S.env);
  lgl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &S.abuf);
  lgl.GetIntegerv(GL_BLEND_SRC, &S.src);
  lgl.GetIntegerv(GL_BLEND_DST, &S.dst);
  lgl.GetIntegerv(GL_SHADE_MODEL, &S.shade);
  for (unsigned i = 0; i < NOFF; i++)
    S.off_was[i] = lgl.IsEnabled(k_off[i]);
  S.blend = lgl.IsEnabled(GL_BLEND);
  S.tex = lgl.IsEnabled(GL_TEXTURE_2D);
  S.va = lgl.IsEnabled(GL_VERTEX_ARRAY);
  S.ca = lgl.IsEnabled(GL_COLOR_ARRAY);
  S.na = lgl.IsEnabled(GL_NORMAL_ARRAY);
  S.ta = lgl.IsEnabled(GL_TEXTURE_COORD_ARRAY);
  get_array(&S.vtx, GL_VERTEX_ARRAY_SIZE, GL_VERTEX_ARRAY_TYPE, GL_VERTEX_ARRAY_STRIDE,
            GL_VERTEX_ARRAY_BUFFER_BINDING, GL_VERTEX_ARRAY_POINTER);
  get_array(&S.tc, GL_TEXTURE_COORD_ARRAY_SIZE, GL_TEXTURE_COORD_ARRAY_TYPE, GL_TEXTURE_COORD_ARRAY_STRIDE,
            GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING, GL_TEXTURE_COORD_ARRAY_POINTER);
  get_array(&S.col, GL_COLOR_ARRAY_SIZE, GL_COLOR_ARRAY_TYPE, GL_COLOR_ARRAY_STRIDE,
            GL_COLOR_ARRAY_BUFFER_BINDING, GL_COLOR_ARRAY_POINTER);

  for (unsigned i = 0; i < NOFF; i++)
    if (S.off_was[i])
      lgl.Disable(k_off[i]);
  lgl.Enable(GL_BLEND);
  lgl.ColorMask(1, 1, 1, 1);
  lgl.DepthMask(0);
  lgl.ShadeModel(GL_SMOOTH);
  lgl.Viewport(0, 0, target_w, target_h);
  lgl.MatrixMode(GL_TEXTURE);
  lgl.LoadIdentity();
  lgl.MatrixMode(GL_PROJECTION);
  lgl.LoadIdentity();
  lgl.Orthof(g_org_x, g_org_x + units_w, g_org_y + units_h, g_org_y, -1, 1);
  lgl.MatrixMode(GL_MODELVIEW);
  lgl.LoadIdentity();
  lgl.TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  lgl.BindBuffer(GL_ARRAY_BUFFER, 0);
  lgl.EnableClientState(GL_VERTEX_ARRAY);
  lgl.DisableClientState(GL_COLOR_ARRAY);
  lgl.DisableClientState(GL_NORMAL_ARRAY);
}

void lab_draw_end(void) {
  if (!g_depth || --g_depth)
    return;
  put_array(&S.vtx, lgl.VertexPointer);
  put_array(&S.tc, lgl.TexCoordPointer);
  put_array(&S.col, (void (*)(GLint, GLenum, GLsizei, const void *))lgl.ColorPointer);
  lgl.BindBuffer(GL_ARRAY_BUFFER, (GLuint)S.abuf);
  (S.va ? lgl.EnableClientState : lgl.DisableClientState)(GL_VERTEX_ARRAY);
  (S.ca ? lgl.EnableClientState : lgl.DisableClientState)(GL_COLOR_ARRAY);
  (S.na ? lgl.EnableClientState : lgl.DisableClientState)(GL_NORMAL_ARRAY);
  (S.ta ? lgl.EnableClientState : lgl.DisableClientState)(GL_TEXTURE_COORD_ARRAY);
  lgl.MatrixMode(GL_TEXTURE);
  lgl.LoadMatrixf(S.texm);
  lgl.MatrixMode(GL_PROJECTION);
  lgl.LoadMatrixf(S.proj);
  lgl.MatrixMode(GL_MODELVIEW);
  lgl.LoadMatrixf(S.model);
  lgl.MatrixMode((GLenum)S.mode);
  lgl.Viewport(S.vp[0], S.vp[1], S.vp[2], S.vp[3]);
  lgl.Scissor(S.scissor[0], S.scissor[1], S.scissor[2], S.scissor[3]);
  lgl.ColorMask(S.mask[0], S.mask[1], S.mask[2], S.mask[3]);
  lgl.DepthMask(S.depth_mask);
  lgl.Color4f(S.color[0], S.color[1], S.color[2], S.color[3]);
  lgl.BindTexture(GL_TEXTURE_2D, (GLuint)S.bound);
  lgl.TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, S.env);
  lgl.BlendFunc((GLenum)S.src, (GLenum)S.dst);
  lgl.ShadeModel((GLenum)S.shade);
  enable_if(GL_BLEND, S.blend);
  enable_if(GL_TEXTURE_2D, S.tex);
  /* each as it was -- off too: a list's scissor left on when a game starts
   * mid-frame must not outlive the frame (the engine never sets one, and the
   * default box is the 1280x720 window: half the portrait surface) */
  for (unsigned i = 0; i < NOFF; i++)
    enable_if(k_off[i], S.off_was[i]);
  for (int i = 1; i < S.units; i++) {
    if (S.unit_tex[i]) {
      lgl.ActiveTexture(GL_TEXTURE0 + i);
      lgl.Enable(GL_TEXTURE_2D);
    }
    if (S.unit_ta[i]) {
      lgl.ClientActiveTexture(GL_TEXTURE0 + i);
      lgl.EnableClientState(GL_TEXTURE_COORD_ARRAY);
    }
  }
  lgl.ActiveTexture((GLenum)S.active);
  lgl.ClientActiveTexture((GLenum)S.client);
}

/* The engine is called in the middle of a screen's frame (thumbnails, a
 * game starting): its GL state must be its own while it runs, and what it
 * leaves must be what our end puts back. So the drawing is ended around the
 * call, and begun again after it. */
int lab_draw_suspend(void) {
  int d = g_depth;
  if (d) {
    g_depth = 1;
    lab_draw_end();
  }
  return d;
}

void lab_draw_resume(int depth) {
  if (depth) {
    lab_draw_begin(g_units_w, g_units_h, g_target_w, g_target_h);
    g_depth = depth;
  }
}

/* ============================================================== pictures */
static void color_premul(uint32_t rgba) {
  float a = (float)(rgba & 255) / 255.0f;
  lgl.Color4f((float)(rgba >> 24) / 255.0f * a, (float)((rgba >> 16) & 255) / 255.0f * a,
              (float)((rgba >> 8) & 255) / 255.0f * a, a);
}

static void color_straight(uint32_t rgba) {
  lgl.Color4f((float)(rgba >> 24) / 255.0f, (float)((rgba >> 16) & 255) / 255.0f,
              (float)((rgba >> 8) & 255) / 255.0f, (float)(rgba & 255) / 255.0f);
}

/* xy: 4 corners (tl, tr, bl, br) in units; uv likewise */
void lab_draw_quad_raw(const LabTex *t, const float *xy, const float *uv, uint32_t rgba, int blend) {
  if (!t || !t->tex)
    return;
  lgl.Enable(GL_TEXTURE_2D);
  lgl.BindTexture(GL_TEXTURE_2D, t->tex);
  if (blend) {
    lgl.Enable(GL_BLEND);
    lgl.BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  } else {
    lgl.Disable(GL_BLEND);
  }
  color_premul(rgba);
  lgl.EnableClientState(GL_TEXTURE_COORD_ARRAY);
  lgl.VertexPointer(2, GL_FLOAT, 0, xy);
  lgl.TexCoordPointer(2, GL_FLOAT, 0, uv);
  lgl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  lgl.Enable(GL_BLEND);
}

void lab_draw_image_uv(const LabTex *t, float x, float y, float w, float h, float u0, float v0,
                       float u1, float v1, uint32_t rgba) {
  if (!t)
    return;
  const float xy[8] = {x, y, x + w, y, x, y + h, x + w, y + h};
  u0 *= t->tw, u1 *= t->tw, v0 *= t->th, v1 *= t->th;
  const float uv[8] = {u0, v0, u1, v0, u0, v1, u1, v1};
  lab_draw_quad_raw(t, xy, uv, rgba, 1);
}

void lab_draw_image(const LabTex *t, float x, float y, float w, float h, uint32_t rgba) {
  lab_draw_image_uv(t, x, y, w, h, 0, 0, 1, 1, rgba);
}

void lab_draw_image_xform(const LabTex *t, const float m[6], uint32_t rgba) {
  if (!t)
    return;
  const float w = lab_tex_dp_w(t), h = lab_tex_dp_h(t);
  const float c[4][2] = {{0, 0}, {w, 0}, {0, h}, {w, h}};
  float xy[8];
  for (int i = 0; i < 4; i++) {
    xy[i * 2] = m[0] * c[i][0] + m[1] * c[i][1] + m[2];
    xy[i * 2 + 1] = m[3] * c[i][0] + m[4] * c[i][1] + m[5];
  }
  const float uv[8] = {0, 0, t->tw, 0, 0, t->th, t->tw, t->th};
  lab_draw_quad_raw(t, xy, uv, rgba, 1);
}

static void untextured(const float *xy, int n, GLenum mode, uint32_t rgba) {
  lgl.Disable(GL_TEXTURE_2D);
  lgl.DisableClientState(GL_TEXTURE_COORD_ARRAY);
  lgl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  color_straight(rgba);
  lgl.VertexPointer(2, GL_FLOAT, 0, xy);
  lgl.DrawArrays(mode, 0, n);
}

/* The target's alpha made 1 over a rounded rect, its colours left: what is
 * drawn with SRC_ALPHA blending over a see-through target keeps too little
 * alpha where it is soft (text's edges, a sheen), and the local-play
 * composite would show the boards through a panel there. */
void lab_draw_opaque(float x, float y, float w, float h, float r) {
  lgl.ColorMask(0, 0, 0, 1);
  lab_draw_rrect(x, y, w, h, r, 0x000000ffu);
  lgl.ColorMask(1, 1, 1, 1);
}

void lab_draw_rect(float x, float y, float w, float h, uint32_t rgba) {
  const float xy[8] = {x, y, x + w, y, x, y + h, x + w, y + h};
  untextured(xy, 4, GL_TRIANGLE_STRIP, rgba);
}

void lab_draw_line(float x0, float y0, float x1, float y1, float width, uint32_t rgba) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-4f)
    return;
  float nx = -dy / l * width * 0.5f, ny = dx / l * width * 0.5f;
  const float xy[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x0 - nx, y0 - ny, x1 - nx, y1 - ny};
  untextured(xy, 4, GL_TRIANGLE_STRIP, rgba);
}

void lab_draw_frame(float x, float y, float w, float h, float t, uint32_t rgba) {
  lab_draw_rect(x, y, w, t, rgba);
  lab_draw_rect(x, y + h - t, w, t, rgba);
  lab_draw_rect(x, y + t, t, h - 2 * t, rgba);
  lab_draw_rect(x + w - t, y + t, t, h - 2 * t, rgba);
}

/* the contour of a rounded rectangle, n points per corner */
#define CPTS 8
static int rrect_contour(float x, float y, float w, float h, float r, float *out) {
  if (r > w * 0.5f)
    r = w * 0.5f;
  if (r > h * 0.5f)
    r = h * 0.5f;
  const float cx[4] = {x + w - r, x + r, x + r, x + w - r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
  int n = 0;
  for (int c = 0; c < 4; c++)
    for (int i = 0; i < CPTS; i++) {
      float a = ((float)c * 90.0f + (float)i * 90.0f / (float)(CPTS - 1)) * 3.14159265f / 180.0f;
      out[n * 2] = cx[c] + cosf(a) * r;
      out[n * 2 + 1] = cy[c] - sinf(a) * r;
      n++;
    }
  return n;
}

void lab_draw_rrect(float x, float y, float w, float h, float r, uint32_t rgba) {
  float c[4 * CPTS * 2], fan[(4 * CPTS + 2) * 2];
  int n = rrect_contour(x, y, w, h, r, c);
  fan[0] = x + w * 0.5f, fan[1] = y + h * 0.5f;
  memcpy(fan + 2, c, sizeof(float) * 2 * (size_t)n);
  fan[2 + n * 2] = c[0], fan[3 + n * 2] = c[1];
  untextured(fan, n + 2, GL_TRIANGLE_FAN, rgba);
}

void lab_draw_ring(float x, float y, float w, float h, float r, float t, uint32_t rgba) {
  float o[4 * CPTS * 2], in[4 * CPTS * 2], strip[(4 * CPTS + 1) * 4];
  int n = rrect_contour(x - t, y - t, w + 2 * t, h + 2 * t, r + t, o);
  rrect_contour(x, y, w, h, r, in);
  for (int i = 0; i <= n; i++) {
    int k = i % n;
    strip[i * 4] = o[k * 2], strip[i * 4 + 1] = o[k * 2 + 1];
    strip[i * 4 + 2] = in[k * 2], strip[i * 4 + 3] = in[k * 2 + 1];
  }
  untextured(strip, (n + 1) * 2, GL_TRIANGLE_STRIP, rgba);
}

/* a fan from (cx, cy) out to an ellipse: the centre's colour fading to
 * nothing at the rim, added to what is there */
static void glow_fan(float cx, float cy, float rx, float ry, float ang, const uint8_t c[3], float a) {
  enum { N = 48 };
  float xy[(N + 2) * 2];
  GLubyte col[(N + 2) * 4];
  xy[0] = cx, xy[1] = cy;
  col[0] = c[0], col[1] = c[1], col[2] = c[2], col[3] = (GLubyte)(a > 255 ? 255 : a < 0 ? 0 : a);
  float ca = cosf(ang), sa = sinf(ang);
  for (int i = 0; i <= N; i++) {
    float t = (float)i / (float)N * 6.2831853f, ex = rx * cosf(t), ey = ry * sinf(t);
    xy[2 + i * 2] = cx + ex * ca - ey * sa, xy[3 + i * 2] = cy + ex * sa + ey * ca;
    GLubyte *o = col + 4 + i * 4;
    o[0] = c[0], o[1] = c[1], o[2] = c[2], o[3] = 0;
  }
  lgl.Disable(GL_TEXTURE_2D);
  lgl.DisableClientState(GL_TEXTURE_COORD_ARRAY);
  lgl.BlendFunc(GL_SRC_ALPHA, GL_ONE);
  lgl.EnableClientState(GL_COLOR_ARRAY);
  lgl.ColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
  lgl.VertexPointer(2, GL_FLOAT, 0, xy);
  lgl.DrawArrays(GL_TRIANGLE_FAN, 0, N + 2);
  lgl.DisableClientState(GL_COLOR_ARRAY);
  lgl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* pvztouch's Zombatar highlight: a warm halo fading outwards, and the
 * element itself lit -- steady, soft (the halo's centre adds about a
 * sixth of white) */
void lab_draw_glow_at(float cx, float cy, float rx, float ry, float deg, float k) {
  static const uint8_t halo[3] = {255, 226, 120}, lit[3] = {255, 240, 190};
  float a = deg * 3.14159265f / 180.0f, grow = 12.0f * (k > 1 ? k : 1);
  glow_fan(cx, cy, rx + grow, ry + grow, a, halo, 44.0f * k);
  glow_fan(cx, cy, rx * 0.85f, ry * 0.85f, a, lit, 20.0f * k);
}

void lab_draw_glow(float x, float y, float w, float h) {
  float r = (w > h ? w : h) * 0.5f;
  lab_draw_glow_at(x + w * 0.5f, y + h * 0.5f, r, r, 0, 1.0f);
}

void lab_draw_glow_behind(float x, float y, float w, float h) {
  /* behind a picture its middle is hidden: a wider, brighter halo shows */
  float r = (w > h ? w : h) * 0.5f;
  lab_draw_glow_at(x + w * 0.5f, y + h * 0.5f, r * 0.8f, r * 0.8f, 0, 2.6f);
}

void lab_draw_scissor(float x, float y, float w, float h) {
  if (w <= 0 || h <= 0) {
    lgl.Disable(GL_SCISSOR_TEST);
    return;
  }
  int sx = (int)floorf((x - g_org_x) * g_scale), sy = (int)floorf((y - g_org_y) * g_scale);
  int sw = (int)ceilf(w * g_scale), sh = (int)ceilf(h * g_scale);
  lgl.Enable(GL_SCISSOR_TEST);
  lgl.Scissor(sx, g_target_h - sy - sh, sw, sh);
}

/* ============================================================ textures */
static int pot(int v) {
  int p = 1;
  while (p < v)
    p <<= 1;
  return p;
}

LabTex *lab_tex_from_rgba(const uint8_t *rgba, int w, int h) {
  if (!rgba || w <= 0 || h <= 0)
    return NULL;
  LabTex *t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  int tw = g_npot > 0 ? w : pot(w), th = g_npot > 0 ? h : pot(h);
  /* premultiply (a copy, padded with the edge pixels when not NPOT) */
  uint8_t *p = malloc((size_t)tw * th * 4);
  if (!p) {
    free(t);
    return NULL;
  }
  for (int y = 0; y < th; y++) {
    int sy = y < h ? y : h - 1;
    for (int x = 0; x < tw; x++) {
      int sx = x < w ? x : w - 1;
      const uint8_t *s = rgba + ((size_t)sy * w + sx) * 4;
      uint8_t *d = p + ((size_t)y * tw + x) * 4;
      unsigned a = s[3];
      d[0] = (uint8_t)((s[0] * a + 127) / 255);
      d[1] = (uint8_t)((s[1] * a + 127) / 255);
      d[2] = (uint8_t)((s[2] * a + 127) / 255);
      d[3] = (uint8_t)a;
    }
  }
  GLint prev = 0, align = 4;
  lgl.GetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
  lgl.GetIntegerv(GL_UNPACK_ALIGNMENT, &align);
  lgl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  lgl.GenTextures(1, &t->tex);
  lgl.BindTexture(GL_TEXTURE_2D, t->tex);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  lgl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, p);
  lgl.PixelStorei(GL_UNPACK_ALIGNMENT, align);
  lgl.BindTexture(GL_TEXTURE_2D, (GLuint)prev);
  free(p);
  t->w = w, t->h = h;
  t->tw = (float)w / (float)tw, t->th = (float)h / (float)th;
  return t;
}

void lab_tex_free(LabTex *t) {
  if (!t)
    return;
  if (t->tex)
    lgl.DeleteTextures(1, &t->tex);
  free(t);
}

float lab_tex_dp_w(const LabTex *t) { return t ? (float)t->w / (t->scale > 0 ? t->scale : 1.5f) : 0; }
float lab_tex_dp_h(const LabTex *t) { return t ? (float)t->h / (t->scale > 0 ? t->scale : 1.5f) : 0; }
float lab_tex_pt_w(const LabTex *t) { return lab_tex_dp_w(t); }
float lab_tex_pt_h(const LabTex *t) { return lab_tex_dp_h(t); }

static int g_prefer_2x = 1;
void lab_tex_prefer_2x(int on) { g_prefer_2x = on; }

/* "ipa:menugraphics/tab_ipad": the .ipa's picture, @2x or not; *scale 2 or 1 */
static uint8_t *ipa_picture(const char *path, size_t *len, float *scale) {
  static const char *const exts[] = {".png", ".jpg", "~ipad.png", "~ipad.jpg", ".pvr"};
  char nm[160];
  for (int pass = 0; pass < 2; pass++) {
    int two = (pass == 0) == (g_prefer_2x != 0);
    for (unsigned x = 0; x < 5; x++) {
      if (two && x >= 2 && x < 4)
        snprintf(nm, sizeof nm, "%s@2x%s", path, exts[x]);
      else
        snprintf(nm, sizeof nm, "%s%s%s", path, two ? "@2x" : "", exts[x]);
      uint8_t *d = lab_ipa_read(nm, len);
      if (d) {
        *scale = two ? 2.0f : 1.0f;
        return d;
      }
    }
  }
  return NULL;
}

typedef struct {
  char name[96];
  LabTex *t;
  int failed;
} TexEntry;
#define NTEX 640
static TexEntry g_tex[NTEX];
static int g_ntex;

const LabTex *lab_tex(const char *name) {
  for (int i = 0; i < g_ntex; i++)
    if (!strcmp(g_tex[i].name, name))
      return g_tex[i].t;
  if (g_ntex >= NTEX)
    return NULL;
  TexEntry *e = &g_tex[g_ntex++];
  snprintf(e->name, sizeof e->name, "%s", name);
  char path[160];
  size_t len = 0;
  uint8_t *data = NULL;
  float scale = 0;
  if (!strncmp(name, "ipa:", 4)) {
    data = ipa_picture(name + 4, &len, &scale);
  } else if (strchr(name, '/')) {
    data = lab_apk_read(name, &len);
  } else {
    static const char *const dirs[] = {"drawable-hdpi-v4", "drawable-mdpi-v4", "drawable-xhdpi-v4"};
    static const char *const exts[] = {".png", ".jpg"};
    for (unsigned d = 0; d < 3 && !data; d++)
      for (unsigned x = 0; x < 2 && !data; x++) {
        snprintf(path, sizeof path, "res/%s/%s%s", dirs[d], name, exts[x]);
        data = lab_apk_read(path, &len);
      }
  }
  if (!data) {
    debugPrintf("[draw] %s: not in the APK\n", name);
    e->failed = 1;
    return NULL;
  }
  int w = 0, h = 0;
  uint8_t *px = lab_image_decode(data, len, &w, &h);
  free(data);
  if (!px) {
    debugPrintf("[draw] %s: cannot decode (%s)\n", name, stbi_failure_reason());
    e->failed = 1;
    return NULL;
  }
  e->t = lab_tex_from_rgba(px, w, h);
  stbi_image_free(px);
  if (e->t)
    e->t->scale = scale;
  return e->t;
}

/* ================================================================= text */
#define MAX_EMO 12
typedef struct {
  char *s;
  int px, bold;
  GLuint tex;
  int w, h, tw, th;  /* bitmap and texture sizes */
  float asc;         /* baseline from the top, pixels */
  uint64_t used;
  int nemo;          /* emoji in it: drawn over their gaps */
  float emo_x[MAX_EMO];
  int emo_g[MAX_EMO];
} TextEntry;
#define NTEXT 384
static TextEntry g_text[NTEXT];
static uint64_t g_text_clock;

static TextEntry *text_get(const char *s, int px, int bold) {
  TextEntry *free_e = NULL, *old = NULL;
  for (int i = 0; i < NTEXT; i++) {
    TextEntry *e = &g_text[i];
    if (!e->s) {
      if (!free_e)
        free_e = e;
      continue;
    }
    if (e->px == px && e->bold == bold && !strcmp(e->s, s)) {
      e->used = g_text_clock;
      return e;
    }
    if (!old || e->used < old->used)
      old = e;
  }
  TextEntry *e = free_e ? free_e : old;
  if (e->s) {
    free(e->s);
    lgl.DeleteTextures(1, &e->tex);
    e->s = NULL;
  }
  float fpx = (float)px;
  float asc = lab_font_ascent(fpx), desc = lab_font_descent(fpx);
  int w = (int)ceilf(lab_font_width(fpx, s, bold)) + 4, h = (int)ceilf(asc + desc) + 4;
  int tw = g_npot > 0 ? w : pot(w), th = g_npot > 0 ? h : pot(h);
  uint8_t *a = calloc((size_t)tw * th, 1);
  if (!a)
    return NULL;
  lab_font_draw(a, tw, th, tw, 2.0f, 2.0f + asc, fpx, s, bold);
  GLint prev = 0, align = 4;
  lgl.GetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
  lgl.GetIntegerv(GL_UNPACK_ALIGNMENT, &align);
  lgl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  lgl.GenTextures(1, &e->tex);
  lgl.BindTexture(GL_TEXTURE_2D, e->tex);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  lgl.TexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, tw, th, 0, GL_ALPHA, GL_UNSIGNED_BYTE, a);
  lgl.PixelStorei(GL_UNPACK_ALIGNMENT, align);
  lgl.BindTexture(GL_TEXTURE_2D, (GLuint)prev);
  free(a);
  e->s = strdup(s);
  e->px = px, e->bold = bold;
  e->w = w, e->h = h, e->tw = tw, e->th = th;
  e->asc = 2.0f + asc;
  e->used = g_text_clock;
  e->nemo = lab_font_emoji_bold(fpx, s, bold, e->emo_x, e->emo_g, MAX_EMO);
  return e;
}

/* ---- the emoji's pictures (lab_emoji.c), kept as textures */
typedef struct {
  int g;
  LabTex *t;
  float left, top, w, h; /* em */
  uint64_t used;
} EmojiTex;
#define NEMOJI 128
static EmojiTex g_emo[NEMOJI];

/* halved (2x2 averages, alpha-weighted) while it is much bigger than drawn */
static uint8_t *shrink(uint8_t *px, int *w, int *h, int want) {
  while (*w >= 2 * want && *h >= 2 * want) {
    int nw = *w / 2, nh = *h / 2;
    uint8_t *o = malloc((size_t)nw * (size_t)nh * 4);
    if (!o)
      break;
    for (int y = 0; y < nh; y++)
      for (int x = 0; x < nw; x++) {
        unsigned s[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; k++) {
          const uint8_t *q = px + (((size_t)(2 * y + k / 2) * (size_t)*w) + (size_t)(2 * x + k % 2)) * 4;
          s[0] += q[0] * q[3], s[1] += q[1] * q[3], s[2] += q[2] * q[3], s[3] += q[3];
        }
        uint8_t *d = o + ((size_t)y * (size_t)nw + (size_t)x) * 4;
        for (int c = 0; c < 3; c++)
          d[c] = (uint8_t)(s[3] ? s[c] / s[3] : 0);
        d[3] = (uint8_t)(s[3] / 4);
      }
    free(px);
    px = o;
    *w = nw, *h = nh;
  }
  return px;
}

static const EmojiTex *emoji_tex(int g) {
  EmojiTex *free_e = NULL, *old = NULL;
  for (int i = 0; i < NEMOJI; i++) {
    EmojiTex *e = &g_emo[i];
    if (!e->g) {
      if (!free_e)
        free_e = e;
      continue;
    }
    if (e->g == g) {
      e->used = g_text_clock;
      return e->t ? e : NULL;
    }
    if (!old || e->used < old->used)
      old = e;
  }
  EmojiTex *e = free_e ? free_e : old;
  lab_tex_free(e->t);
  memset(e, 0, sizeof *e);
  e->g = g;
  e->used = g_text_clock;
  int w, h;
  float left, top, pxem;
  uint8_t *px = lab_emoji_rgba(g, &w, &h, &left, &top, &pxem);
  if (!px)
    return NULL; /* remembered as none */
  int w0 = w;
  px = shrink(px, &w, &h, 72);
  e->t = lab_tex_from_rgba(px, w, h);
  free(px);
  float k = (float)w0 / (float)w; /* shrunk: each pixel covers more em */
  e->left = left, e->top = top, e->w = (float)w * k * pxem, e->h = (float)h * k * pxem;
  return e->t ? e : NULL;
}

float lab_text_width(float size, int bold, const char *utf8) {
  if (!utf8 || !*utf8)
    return 0;
  float px = size * g_scale;
  return lab_font_width(px, utf8, bold) / g_scale;
}

float lab_draw_text(float x, float y_baseline, float size, uint32_t rgba, int align, int bold,
                    const char *utf8) {
  if (!utf8 || !*utf8 || !(rgba & 255))
    return 0;
  g_text_clock++;
  int px = (int)lroundf(size * g_scale);
  if (px < 4)
    px = 4;
  TextEntry *e = text_get(utf8, px, bold);
  if (!e)
    return 0;
  float w = (float)e->w / g_scale, h = (float)e->h / g_scale;
  float tw_units = (float)(e->w - 4) / g_scale;
  float x0 = align == LAB_CENTER ? x - tw_units * 0.5f : align == LAB_RIGHT ? x - tw_units : x;
  x0 -= 2.0f / g_scale;
  float y0 = y_baseline - e->asc / g_scale;
  /* snap to target pixels: text stays crisp */
  x0 = floorf(x0 * g_scale + 0.5f) / g_scale;
  y0 = floorf(y0 * g_scale + 0.5f) / g_scale;
  const float xy[8] = {x0, y0, x0 + w, y0, x0, y0 + h, x0 + w, y0 + h};
  float u = (float)e->w / (float)e->tw, v = (float)e->h / (float)e->th;
  const float uv[8] = {0, 0, u, 0, 0, v, u, v};
  lgl.Enable(GL_TEXTURE_2D);
  lgl.BindTexture(GL_TEXTURE_2D, e->tex);
  lgl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  color_straight(rgba);
  lgl.EnableClientState(GL_TEXTURE_COORD_ARRAY);
  lgl.VertexPointer(2, GL_FLOAT, 0, xy);
  lgl.TexCoordPointer(2, GL_FLOAT, 0, uv);
  lgl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  /* the emoji, in their own colours (the text's alpha) over their gaps */
  float em = (float)px / g_scale;
  for (int i = 0; i < e->nemo; i++) {
    const EmojiTex *m = emoji_tex(e->emo_g[i]);
    if (m)
      lab_draw_image(m->t, x0 + (2.0f + e->emo_x[i]) / g_scale + m->left * em, y_baseline - m->top * em,
                     m->w * em, m->h * em, 0xffffff00u | (rgba & 255));
  }
  return tw_units;
}

float lab_draw_text_box(float x, float y_top, float w, float size, uint32_t rgba, int align, int bold,
                        const char *utf8) {
  if (!utf8)
    return 0;
  int starts[64], lens[64];
  float px = size * g_scale;
  int n = lab_text_wrap(utf8, px, bold, w * g_scale, starts, lens, 64);
  float line = size * 1.25f, asc = lab_font_ascent(px) / g_scale;
  char buf[512];
  for (int i = 0; i < n; i++) {
    int l = lens[i] < (int)sizeof buf - 1 ? lens[i] : (int)sizeof buf - 1;
    memcpy(buf, utf8 + starts[i], (size_t)l);
    buf[l] = 0;
    float ax = align == LAB_CENTER ? x + w * 0.5f : align == LAB_RIGHT ? x + w : x;
    lab_draw_text(ax, y_top + asc + line * (float)i, size, rgba, align, bold, buf);
  }
  return line * (float)n;
}
