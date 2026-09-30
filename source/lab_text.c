/* lab_text.c -- se.illusionlabs.common.ZFont.renderTextToTexture.
 *
 * The engine's text (the level's name, "Congratulations! You got the award",
 * the times on the scoreboard...) is drawn by Java: ZFont paints the string
 * with a TextPaint into a shared 1024x1024 ALPHA_8 bitmap through a
 * StaticLayout, uploads it with GLUtils.texImage2D (GL_ALPHA) into the
 * engine's texture (or a new one), and leaves four fields for the engine:
 *   textWidth, textHeight        the layout's box: width = min(maxWidth, the
 *                                widest line), height = lines x line height
 *   textureWidth, textureHeight  the texture uploaded (the size asked for,
 *                                or the whole 1024x1024 bitmap)
 * StaticLayout's line height with spacing 1.3 is 1.3 x (descent - ascent);
 * the text wraps at spaces. Font names are iOS's ("HelveticaNeue",
 * "HelveticaNeue-Bold"): the Switch's standard font, bold for "-Bold".
 * Alignment 0 left, 1 centre, 2 right (ax.java), within the layout's width.
 * MIT.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "lab.h"
#include "lab_gl.h"
#include "util.h"

#define BITMAP 1024

/* word wrap: [start, len) of each line */
int lab_text_wrap(const char *text, float px, int bold, float max_w, int *starts, int *lens, int cap) {
  int n = 0;
  if (!text)
    return 0;
  const char *p = text;
  char buf[1024];
  while (*p && n < cap) {
    /* one paragraph: up to '\n' */
    const char *e = strchr(p, '\n');
    size_t plen = e ? (size_t)(e - p) : strlen(p);
    size_t pos = 0;
    if (plen == 0) {
      starts[n] = (int)(p - text), lens[n] = 0, n++;
    }
    while (pos < plen && n < cap) {
      /* the longest run from pos that fits, broken after a space */
      size_t best = 0, i = pos;
      while (i < plen) {
        size_t j = i;
        while (j < plen && p[j] != ' ')
          j++;
        size_t cand = j - pos;
        if (cand >= sizeof buf)
          cand = sizeof buf - 1;
        memcpy(buf, p + pos, cand);
        buf[cand] = 0;
        if (lab_font_width(px, buf, bold) > max_w && best)
          break;
        best = j - pos;
        if (lab_font_width(px, buf, bold) > max_w) {
          /* one word wider than the line: break it by characters */
          size_t k = 1;
          while (k < cand && ((unsigned char)p[pos + k] & 0xC0) == 0x80)
            k++; /* at least the first whole character */
          while (k < cand) {
            size_t kk = k + 1;
            while (kk < cand && ((unsigned char)p[pos + kk] & 0xC0) == 0x80)
              kk++;
            memcpy(buf, p + pos, kk);
            buf[kk] = 0;
            if (lab_font_width(px, buf, bold) > max_w)
              break;
            k = kk;
          }
          best = k;
          break;
        }
        i = j + 1;
      }
      if (!best)
        best = plen - pos;
      starts[n] = (int)(p - text + (long)pos);
      lens[n] = (int)best;
      n++;
      pos += best;
      while (pos < plen && p[pos] == ' ')
        pos++;
    }
    if (!e)
      break;
    p = e + 1;
    if (!*p && n < cap) /* a trailing newline makes an empty last line */
      starts[n] = (int)(p - text), lens[n] = 0, n++;
  }
  return n;
}

int lab_text_render(JObj *zfont, const char *text, const char *font, int size, int align, int max_w,
                    int unknown, int tex, int tex_w, int tex_h) {
  if (!text)
    text = "";
  int bold = font && strstr(font, "Bold") != NULL;
  float px = (float)(size > 0 ? size : 12);
  /* StaticLayout.getDesiredWidth: the widest paragraph, unwrapped */
  float desired = 0;
  {
    const char *p = text;
    char buf[1024];
    for (;;) {
      const char *e = strchr(p, '\n');
      size_t l = e ? (size_t)(e - p) : strlen(p);
      if (l >= sizeof buf)
        l = sizeof buf - 1;
      memcpy(buf, p, l);
      buf[l] = 0;
      float w = lab_font_width(px, buf, bold);
      if (w > desired)
        desired = w;
      if (!e)
        break;
      p = e + 1;
    }
  }
  /* rounded up: a word measured 116.4 px in a 116 px box went onto a
   * second line, out of the texture the engine asked for ("Tutoria",
   * hardware 2026-09-26); what fits unwrapped is not wrapped */
  int fits = desired <= (float)max_w;
  int layout_w = fits ? (int)ceilf(desired) : max_w;
  if (layout_w < 1)
    layout_w = 1;
  int starts[64], lens[64];
  int n = lab_text_wrap(text, px, bold, fits ? desired + 1.0f : (float)layout_w + 0.5f, starts, lens, 64);
  float asc = lab_font_ascent(px), desc = lab_font_descent(px);
  int line_h = (int)lroundf((asc + desc) * 1.3f);
  int text_h = line_h * (n ? n : 1);

  int bw = tex_w > 0 && tex_w < BITMAP ? tex_w : BITMAP, bh = tex_h > 0 && tex_h < BITMAP ? tex_h : BITMAP;
  uint8_t *a = calloc((size_t)bw * bh, 1);
  if (!a)
    return 0;
  char buf[1024];
  for (int i = 0; i < n; i++) {
    int l = lens[i] < (int)sizeof buf - 1 ? lens[i] : (int)sizeof buf - 1;
    memcpy(buf, text + starts[i], (size_t)l);
    buf[l] = 0;
    float lw = lab_font_width(px, buf, bold);
    float x = align == 1 ? ((float)layout_w - lw) * 0.5f : align == 2 ? (float)layout_w - lw : 0.0f;
    float base = (float)(i * line_h) + asc;
    lab_font_draw(a, bw, bh, bw, x, base, px, buf, bold);
  }

  GLuint t = (GLuint)tex;
  if (!t)
    lgl.GenTextures(1, &t);
  GLint prev = 0, align_px = 4;
  lgl.GetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
  lgl.GetIntegerv(GL_UNPACK_ALIGNMENT, &align_px);
  lgl.BindTexture(GL_TEXTURE_2D, t);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  lgl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  lgl.TexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, bw, bh, 0, GL_ALPHA, GL_UNSIGNED_BYTE, a);
  lgl.PixelStorei(GL_UNPACK_ALIGNMENT, align_px);
  /* GLUtils.texImage2D leaves the texture bound */
  (void)prev;
  free(a);

  jni_set_field(zfont, "textWidth", jv_i(layout_w), 0);
  jni_set_field(zfont, "textHeight", jv_i(text_h), 0);
  jni_set_field(zfont, "textureWidth", jv_i(bw), 0);
  jni_set_field(zfont, "textureHeight", jv_i(bh), 0);
  static int logged;
  if (logged++ < 40)
    debugPrintf("[text] \"%.40s\" %s %dpx align %d max %d -> %dx%d in tex %u (%dx%d)\n", text,
                font ? font : "?", size, align, max_w, layout_w, text_h, (unsigned)t, bw, bh);
  return (int)t;
}
