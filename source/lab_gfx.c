/* lab_gfx.c -- the screen: EGL, the portrait surface, the composite.
 *
 * Labyrinth 2 is a portrait phone game: on Android its GLSurfaceView (ES 1,
 * the default EGL config) filled a portrait screen, and the engine drew the
 * 320x480 board to "the screen" (framebuffer 0) at whatever size resize()
 * gave it. Here the engine and the rebuilt menus draw into a PORTRAIT
 * SURFACE: an FBO of 2:3 (the board's shape), 480x720 at 720p, twice that
 * with supersampling. Each frame it is then put on the landscape window:
 *   portrait       upright in the middle, the side panels (lab_ui.c)
 *                  beside it;
 *   rotated_left   turned a quarter to fill the screen, 1080x720, for a
 *   rotated_right  console held upright (its left / right side at the top).
 * With the portrait layout, a level being played is turned too
 * ([display] level_layout, ZR in a level): the board fills the screen, the
 * controller held as usual (lab_gfx_view_to_board turns the stick's and the
 * motion's directions to the board's); the menus, and the game's own
 * screens over a level, stay upright. The turn is animated (lab_gfx_set_rotation).
 *
 * The engine binds framebuffer 0 whenever it means the screen (after an
 * off-screen pass, and for thumbnails the pbuffer was current instead), so
 * glBindFramebufferOES is imported through lab_gl_bind_framebuffer: 0 means
 * the current target (the portrait surface, or a thumbnail's texture), and
 * glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES) answers 0 for it again. MIT.
 */
#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "gl_layer.h"
#include "lab.h"
#include "lab_gl.h"
#include "so_util.h"
#include "util.h"

const char *dcr_game_root(void); /* the runtime (dcr_path.c) */

#if DCR_GL_MESA
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

void dcr_window_prepare(void);           /* android_ndk.c */
void dcr_window_size(int *w, int *h);    /* android_ndk.c */

LabGL lgl;

int lab_gl_load(void) {
  int missing = 0;
#define LAB_GL_LOAD(n)                                                  \
  if (!(lgl.n = (void *)dcr_gl_lookup("gl" #n))) {                      \
    debugPrintf("[gfx] the GL driver has no gl" #n "\n");                \
    missing++;                                                          \
  }
  LAB_GL_FUNCS(LAB_GL_LOAD)
#undef LAB_GL_LOAD
  return missing ? -1 : 0;
}

static int g_win_w = 1280, g_win_h = 720;
static float g_px, g_py, g_pw, g_ph; /* the picture's box in the window (where it is going) */
static int g_rot;                     /* 0, 90 (top to the left), 270 (top to the right) */
static int g_base_rot;                /* the layout's (a console held upright): always */
/* the picture as drawn: its size along its own width and height (window
 * pixels) and its angle, going from `from` to `to` */
static float g_show[3], g_from[3], g_to[3];
static u64 g_anim_t0;
static int g_anim;
static int g_sw = 480, g_sh = 720;    /* the portrait surface, pixels */
static GLuint g_fbo, g_tex, g_depth;
static GLuint g_target;               /* what framebuffer 0 means for the engine now */
/* the iPad board's surface (3:4), and which picture is shown: its aspect */
static GLuint g_ifbo, g_itex, g_idepth;
static int g_iw, g_ih, g_ipic;
static int g_aw = 2, g_ah = 3;
/* the iPad menus' surface: landscape, the window's size (lab_hd*.c draw into
 * it in iPad points, 768 tall), and whether it is what is shown */
static GLuint g_hfbo, g_htex;
static int g_hw, g_hh, g_hshow;
static int g_ok;
void (*lab_gfx_side_hook)(int win_w, int win_h, float px, float py, float pw, float ph);

int lab_gfx_surface_w(void) { return g_sw; }
int lab_gfx_surface_h(void) { return g_sh; }
float lab_gfx_px_per_dp(void) { return (float)g_sw / 320.0f; }

void lab_gfx_picture_rect(float *x, float *y, float *w, float *h, int *rot) {
  *x = g_px, *y = g_py, *w = g_pw, *h = g_ph, *rot = g_rot;
}

/* Where the picture goes for a rotation: its box on the window, and its
 * own width / height there (the board's x and y extents). */
static void place(int rot, float *px, float *py, float *pw, float *ph, float *bw, float *bh) {
  int w, h; /* the board's width and height in window pixels */
  if (!rot) {
    h = g_win_h;
    w = h * g_aw / g_ah;
    *pw = (float)w, *ph = (float)h;
  } else {
    w = g_win_h;             /* its width along the window's height */
    h = w * g_ah / g_aw;     /* its height along the window's width */
    if (h > g_win_w) {
      h = g_win_w;
      w = h * g_aw / g_ah;
    }
    *pw = (float)h, *ph = (float)w;
  }
  *bw = (float)w, *bh = (float)h;
  *px = ((float)g_win_w - *pw) * 0.5f;
  *py = ((float)g_win_h - *ph) * 0.5f;
}

/* the angle it is drawn at (window pixels, y down): its top to the left is
 * a quarter turn anticlockwise */
static float angle_of(int rot) { return rot == 90 ? -90.0f : rot == 270 ? 90.0f : 0.0f; }

void lab_gfx_set_rotation(int rot) {
  if (g_base_rot)
    rot = g_base_rot; /* the layout turns everything already */
  if (rot == g_rot)
    return;
  g_rot = rot;
  memcpy(g_from, g_show, sizeof g_from);
  place(rot, &g_px, &g_py, &g_pw, &g_ph, &g_to[0], &g_to[1]);
  g_to[2] = angle_of(rot);
  g_anim_t0 = armGetSystemTick();
  g_anim = 1;
}

/* straight there, no turning (a level starting: its Loading... is shown
 * for the second the level takes to load, as the level will be) */
void lab_gfx_set_rotation_now(int rot) {
  lab_gfx_set_rotation(rot);
  if (g_anim) {
    memcpy(g_show, g_to, sizeof g_show);
    g_anim = 0;
  }
}

int lab_gfx_rotation(void) { return g_rot; }

int lab_gfx_upright(void) { return g_anim ? -1 : g_rot == 0; }

/* a level is shown turned (its top to the left): [display] level_layout */
int lab_gfx_level_turned(void) { return !g_base_rot && dcr_config()->level_layout != LAB_LAYOUT_PORTRAIT; }

void lab_gfx_view_to_board(float *x, float *y) {
  if (g_base_rot)
    return; /* the console is turned with the picture: lab_input.c's to_viewer */
  float vx = *x, vy = *y;
  if (g_rot == 90) { /* the board's right is up, its top to the left */
    *x = vy;
    *y = -vx;
  } else if (g_rot == 270) {
    *x = -vy;
    *y = vx;
  }
}

static void animate(void) {
  if (!g_anim)
    return;
  float t = (float)armTicksToNs(armGetSystemTick() - g_anim_t0) * 1e-9f / 0.30f;
  if (t >= 1.0f) {
    t = 1.0f;
    g_anim = 0;
  }
  float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); /* ease out */
  for (int i = 0; i < 3; i++)
    g_show[i] = g_from[i] + (g_to[i] - g_from[i]) * e;
}

int lab_gfx_touch_to_dp(float sx, float sy, float *dx, float *dy) {
  /* the touch screen reports 1280x720 whatever the render size */
  float wx = sx * (float)g_win_w / 1280.0f, wy = sy * (float)g_win_h / 720.0f;
  float a = (wx - g_px) / g_pw, b = (wy - g_py) / g_ph, u, v;
  if (g_rot == 90)
    u = 1.0f - b, v = a;
  else if (g_rot == 270)
    u = b, v = 1.0f - a;
  else
    u = a, v = b;
  if (g_ipic)
    *dx = u * 360.0f - 20.0f; /* the iPad board's surface: the overlay space widened */
  else
    *dx = u * 320.0f;
  *dy = v * 480.0f;
  return u >= 0 && u <= 1 && v >= 0 && v <= 1;
}

/* ------------------------------------------------------------ FBOs */
static int make_target(GLuint *fbo, GLuint *tex, GLuint *depth, int w, int h, const char *what) {
  lgl.GenTextures(1, tex);
  lgl.BindTexture(GL_TEXTURE_2D, *tex);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  lgl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  lgl.BindTexture(GL_TEXTURE_2D, 0);
  lgl.GenFramebuffersOES(1, fbo);
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, *fbo);
  lgl.FramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, *tex, 0);
  if (depth) {
    lgl.GenRenderbuffersOES(1, depth);
    lgl.BindRenderbufferOES(GL_RENDERBUFFER_OES, *depth);
    while (lgl.GetError() != GL_NO_ERROR)
      ;
    lgl.RenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_DEPTH_COMPONENT24_OES, w, h);
    if (lgl.GetError() != GL_NO_ERROR)
      lgl.RenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_DEPTH_COMPONENT16_OES, w, h);
    lgl.FramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, *depth);
    lgl.BindRenderbufferOES(GL_RENDERBUFFER_OES, 0);
  }
  GLenum st = lgl.CheckFramebufferStatusOES(GL_FRAMEBUFFER_OES);
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, 0);
  if (st != GL_FRAMEBUFFER_COMPLETE_OES) {
    debugPrintf("[gfx] %s %dx%d: framebuffer incomplete (0x%x)\n", what, w, h, st);
    return -1;
  }
  return 0;
}

/* ------------------------------------------------------------ the iPad board
 * Made the first time an iPad pack is played: 3:4, as big as it is shown
 * (turned: 960 x 720 at 720p), with the portrait surface's supersampling. */
static int ipad_make(void) {
  const DcrConfig *c = dcr_config();
  int ss = c->supersample > 1 ? 2 : 1;
  for (; ss >= 1; ss--) {
    g_iw = g_win_h * ss;
    g_ih = g_iw * 4 / 3;
    if (g_ih > 2048)
      g_ih = 2048, g_iw = g_ih * 3 / 4;
    if (make_target(&g_ifbo, &g_itex, &g_idepth, g_iw, g_ih, "iPad board surface") == 0) {
      debugPrintf("[gfx] the iPad board: %dx%d\n", g_iw, g_ih);
      return 0;
    }
  }
  g_ifbo = 0;
  return -1;
}

int lab_gfx_ipad_w(void) { return g_iw ? g_iw : g_sw; }
int lab_gfx_ipad_h(void) { return g_ih ? g_ih : g_sh; }

void lab_gfx_begin_ipad(void) {
  if (!g_ifbo && ipad_make() != 0) {
    lab_gfx_begin_portrait();
    return;
  }
  g_target = g_ifbo;
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_ifbo);
  lgl.Viewport(0, 0, g_iw, g_ih);
}

/* the picture shown: the portrait surface (2:3) or the iPad board's (3:4) */
void lab_gfx_show_ipad(int on) {
  on = on && g_ifbo;
  if (on == g_ipic)
    return;
  g_ipic = on;
  g_aw = on ? 3 : 2, g_ah = on ? 4 : 3;
  place(g_rot, &g_px, &g_py, &g_pw, &g_ph, &g_to[0], &g_to[1]);
  g_to[2] = angle_of(g_rot);
  memcpy(g_show, g_to, sizeof g_show); /* no animation between the two */
  g_anim = 0;
}

int lab_gfx_showing_ipad(void) { return g_ipic; }

/* ------------------------------------------------------------ the iPad menus */
int lab_gfx_hd_w(void) { return g_hw ? g_hw : g_win_w; }
int lab_gfx_hd_h(void) { return g_hh ? g_hh : g_win_h; }
static float g_hch = 640; /* the canvas's height, points (lab_ui.c sets it) */
void lab_gfx_set_hd_canvas_h(float h) { g_hch = h; }
float lab_gfx_hd_canvas_h(void) { return g_hch; }
float lab_gfx_hd_canvas_w(void) { return g_hch * (float)g_win_w / (float)g_win_h; }

void lab_gfx_begin_hd(void) {
  if (!g_hfbo) {
    g_hw = g_win_w, g_hh = g_win_h;
    if (make_target(&g_hfbo, &g_htex, NULL, g_hw, g_hh, "iPad menus surface") != 0) {
      g_hfbo = 0;
      lab_gfx_begin_portrait();
      return;
    }
    debugPrintf("[gfx] the iPad menus: %dx%d (%.0f x %.0f points)\n", g_hw, g_hh, (double)lab_gfx_hd_canvas_w(),
                (double)g_hch);
  }
  g_target = g_hfbo;
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_hfbo);
  lgl.Viewport(0, 0, g_hw, g_hh);
}

void lab_gfx_show_hd(int on) { g_hshow = on && g_hfbo; }
/* the iPad menus' surface over the game's picture (see-through where
 * nothing is drawn): Settings over a level */
static int g_hover;
void lab_gfx_show_hd_over(int on) { g_hover = on; }
int lab_gfx_showing_hd(void) { return g_hshow; }

/* a touch (1280 x 720) on the iPad menus: points */
void lab_gfx_touch_to_canvas(float sx, float sy, float *x, float *y) {
  *x = sx / 1280.0f * lab_gfx_hd_canvas_w();
  *y = sy / 720.0f * g_hch;
}

static void hd_quad(int blend) {
  const float xy[8] = {0, 0, (float)g_win_w, 0, 0, (float)g_win_h, (float)g_win_w, (float)g_win_h};
  const float uv[8] = {0, 1, 1, 1, 0, 0, 1, 0};
  LabTex t = {g_htex, g_hw, g_hh, 1.0f, 1.0f};
  lab_draw_quad_raw(&t, xy, uv, 0xffffffffu, blend);
}

void lab_gfx_begin_portrait(void) {
  g_target = g_fbo;
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_fbo);
  lgl.Viewport(0, 0, g_sw, g_sh);
}

void lab_gl_bind_framebuffer(unsigned target, unsigned fb) {
  lgl.BindFramebufferOES(target, fb ? fb : g_target);
}

static void gl_get_integerv(GLenum pname, GLint *v) {
  lgl.GetIntegerv(pname, v);
  if (pname == GL_FRAMEBUFFER_BINDING_OES && v && (GLuint)*v == g_target)
    *v = 0;
}

/* The iPad engine's overlays (pause, level end) keep the phone's 320 x 480
 * screen (lab_loader.c): on the 3:4 surface that space is widened to
 * -20 .. 340, as tall, so they keep their shape (lab_gfx_touch_to_dp and
 * the pointer do the same). */
static void gl_orthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) {
  if (lab_eng_ipad(g_lab_eng)) {
    /* the first of them, for debug.log: which pass asked for which space */
    static int logged;
    if (logged < 40) {
      logged++;
      uintptr_t ra = (uintptr_t)__builtin_return_address(0), base = (uintptr_t)lab_module(g_lab_eng)->load_virtbase;
      debugPrintf("[ipad] glOrthof(%g, %g, %g, %g) from +0x%x\n", (double)l, (double)r, (double)b, (double)t,
                  (unsigned)(ra - base));
    }
    if (l == 0.0f && r == 320.0f && ((b == 0.0f && t == 480.0f) || (b == 480.0f && t == 0.0f)))
      l = -20.0f, r = 340.0f;
  }
  lgl.Orthof(l, r, b, t, n, f);
}

DynLibFunction lab_gl_overrides[] = {
    {"glBindFramebufferOES", (uintptr_t)lab_gl_bind_framebuffer},
    {"glGetIntegerv", (uintptr_t)gl_get_integerv},
    {"glOrthof", (uintptr_t)gl_orthof},
};
int lab_gl_overrides_count = (int)(sizeof lab_gl_overrides / sizeof lab_gl_overrides[0]);

/* ------------------------------------------------------------ local play
 * Two boards side by side, each its own surface (the window's size: 480 x
 * 720 at 720p, no supersampling -- two engines draw every frame), its engine
 * binding it as "the screen". The menus' portrait surface is composited over
 * them, cleared see-through (the countdown, the pause screen). iPad boards
 * (3:4) are a little lower than the window (486 x 648 at 720p), so the
 * players' margins beside them stay as wide as the iPhone boards'. */
static GLuint g_rfbo[2], g_rtex[2], g_rdepth[2];
static int g_rw, g_rh, g_race, g_race_ipad = -1;
void (*lab_gfx_race_side_hook)(int win_w, int win_h);
void (*lab_gfx_race_board_hook)(int eng, float x, float y, float w, float h);

int lab_gfx_race_on(int on, int ipad) {
  if (on && g_rfbo[0] && g_race_ipad != ipad) {
    /* the other boards' shape: the surfaces made again */
    for (int i = 0; i < 2; i++) {
      lgl.DeleteFramebuffersOES(1, &g_rfbo[i]);
      lgl.DeleteTextures(1, &g_rtex[i]);
      lgl.DeleteRenderbuffersOES(1, &g_rdepth[i]);
      g_rfbo[i] = g_rtex[i] = g_rdepth[i] = 0;
    }
  }
  if (on && !g_rfbo[0]) {
    g_race_ipad = ipad;
    g_rh = ipad ? g_win_h * 9 / 10 : g_win_h;
    g_rw = ipad ? g_rh * 3 / 4 : g_rh * 2 / 3;
    for (int i = 0; i < 2; i++)
      if (make_target(&g_rfbo[i], &g_rtex[i], &g_rdepth[i], g_rw, g_rh, "local play surface") != 0)
        return -1;
    debugPrintf("[gfx] local play: two %dx%d surfaces%s\n", g_rw, g_rh, ipad ? " (iPad boards)" : "");
  }
  g_race = on;
  lab_gfx_begin_portrait(); /* make_target left its surface bound */
  return 0;
}

int lab_gfx_race_active(void) { return g_race; }
int lab_gfx_race_w(void) { return g_rw; }
int lab_gfx_race_h(void) { return g_rh; }

void lab_gfx_race_begin(int eng) {
  g_target = g_rfbo[eng & 1];
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_target);
  lgl.Viewport(0, 0, g_rw, g_rh);
}

void lab_gfx_race_rect(int eng, float *x, float *y, float *w, float *h) {
  float gap = 20.0f * (float)g_win_h / 720.0f, total = 2.0f * (float)g_rw + gap;
  float x0 = ((float)g_win_w - total) * 0.5f;
  *x = x0 + (eng ? (float)g_rw + gap : 0.0f);
  *y = ((float)g_win_h - (float)g_rh) * 0.5f;
  *w = (float)g_rw, *h = (float)g_rh;
}

static void race_composite(void) {
  if (lab_gfx_race_side_hook)
    lab_gfx_race_side_hook(g_win_w, g_win_h);
  for (int i = 0; i < 2; i++) {
    float x, y, w, h;
    lab_gfx_race_rect(i, &x, &y, &w, &h);
    const float xy[8] = {x, y, x + w, y, x, y + h, x + w, y + h};
    const float uv[8] = {0, 1, 1, 1, 0, 0, 1, 0};
    LabTex t = {g_rtex[i], g_rw, g_rh, 1.0f, 1.0f};
    lab_draw_quad_raw(&t, xy, uv, 0xffffffffu, 0);
    if (lab_gfx_race_board_hook)
      lab_gfx_race_board_hook(i, x, y, w, h);
  }
  /* the menus' surface over them (see-through where nothing is drawn) */
  if (g_hshow) {
    hd_quad(1);
    return;
  }
  float ph = (float)g_win_h, pw = ph * 2.0f / 3.0f, px = ((float)g_win_w - pw) * 0.5f;
  const float xy[8] = {px, 0, px + pw, 0, px, ph, px + pw, ph};
  const float uv[8] = {0, 1, 1, 1, 0, 0, 1, 0};
  LabTex t = {g_tex, g_sw, g_sh, 1.0f, 1.0f};
  lab_draw_quad_raw(&t, xy, uv, 0xffffffffu, 1);
}

/* thumbnails: one FBO + depth, a new texture per picture */
static GLuint g_off_fbo, g_off_depth;
static int g_off_w, g_off_h;

static GLuint g_off_prev; /* the target before a thumbnail (the iPad menus', or the portrait) */
static GLint g_off_vp[4];

unsigned lab_gfx_offscreen_begin(int w, int h) {
  GLuint tex = 0;
  g_off_prev = g_target;
  lgl.GetIntegerv(GL_VIEWPORT, g_off_vp);
  if (g_off_fbo && (g_off_w != w || g_off_h != h)) {
    lgl.DeleteFramebuffersOES(1, &g_off_fbo);
    lgl.DeleteRenderbuffersOES(1, &g_off_depth);
    g_off_fbo = g_off_depth = 0;
  }
  lgl.GenTextures(1, &tex);
  lgl.BindTexture(GL_TEXTURE_2D, tex);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  lgl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  lgl.BindTexture(GL_TEXTURE_2D, 0);
  if (!g_off_fbo) {
    lgl.GenFramebuffersOES(1, &g_off_fbo);
    lgl.GenRenderbuffersOES(1, &g_off_depth);
    lgl.BindRenderbufferOES(GL_RENDERBUFFER_OES, g_off_depth);
    while (lgl.GetError() != GL_NO_ERROR)
      ;
    lgl.RenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_DEPTH_COMPONENT24_OES, w, h);
    if (lgl.GetError() != GL_NO_ERROR)
      lgl.RenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_DEPTH_COMPONENT16_OES, w, h);
    lgl.BindRenderbufferOES(GL_RENDERBUFFER_OES, 0);
    g_off_w = w, g_off_h = h;
  }
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_off_fbo);
  lgl.FramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, tex, 0);
  lgl.FramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, g_off_depth);
  GLenum st = lgl.CheckFramebufferStatusOES(GL_FRAMEBUFFER_OES);
  if (st != GL_FRAMEBUFFER_COMPLETE_OES) {
    debugPrintf("[gfx] thumbnail target %dx%d incomplete (0x%x)\n", w, h, st);
    lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_target);
    lgl.DeleteTextures(1, &tex);
    return 0;
  }
  g_target = g_off_fbo;
  lgl.Viewport(0, 0, w, h);
  return tex;
}

void lab_gfx_offscreen_end(void) {
  if (g_off_fbo) {
    lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_off_fbo);
    lgl.FramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, 0, 0);
  }
  if (g_off_prev && g_off_prev != g_fbo) {
    g_target = g_off_prev;
    lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_target);
    lgl.Viewport(g_off_vp[0], g_off_vp[1], g_off_vp[2], g_off_vp[3]);
    return;
  }
  lab_gfx_begin_portrait();
}

/* ------------------------------------------------------------ EGL */
#if DCR_GL_MESA
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLSurface g_win = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;
EGLBoolean b_eglSwapBuffers(EGLDisplay d, EGLSurface s); /* gl_mesa.c: frame count, hooks */

static int egl_init(void) {
  if (log_console_active())
    debugPrintf("[gfx] handing the screen from the boot log to the game\n");
  log_console_close(); /* for good: see util.c */
  dcr_window_prepare();
  g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint maj = 0, min = 0;
  if (!eglInitialize(g_dpy, &maj, &min)) {
    debugPrintf("[gfx] eglInitialize failed 0x%x\n", eglGetError());
    return -1;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  static const EGLint want[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                EGL_NONE};
  static const EGLint any[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT, EGL_NONE};
  EGLConfig cfg;
  EGLint n = 0;
  if ((!eglChooseConfig(g_dpy, want, &cfg, 1, &n) || n < 1) &&
      (!eglChooseConfig(g_dpy, any, &cfg, 1, &n) || n < 1)) {
    debugPrintf("[gfx] no ES 1 config (0x%x)\n", eglGetError());
    return -1;
  }
  g_win = eglCreateWindowSurface(g_dpy, cfg, (EGLNativeWindowType)nwindowGetDefault(), NULL);
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE};
  g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
  if (g_win == EGL_NO_SURFACE || g_ctx == EGL_NO_CONTEXT || !eglMakeCurrent(g_dpy, g_win, g_win, g_ctx)) {
    debugPrintf("[gfx] surface %p / context %p / make current failed (0x%x)\n", g_win, g_ctx,
                eglGetError());
    return -1;
  }
  eglSwapInterval(g_dpy, 1);
  debugPrintf("[gfx] EGL %d.%d, an OpenGL ES 1 context\n", maj, min);
  return 0;
}
/* a scripted run's picture (lab_test.c): the window as composed, read back
 * through this context (ES 1: gl_mesa.c's capture reads through the ES 2
 * entry points, which give an ES 1 context's window back black), saved as
 * <root>/test/<name>.bmp; an all-black read (the emulator's first frames)
 * is tried again, up to 30 frames */
static char g_cap[64];
static int g_cap_tries;
void lab_gfx_capture(const char *name) {
  snprintf(g_cap, sizeof g_cap, "%s", name);
  g_cap_tries = 0;
}
static void capture_now(void) {
  int w = g_win_w, h = g_win_h;
  uint8_t *px = malloc((size_t)w * h * 4);
  if (!px)
    return;
  lgl.PixelStorei(GL_PACK_ALIGNMENT, 4);
  lgl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
  size_t lit = 0;
  for (size_t i = 0; i < (size_t)w * h * 4 && !lit; i += 4 * 97)
    lit = px[i] | px[i + 1] | px[i + 2];
  if (!lit && ++g_cap_tries < 30) {
    free(px);
    return;
  }
  /* half size (the card fills up with full ones): each pixel of 2 x 2 */
  int hw = w / 2, hh = h / 2;
  for (int y = 0; y < hh; y++)
    for (int x = 0; x < hw; x++)
      for (int c = 0; c < 4; c++) {
        const uint8_t *q = px + ((size_t)(y * 2) * w + (size_t)x * 2) * 4 + c;
        px[((size_t)y * hw + x) * 4 + c] = (uint8_t)((q[0] + q[4] + q[(size_t)w * 4] + q[(size_t)w * 4 + 4]) / 4);
      }
  w = hw, h = hh;
  char path[320];
  snprintf(path, sizeof path, "%s/test/%s.bmp", dcr_game_root(), g_cap);
  g_cap[0] = 0;
  FILE *f = fopen(path, "wb");
  if (f) {
    const uint32_t row = (uint32_t)w * 3, size = 54 + row * (uint32_t)h;
    uint8_t hdr[54] = {'B', 'M'};
    uint32_t v[][2] = {{2, size}, {10, 54}, {14, 40}, {18, (uint32_t)w}, {22, (uint32_t)h}, {34, row * (uint32_t)h}};
    for (unsigned k = 0; k < sizeof v / sizeof v[0]; k++)
      for (int b = 0; b < 4; b++)
        hdr[v[k][0] + b] = (uint8_t)(v[k][1] >> (8 * b));
    hdr[26] = 1, hdr[28] = 24;
    fwrite(hdr, 1, sizeof hdr, f);
    uint8_t *line = malloc(row);
    for (int y = 0; line && y < h; y++) {
      const uint8_t *q = px + (size_t)y * w * 4;
      for (int x = 0; x < w; x++)
        line[x * 3] = q[x * 4 + 2], line[x * 3 + 1] = q[x * 4 + 1], line[x * 3 + 2] = q[x * 4];
      fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
    debugPrintf("[capture] %s (%dx%d)%s\n", path, w, h, lit ? "" : ": all black");
  }
  free(px);
}
static void egl_swap(void) { b_eglSwapBuffers(g_dpy, g_win); }
#else
static int egl_init(void) { return 0; }
static void egl_swap(void) {}
#endif

int lab_gfx_init(void) {
  if (egl_init() != 0)
    return -1;
  if (lab_gl_load() != 0)
    return -1;
  debugPrintf("[gfx] %s | %s | %s\n", (const char *)lgl.GetString(GL_VENDOR),
              (const char *)lgl.GetString(GL_RENDERER), (const char *)lgl.GetString(GL_VERSION));
  dcr_window_size(&g_win_w, &g_win_h);
  const DcrConfig *c = dcr_config();
  g_base_rot = c->layout == LAB_LAYOUT_ROTATED_LEFT ? 90 : c->layout == LAB_LAYOUT_ROTATED_RIGHT ? 270 : 0;
  g_rot = g_base_rot;
  float bw, bh; /* the board in window pixels, as shown */
  place(g_rot, &g_px, &g_py, &g_pw, &g_ph, &bw, &bh);
  g_show[0] = bw, g_show[1] = bh, g_show[2] = angle_of(g_rot);
  /* the surface: the board as big as it is ever shown (the rotated view of
   * a level is bigger than the upright one); twice that when supersampled */
  int pw = (int)bw, ph = (int)bh;
  if (!g_base_rot) {
    float rx, ry, rw, rh, rbw, rbh;
    place(90, &rx, &ry, &rw, &rh, &rbw, &rbh);
    if (c->supersample <= 1 && rbw > bw)
      pw = (int)rbw, ph = (int)rbh;
  }
  g_sw = pw * c->supersample;
  g_sh = ph * c->supersample;
  if (make_target(&g_fbo, &g_tex, &g_depth, g_sw, g_sh, "portrait surface") != 0) {
    if (c->supersample <= 1)
      return -1;
    g_sw = pw, g_sh = ph; /* without the supersampling */
    if (make_target(&g_fbo, &g_tex, &g_depth, g_sw, g_sh, "portrait surface") != 0)
      return -1;
  }
  g_ok = 1;
  lab_gfx_begin_portrait();
  lgl.ClearColor(0, 0, 0, 1);
  lgl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  debugPrintf("[gfx] window %dx%d; portrait surface %dx%d (%s), shown at %.0f,%.0f %.0fx%.0f%s\n",
              g_win_w, g_win_h, g_sw, g_sh, c->supersample > 1 ? "supersampled" : "1:1", g_px, g_py,
              g_pw, g_ph, g_rot ? (g_rot == 90 ? ", turned: top to the left" : ", turned: top to the right") : "");
  return 0;
}

/* the window's picture, drawn into the target bound now */
static void compose(void) {
  lab_draw_begin((float)g_win_w, (float)g_win_h, g_win_w, g_win_h);
  lgl.ClearColor(0, 0, 0, 1);
  lgl.Clear(GL_COLOR_BUFFER_BIT);
  if (g_race) {
    race_composite();
    lab_draw_end();
    return;
  }
  if (g_hshow && !g_hover) {
    hd_quad(0); /* the iPad menus fill the window */
    lab_draw_end();
    return;
  }
  if (lab_gfx_side_hook && !g_base_rot)
    lab_gfx_side_hook(g_win_w, g_win_h, g_px, g_py, g_pw, g_ph);
  /* the surface: four corners about the window's centre, turned, texture v up */
  float xy[8], uv[8];
  static const float corner[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  float a = g_show[2] * 3.14159265f / 180.0f, ca = cosf(a), sa = sinf(a);
  float cx = (float)g_win_w * 0.5f, cy = (float)g_win_h * 0.5f;
  for (int i = 0; i < 4; i++) {
    float lx = (corner[i][0] - 0.5f) * g_show[0], ly = (corner[i][1] - 0.5f) * g_show[1];
    xy[i * 2] = cx + lx * ca - ly * sa;
    xy[i * 2 + 1] = cy + lx * sa + ly * ca;
    uv[i * 2] = corner[i][0];
    uv[i * 2 + 1] = 1.0f - corner[i][1];
  }
  LabTex t = {g_ipic ? g_itex : g_tex, g_ipic ? g_iw : g_sw, g_ipic ? g_ih : g_sh, 1.0f, 1.0f};
  lab_draw_quad_raw(&t, xy, uv, 0xffffffffu, 0);
  if (g_hshow && g_hover)
    hd_quad(1);
  lab_draw_end();
}

void lab_gfx_present(void) {
  if (!g_ok)
    return;
  animate();
  if (g_cap[0]) {
    /* a scripted run's picture: composed once more into a surface of its own
     * and read from there (the emulator reads the window back black), a new
     * one each time, kept (Ryujinx 1.1.1098 reads a surface back once: then
     * the first picture again, or black for one made where one was deleted) */
    GLuint cfbo = 0, ctex = 0;
    if (make_target(&cfbo, &ctex, NULL, g_win_w, g_win_h, "capture surface") == 0) {
      lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, cfbo);
      lgl.Viewport(0, 0, g_win_w, g_win_h);
      compose();
      capture_now();
    }
  }
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, 0);
  lgl.Viewport(0, 0, g_win_w, g_win_h);
  compose();
  egl_swap();
  lab_gfx_begin_portrait();
}
