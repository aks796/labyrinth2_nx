/* lab_screens.c -- the game's Android screens, rebuilt.
 *
 * Each follows its activity and layout (res/layout/<name>.xml, dp as written
 * there, on the 320 x 480 dp portrait) and draws with the APK's own hdpi
 * pictures; what the activity did on a tap, the screen does on a tap or A:
 *   splash       StartupActivity (splashscreen_166) while the sounds decode
 *   main menu    NewMainMenuFull + MainMenuButtonView: three tilted bars
 *                sliding in (Play game, Create, Download levels), Settings,
 *                the chosen ball (Awards)
 *   level packs  LevelPackActivity: Official / Downloaded, groups Ongoing /
 *                New / Finished (LevelsDB's queries), a row plays, its (i)
 *                opens the info
 *   level info   LevelPackInfoActivity(Full): the level's picture (the
 *                engine's ThumbnailManager), previous / next up to the
 *                levels finished, your best and the designer's time; Play,
 *                or Delete + Play (downloaded), Publish + Play (your own),
 *                Download (a server pack looked at before downloading)
 *   settings     SettingsActivity: Calibrate, Sound FX, Tilt view; the
 *                port's tilt source; Credits
 *   awards       AwardsActivity: the four balls, the counts, the list
 *   credits      CreditsActivity
 *   download, create, the editor's how-to: lab_screens_online.c
 * MIT.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "lab.h"
#include "lab_ui.h"
#include "util.h"

LabPack *g_ui_pack;

#define BLACK 0x000000ffu
#define WHITE 0xffffffffu

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* LevelPackInfoActivityBase.a(int): "1 m 2.5 s" */
static void fmt_time(char *out, size_t cap, int ms) {
  float f = (float)ms / 1000.0f;
  int m = (int)(f / 60.0f);
  char sec[32];
  if (m > 0)
    f -= (float)(m * 60);
  snprintf(sec, sizeof sec, "%.2f", (double)f);
  /* NumberFormat, 2 fraction digits at most: no trailing zeros */
  char *dot = strchr(sec, '.');
  if (dot) {
    char *e = sec + strlen(sec) - 1;
    while (e > dot && *e == '0')
      *e-- = 0;
    if (e == dot)
      *e = 0;
  }
  if (m > 0)
    snprintf(out, cap, "%d m %s s", m, sec);
  else
    snprintf(out, cap, "%s s", sec);
}

static const LabTex *tex(const char *n) { return lab_tex(n); }

/* a picture at its hdpi size (dp), top-left at x, y */
static void pic(const char *name, float x, float y, uint32_t rgba) {
  const LabTex *t = tex(name);
  if (t)
    lab_draw_image(t, x, y, lab_tex_dp_w(t), lab_tex_dp_h(t), rgba);
}

static float pic_w(const char *name) { return lab_tex_dp_w(tex(name)); }
static float pic_h(const char *name) { return lab_tex_dp_h(tex(name)); }

/* An Android-2.x-style push button ("SET") / toggle ("ON" / "OFF"). */
static void android_button(float x, float y, float w, float h, const char *label, int down, int toggle,
                           int on) {
  lab_draw_rrect(x, y, w, h, 5, down ? 0xe8a33cffu : 0xd8d8d0ffu);
  lab_draw_rrect(x + 1.5f, y + 1.5f, w - 3, h - 3, 4, down ? 0xf2b24cffu : 0xf4f4eeffu);
  lab_draw_rrect(x + 1.5f, y + h * 0.5f, w - 3, h * 0.5f - 1.5f, 4, down ? 0xe8a33cffu : 0xe2e2daffu);
  float ty = y + h * 0.5f + 5.5f;
  if (toggle) {
    ty -= 4;
    lab_draw_rrect(x + w * 0.25f, y + h - 10, w * 0.5f, 4, 2, on ? 0x55d02cffu : 0x8c8c86ffu);
  }
  lab_draw_text(x + w * 0.5f, ty, 15, 0x202020ffu, LAB_CENTER, 1, label);
}

/* =============================================================== splash */
static float g_splash_t0;

static void splash_frame(void) {
  const LabTex *t = tex("splashscreen_166");
  if (t) {
    /* cover the portrait (480x800 art, 2:3 screen): trimmed top and bottom */
    float h = 320.0f * (float)t->h / (float)t->w, over = (h - 480.0f) * 0.5f / h;
    lab_draw_image_uv(t, 0, 0, 320, 480, 0, over, 1, 1 - over, WHITE);
  }
  float el = ui_time() - g_splash_t0;
  if ((lab_audio_ready() && el > 1.2f) || el > 8.0f)
    ui_reset(SCR_MAIN);
}

/* ============================================================ main menu */
static float g_mm_anim;       /* when the bars start sliding in */
static int g_mm_pressed = -1; /* m[]: the bar a finger is on */

typedef struct {
  float m[6]; /* local dp -> screen dp */
} Xf;

/* MainMenuButtonView.setSize(480 px, 720 px), density 1.5, in dp:
 * T(0, ty) R(-35) S(0.66) */
#define MM_BARS 4
static Xf bar_xf(int i, float slide) {
  /* the phone's three at 195, 295, 395; the port's four 90 dp apart */
  const float ty[MM_BARS] = {158.0f, 248.0f, 338.0f, 428.0f};
  const float a = -35.0f * 3.14159265f / 180.0f, c = cosf(a) * 0.66f, s = sinf(a) * 0.66f;
  Xf x = {{c, -s, ty[i] * 0 + 0, s, c, ty[i]}};
  /* the local translation (slide, 0) applied first */
  x.m[2] = c * slide;
  x.m[5] = ty[i] + s * slide;
  return x;
}

static void xf_point(const Xf *x, float lx, float ly, float *sx, float *sy) {
  *sx = x->m[0] * lx + x->m[1] * ly + x->m[2];
  *sy = x->m[3] * lx + x->m[4] * ly + x->m[5];
}

static int xf_inverse(const Xf *x, float sx, float sy, float *lx, float *ly) {
  float det = x->m[0] * x->m[4] - x->m[1] * x->m[3];
  if (fabsf(det) < 1e-6f)
    return 0;
  float dx = sx - x->m[2], dy = sy - x->m[5];
  *lx = (x->m[4] * dx - x->m[1] * dy) / det;
  *ly = (-x->m[3] * dx + x->m[0] * dy) / det;
  return 1;
}

/* ---- Multi player, the port's fourth bar (lab_versus.c). The game has no
 * picture for it, so it is put together from the other bars' when the menu
 * first shows: the bar is bar2 with its stripe red (the fourth player's dot);
 * the label is set in the other labels' letters -- "M" an "m" made taller,
 * "u" an "n" and "p" a "d" turned over, "i" an "l" cut in two -- after
 * their icon's ring with two balls in it. The console's font if the
 * pictures are not the ones expected. */
typedef struct {
  uint8_t *px;
  int w, h;
} Img;

static int img_load(Img *m, const char *name) {
  char path[96];
  snprintf(path, sizeof path, "res/drawable-hdpi-v4/%s.png", name);
  size_t len = 0;
  uint8_t *data = lab_apk_read(path, &len);
  m->px = data ? lab_image_decode(data, len, &m->w, &m->h) : NULL;
  free(data);
  return m->px != NULL;
}

static uint8_t *img_at(const Img *m, int x, int y) { return m->px + ((size_t)y * m->w + x) * 4; }

static LabTex *mp_bar_make(void) {
  Img b = {0};
  if (!img_load(&b, "bar2"))
    return NULL;
  /* the stripe: the rows along the top that are orange */
  const int base[3] = {226, 158, 30}, want[3] = {191, 53, 53};
  for (int y = 0; y < b.h && y < 48; y++) {
    const uint8_t *m = img_at(&b, b.w / 2, y);
    if (m[3] && !(m[0] > 150 && m[2] < 100))
      continue;
    for (int x = 0; x < b.w; x++) {
      uint8_t *q = img_at(&b, x, y);
      for (int c = 0; c < 3; c++) {
        int v = want[c] + (q[c] - base[c]);
        q[c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
      }
    }
  }
  LabTex *t = lab_tex_from_rgba(b.px, b.w, b.h);
  free(b.px);
  return t;
}

enum { G_COPY, G_TURN, G_TALL, G_I, G_SPACE };
static const struct {
  uint8_t src, op;
  short x0, x1, a, b, c;
} k_mp_glyphs[] = {
    /* the pictures: 0 playgame, 1 create, 2 dowloadlevels; the baseline at 71 */
    {0, G_TALL, 367, 412, 0, 0, 7},     /* M: "game"'s m, its stems 12 rows longer */
    {2, G_TURN, 260, 290, 35, 71, 35},  /* u: "Download"'s n turned over */
    {0, G_COPY, 168, 177, 0, 0, 7},     /* l */
    {1, G_COPY, 267, 282, -1, 0, 6},    /* t ("Create" sits a row lower) */
    {0, G_I, 168, 177, 0, 0, 0},        /* i: an l's foot, a square of it above */
    {0, G_SPACE, 0, 28, 0, 0, 0},
    {2, G_TURN, 392, 422, 21, 71, 35},  /* p: a d turned over, into the descender */
    {0, G_COPY, 168, 177, 0, 0, 7},     /* l */
    {0, G_COPY, 185, 215, 0, 0, 7},     /* a */
    {0, G_COPY, 224, 254, 0, 0, 7},     /* y */
    {0, G_COPY, 419, 449, 0, 0, 7},     /* e */
    {1, G_COPY, 167, 182, -1, 0, 0},    /* r */
};

static LabTex *mp_label_font(int down) {
  const int W = 484, H = 92;
  uint8_t *cov = calloc((size_t)W * H, 1), *o = calloc((size_t)W * H, 4);
  LabTex *t = NULL;
  if (cov && o && lab_font_ready()) {
    lab_font_draw(cov, W, H, W, 127, 71, 58, "Multi player", 1);
    const uint8_t c[3] = {down ? 0x1b : 244, down ? 0x2e : 240, down ? 0x24 : 220};
    for (int i = 0; i < W * H; i++)
      o[i * 4] = c[0], o[i * 4 + 1] = c[1], o[i * 4 + 2] = c[2], o[i * 4 + 3] = cov[i];
    t = lab_tex_from_rgba(o, W, H);
  }
  free(cov);
  free(o);
  return t;
}

static LabTex *mp_label_make(int down) {
  static const char *const names[3] = {"playgame", "create", "dowloadlevels"};
  static const int want_w[3] = {468, 330, 638};
  Img src[3] = {{0}};
  int ok = 1;
  for (int i = 0; i < 3; i++) {
    char n[40];
    snprintf(n, sizeof n, "%s%s", names[i], down ? "_down" : "");
    if (!img_load(&src[i], n) || src[i].w != want_w[i] || src[i].h != 92)
      ok = 0;
  }
  LabTex *t = NULL;
  const int W = 484, H = 92;
  uint8_t *o = ok ? calloc((size_t)W * H, 4) : NULL;
  if (o) {
    /* the icon: playgame's ring (without its triangle), two balls in it */
    const Img *pg = &src[0];
    uint8_t col[3] = {244, 240, 220};
    for (int x = 127; x < 163; x++)
      if (img_at(pg, x, 46)[3] > 200) {
        memcpy(col, img_at(pg, x, 46), 3);
        break;
      }
    for (int y = 0; y < H; y++)
      for (int x = 0; x < 90; x++) {
        float dx = (float)x - 45.0f, dy = (float)y - 46.0f;
        uint8_t *d = o + ((size_t)y * W + x) * 4;
        if (dx * dx + dy * dy > 31.0f * 31.0f)
          memcpy(d, img_at(pg, x, y), 4);
        static const float balls[2][2] = {{34.0f, 53.0f}, {57.0f, 39.0f}};
        for (int k = 0; k < 2; k++) {
          float bx = (float)x + 0.5f - balls[k][0], by = (float)y + 0.5f - balls[k][1];
          float a = 10.5f - sqrtf(bx * bx + by * by) + 0.5f;
          a = a < 0 ? 0 : a > 1 ? 1 : a;
          if (a > 0) {
            float na = a + (float)d[3] / 255.0f * (1 - a);
            memcpy(d, col, 3);
            d[3] = (uint8_t)(na * 255.0f + 0.5f);
          }
        }
      }
    /* the letters */
    int x = 127;
    for (unsigned g = 0; g < sizeof k_mp_glyphs / sizeof k_mp_glyphs[0]; g++) {
      const int op = k_mp_glyphs[g].op, x0 = k_mp_glyphs[g].x0, x1 = k_mp_glyphs[g].x1;
      const int a = k_mp_glyphs[g].a, b = k_mp_glyphs[g].b;
      if (op == G_SPACE) {
        x += x1;
        continue;
      }
      const Img *m = &src[k_mp_glyphs[g].src];
      int w = x1 - x0 + 1;
      for (int dx = 0; dx < w && x + dx < W; dx++)
        for (int y = 0; y < H; y++) {
          int sx = x0 + dx, sy = -1;
          if (op == G_COPY)
            sy = y - a;
          else if (op == G_TURN) {
            if (y >= k_mp_glyphs[g].c && y <= k_mp_glyphs[g].c + (b - a))
              sy = b - (y - k_mp_glyphs[g].c), sx = x1 - dx;
          } else if (op == G_TALL) {
            sy = y >= 23 && y <= 43 ? y + 12 : y >= 44 && y <= 55 ? 55 : y >= 56 && y <= 71 ? y : -1;
          } else if (op == G_I) {
            sy = y >= 35 && y <= 71 ? y : y >= 21 && y <= 29 ? y + 15 : -1;
          }
          if (sy >= 0 && sy < m->h && img_at(m, sx, sy)[3])
            memcpy(o + ((size_t)y * W + x + dx) * 4, img_at(m, sx, sy), 4);
        }
      x += w + k_mp_glyphs[g].c * (op != G_TURN) + 7 * (op == G_TURN);
    }
    t = lab_tex_from_rgba(o, W, H);
    free(o);
  }
  for (int i = 0; i < 3; i++)
    free(src[i].px);
  return t ? t : mp_label_font(down);
}

/* the bars and their labels, top to bottom */
static const LabTex *mm_bar(int i) {
  static LabTex *mp;
  static int tried;
  static const char *const bars[MM_BARS] = {"bar3", NULL, "bar2", "bar1"};
  if (bars[i])
    return tex(bars[i]);
  if (!tried++)
    mp = mp_bar_make();
  return mp ? mp : tex("bar2");
}

static const LabTex *mm_label(int i, int down) {
  static LabTex *mp[2];
  static int tried[2];
  static const char *const labels[MM_BARS][2] = {{"playgame", "playgame_down"},
                                                 {NULL, NULL},
                                                 {"create", "create_down"},
                                                 {"dowloadlevels", "dowloadlevels_down"}};
  if (labels[i][0])
    return tex(labels[i][down ? 1 : 0]);
  if (!tried[down]++)
    mp[down] = mp_label_make(down);
  return mp[down];
}

static void mm_enter(void) {
  g_mm_anim = ui_time() + 0.25f; /* a(): 250 ms, then 350 ms of sliding */
  g_mm_pressed = -1;
  lab_ui_side_info(NULL, NULL, NULL);
}

static void quit_answer(int yes) {
  if (yes)
    scr_request_quit();
}

static const char *ball_name(int down) {
  static const char *const n[4][2] = {{"steel", "steel_down"}, {"bronze", "bronze_down"},
                                      {"silver", "silver_down"}, {"gold", "gold_down"}};
  int b = lab_reg_get_int("setting-ball-selected", 0);
  if (b < 0 || b > 3)
    b = 0;
  return n[b][down ? 1 : 0];
}

static int g_mp; /* the level packs are Multi player's (below) */

static void mm_open(int i) {
  static const int to[MM_BARS] = {SCR_PACKS, SCR_PACKS, SCR_CREATE, SCR_DOWNLOAD};
  if (i >= 2)
    scr_online_reset();
  g_mp = i == 1;
  ui_push(to[i]);
}

static void mm_frame(void) {
  ui_background("main_menu2_bg");
  float now = ui_time();
  float f = clampf((now - g_mm_anim) / 0.35f, 0, 1);
  float interp = 1.0f - (1.0f - f) * (1.0f - f); /* DecelerateInterpolator(1) */
  float slide = (-1050.0f + 945.0f * interp) / 1.5f; /* s, in dp */
  float alpha = now < g_mm_anim ? 25.0f : interp * 255.0f;
  uint32_t a = (uint32_t)alpha;
  const LabPad *p = ui_pad;
  /* touches: in the button's own (rotated) frame, a press on ACTION_DOWN */
  int tapped = -1;
  if (p->touch_began && p->touch_in) {
    for (int i = 0; i < MM_BARS; i++) {
      Xf x = bar_xf(i, 0);
      float lx, ly;
      const LabTex *lt = mm_label(i, 0);
      if (lt && xf_inverse(&x, p->tx, p->ty, &lx, &ly) && lx >= 0 && lx < lab_tex_dp_w(lt) && ly >= 20 &&
          ly < 20 + lab_tex_dp_h(lt))
        tapped = i;
    }
  }
  if (!p->touch)
    g_mm_pressed = -1;
  for (int i = 0; i < MM_BARS; i++) {
    Xf bx = bar_xf(i, slide);
    int focus = ui_focused(100 + i);
    const LabTex *lt = mm_label(i, 0);
    lab_draw_image_xform(mm_bar(i), bx.m, (0xffffff00u) | a);
    if (!lt)
      continue;
    float lw = lab_tex_dp_w(lt), lh = lab_tex_dp_h(lt);
    if (focus && f >= 1.0f) {
      /* the focus: a soft glow along the bar, behind its label */
      float gx, gy;
      Xf l0 = bar_xf(i, 0);
      xf_point(&l0, lw * 0.5f, 20 + lh * 0.5f, &gx, &gy);
      lab_draw_glow_at(gx, gy, lw * 0.66f * 0.5f + 6.0f, lh * 0.66f * 0.5f + 2.0f, -35.0f, 1.3f);
    }
    Xf lx = bar_xf(i, 0);
    /* the label at (0, 20) in the bar's frame */
    lx.m[2] += lx.m[1] * 20.0f;
    lx.m[5] += lx.m[4] * 20.0f;
    int down = g_mm_pressed == i || (focus && (p->held & HidNpadButton_A));
    const LabTex *ld = down ? mm_label(i, 1) : lt;
    lab_draw_image_xform(ld ? ld : lt, lx.m, 0xffffff00u | a);
    /* for the controller: the label's box on the screen */
    float x0, y0, x1, y1, x2, y2, x3, y3, w = lw, h = lh;
    Xf l0 = bar_xf(i, 0);
    xf_point(&l0, 0, 20, &x0, &y0);
    xf_point(&l0, w, 20, &x1, &y1);
    xf_point(&l0, 0, 20 + h, &x2, &y2);
    xf_point(&l0, w, 20 + h, &x3, &y3);
    float minx = fminf(fminf(x0, x1), fminf(x2, x3)), maxx = fmaxf(fmaxf(x0, x1), fmaxf(x2, x3));
    float miny = fminf(fminf(y0, y1), fminf(y2, y3)), maxy = fmaxf(fmaxf(y0, y1), fmaxf(y2, y3));
    if (ui_button(100 + i, minx, miny, maxx - minx, maxy - miny, UI_NORING | UI_NOTOUCH))
      mm_open(i);
  }
  if (tapped >= 0) {
    g_mm_pressed = tapped;
    lab_audio_click();
    ui_focus(100 + tapped);
    mm_open(tapped);
    return;
  }
  /* Settings: top left, 10 dp padding */
  float sw = pic_w("settings"), sh = pic_h("settings");
  if (ui_button(1, 10, 10, sw, sh, UI_NORING)) {
    ui_push(SCR_SETTINGS);
    return;
  }
  if (ui_focused(1))
    lab_draw_glow_behind(10, 10, sw, sh); /* the focus: a glow behind it */
  pic(ui_down(1) ? "settings_down" : "settings", 10, 10, WHITE);
  /* the ball: Awards, bottom right, 10 dp margin */
  float bw = pic_w(ball_name(0)), bh = pic_h(ball_name(0));
  float bx = 320 - 10 - bw, by = 480 - 10 - bh;
  if (ui_button(2, bx, by, bw, bh, UI_NORING)) {
    ui_push(SCR_AWARDS);
    return;
  }
  if (ui_focused(2))
    lab_draw_glow_behind(bx, by, bw, bh);
  pic(ball_name(ui_down(2)), bx, by, WHITE);
  ui_default_focus(100);
  if (ui_back())
    ui_confirm("Labyrinth 2", "Quit the game?", "Quit", "Cancel", quit_answer);
  ui_hint("A", "Select");
  ui_hint("B", "Quit");
}

/* ========================================================== level packs */
static int g_official = 1;
static float g_packs_scroll;
static int g_group_open[3] = {1, 1, 1};
static int g_packs_focus_want = -1;

static const char *const k_group_name[3] = {"Ongoing", "New", "Finished"};
static const uint32_t k_group_bg[3] = {0x777260ffu, 0x518d89ffu, 0x0f0d0affu};

static void packs_enter(void) { lab_ui_side_info(NULL, NULL, NULL); }

static void open_game(LabPack *p);

/* LevelDotsView: green = finished, white = not, the current one ringed */
static void level_dots(float x, float y, int n, int finished, int current, int selected, float pad) {
  if (finished > n)
    finished = n;
  float e = pic_w("info_dot_white") + pad;
  float cx = x + pad * 0.5f;
  for (int i = 0; i < n; i++)
    pic(i < finished ? "info_dot_green" : "info_dot_white", cx + e * (float)i, y + pad, WHITE);
  if (selected >= 0)
    pic("info_dot_black", x + e * (float)selected + pad * 0.5f, y + pad, WHITE);
  if (current >= 0 && current < n)
    pic("info_dot_selected", x + e * (float)current + pad * 0.5f, y + pad, WHITE);
}

static void diff_style(const LabPack *k, uint32_t *bg, const char **icon, const char **info) {
  if (k->tutorial) {
    *bg = 0xd9e394ffu, *icon = "icon_tutorial_transp", *info = "icon_info_green";
  } else if (k->difficulty == 1) {
    *bg = 0xf5d9a6ffu, *icon = "icon_medium_transp", *info = "icon_info_orange";
  } else if (k->difficulty == 2) {
    *bg = 0xc9c4a3ffu, *icon = "icon_hard_transp", *info = "icon_info_black";
  } else {
    *bg = 0xd9e394ffu, *icon = "icon_easy_transp", *info = "icon_info_green";
  }
}

/* one line, cut with "..." to fit w */
static void text_fit(float x, float y, float size, uint32_t c, int bold, float w, const char *s) {
  if (lab_text_width(size, bold, s) <= w) {
    lab_draw_text(x, y, size, c, LAB_LEFT, bold, s);
    return;
  }
  char buf[160];
  size_t n = strlen(s);
  if (n >= sizeof buf - 4)
    n = sizeof buf - 4;
  while (n > 0) {
    memcpy(buf, s, n);
    strcpy(buf + n, "...");
    if (lab_text_width(size, bold, buf) <= w)
      break;
    n--;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
      n--;
  }
  lab_draw_text(x, y, size, c, LAB_LEFT, bold, buf);
}

/* ========================================================= multi player
 * The iPhone game's Multi player: a level pack, then how to play it -- over
 * Wi-Fi or Bluetooth, up to four phones racing through its levels. The
 * Android build has no network play left (its engine keeps the titles and
 * the players' dots); here a third way, Local, races two players on this
 * console, a board each (lab_versus.c). The list is the level packs', but
 * the tutorials; a pack's dots show where its races are. */
static struct {
  int on, mode, start_after;
  LabPack *p;
  int row_focus;
} MP;

static const char *const k_mp_mode[3] = {"Wi-Fi", "Bluetooth", "Local"};

/* the list's rows (Multi player: not the tutorials) */
static int packs_query(int group, LabPack **out) {
  int n = lab_levels_query(group, g_official, out, 1024);
  if (!g_mp)
    return n;
  int m = 0;
  for (int i = 0; i < n; i++)
    if (!out[i]->tutorial)
      out[m++] = out[i];
  return m;
}

#define PB(...) (MP.on ? 0 : ui_button(__VA_ARGS__))

static void mp_close(void) {
  MP.on = 0;
  MP.start_after = 0;
  lab_input_local_play(0);
  ui_focus(MP.row_focus);
}

static void mp_go(void) {
  LabPack *p = MP.p;
  MP.on = 0;
  MP.start_after = 0;
  ui_focus(MP.row_focus);
  if (p && lab_versus_start(p) == 0)
    ui_push(SCR_VERSUS);
  else
    lab_input_local_play(0);
}

/* the console's controller screen closed: ok if two players are set */
static void mp_controllers_done(int ok) {
  if (MP.on && MP.start_after && ok)
    mp_go();
  MP.start_after = 0;
}

static void mp_controllers(int then_start) {
  lab_input_local_play(1); /* the screen offers a Joy-Con each, held sideways */
  MP.start_after = then_start;
  if (lab_controllers_request(mp_controllers_done) != 0)
    MP.start_after = 0;
}

static void mp_open(LabPack *p) {
  if (!p->preloaded) {
    lab_files_complete_pack(p);
    char why[160], msg[256];
    if (lab_files_check_pack(p, why, sizeof why)) {
      snprintf(msg, sizeof msg, "%s\n\n%s", why,
               p->ownlevel ? "Refresh it on the Create screen." : "Delete it and download it again.");
      ui_confirm("Can't play this level pack", msg, "OK", NULL, NULL);
      return;
    }
  }
  memset(&MP, 0, sizeof MP);
  MP.on = 1;
  MP.p = p;
  MP.mode = 2;
  MP.row_focus = ui_focus_id();
  ui_focus(710);
}

static float text_h(float size, int bold, float w, const char *s) {
  int starts[24], lens[24];
  float k = lab_gfx_px_per_dp();
  return (float)lab_text_wrap(s, size * k, bold, w * k, starts, lens, 24) * size * 1.3f;
}

/* a player's dot (overlay-multiplayer-dots.png: 37 px, 40 apart) */
static void mp_dot(float cx, float cy, float r, int player) {
  const LabTex *t = tex("assets/textures/common/overlay-multiplayer-dots.png");
  if (!t)
    return;
  float u0 = (float)(40 * player) / 256.0f;
  lab_draw_image_uv(t, cx - r, cy - r, 2 * r, 2 * r, u0, 0, u0 + 37.0f / 256.0f, 37.0f / 64.0f, WHITE);
}

static void mp_frame(void) {
  lab_draw_rect(0, 0, 320, 480, 0x000000a0u);
  const float x = 20, w = 280, iw = w - 32, bx = x + 16;
  const int local = MP.mode == 2;
  const char *body =
      local ? "Two players, a board each, the same level: the first ball in the goal wins it. Hold a "
              "Joy-Con each sideways, or use two controllers."
            : "The iPhone game raced up to four phones over Wi-Fi or Bluetooth. The Android game this "
              "port runs has no network play, so Local races two players on this console instead.";
  float body_h = text_h(13, 0, iw, body);
  float h = 104 + body_h + 12 + (local ? 52 + 86 : 38) + 16, y = (480 - h) * 0.5f;
  const LabTex *bg = tex("bg_credits_popup");
  lab_draw_rrect(x - 3, y - 3, w + 6, h + 6, 10, 0x00000060u);
  if (bg)
    lab_draw_image(bg, x, y, w, h, WHITE);
  else
    lab_draw_rrect(x, y, w, h, 8, 0xf6ecd0ffu);
  lab_draw_text(160, y + 30, 17, 0x1e3a3cffu, LAB_CENTER, 1, "Multi player");
  if (lab_text_width(13, 1, MP.p->name) <= iw)
    lab_draw_text(160, y + 50, 13, 0x202020ffu, LAB_CENTER, 1, MP.p->name);
  else
    text_fit(bx, y + 50, 13, 0x202020ffu, 1, iw, MP.p->name);
  /* Wi-Fi | Bluetooth | Local, as the iPhone's chooser (L / R too) */
  float sy = y + 62, sh = 30, sw = iw / 3;
  lab_draw_rrect(bx, sy, iw, sh, 7, 0x3a7f84ffu);
  for (int i = 0; i < 3; i++) {
    float sx = bx + sw * (float)i;
    if (ui_button(700 + i, sx, sy, sw, sh, 0))
      MP.mode = i;
    if (MP.mode != i)
      lab_draw_rrect(sx + 1.5f, sy + 1.5f, sw - 3, sh - 3, 6, ui_down(700 + i) ? 0xdfe8e4ffu : 0xf6f2e4ffu);
    lab_draw_text(sx + sw * 0.5f, sy + sh * 0.5f + 5, 13, MP.mode == i ? 0xfff4e6ffu : 0x2f6f73ffu, LAB_CENTER, 1,
                  k_mp_mode[i]);
  }
  if (ui_pressed(HidNpadButton_L | HidNpadButton_ZL) && MP.mode > 0)
    MP.mode--;
  if (ui_pressed(HidNpadButton_R | HidNpadButton_ZR) && MP.mode < 2)
    MP.mode++;
  float ty = sy + sh + 12;
  lab_draw_text_box(bx, ty, iw, 13, 0x202020ffu, LAB_CENTER, 0, body);
  ty += body_h + 12;
  if (local) {
    /* who plays with what */
    for (int i = 0; i < 2; i++) {
      const char *n = lab_input_player_name(i);
      char line[96];
      snprintf(line, sizeof line, "Player %d: %s", i + 1, n ? n : "no controller yet");
      float ly = ty + 10 + 24 * (float)i;
      mp_dot(bx + 9, ly, 8, i);
      text_fit(bx + 24, ly + 5, 13, n ? 0x202020ffu : 0x9a3a2affu, 1, iw - 24, line);
    }
    ty += 52;
    int p2 = lab_input_p2_connected();
    if (ui_button(710, bx, ty, iw, 38, 0)) {
      if (p2)
        mp_go();
      else
        mp_controllers(1); /* the controller screen first */
      return;
    }
    ui_button_box(bx, ty, iw, 38, p2 ? "Start" : "Start: connect player 2", ui_focused(710), ui_down(710));
    float hw = (iw - 10) * 0.5f;
    if (ui_button(711, bx, ty + 48, hw, 38, 0))
      mp_controllers(0);
    ui_button_box(bx, ty + 48, hw, 38, "Controllers", ui_focused(711), ui_down(711));
    if (ui_button(712, bx + hw + 10, ty + 48, hw, 38, 0)) {
      mp_close();
      return;
    }
    ui_button_box(bx + hw + 10, ty + 48, hw, 38, "Cancel", ui_focused(712), ui_down(712));
    ui_default_focus(710);
  } else {
    float hw = (iw - 10) * 0.5f;
    if (ui_button(713, bx, ty, hw, 38, 0)) {
      MP.mode = 2;
      ui_focus(710);
    }
    ui_button_box(bx, ty, hw, 38, "Local play", ui_focused(713), ui_down(713));
    if (ui_button(712, bx + hw + 10, ty, hw, 38, 0)) {
      mp_close();
      return;
    }
    ui_button_box(bx + hw + 10, ty, hw, 38, "Cancel", ui_focused(712), ui_down(712));
    ui_default_focus(713);
  }
  if (ui_back()) {
    mp_close();
    return;
  }
  ui_hint("A", "Select");
  ui_hint("L", "Wi-Fi / Bluetooth");
  ui_hint("R", "Local");
  ui_hint("B", "Cancel");
}

/* ----------------------------------------------------- the race's screen
 * Over the two boards (the menus' surface, see-through): + (or -) from
 * either player pauses; after the pack's last level, again or leave. */
static void vs_quit(void) { lab_versus_quit(); /* ui_pop once it has ended (below) */ }

static int vs_panel(const char *title, const char *sub, const char *const *labels, int n) {
  float cx = ui_canvas_w() * 0.5f, cy = ui_canvas_h() * 0.5f;
  float w = 220, x = cx - w * 0.5f, h = 70 + 48 * (float)n + 6, y = cy - h * 0.5f;
  const LabTex *bg = tex("bg_credits_popup");
  lab_draw_rrect(x - 3, y - 3, w + 6, h + 6, 10, 0x00000060u);
  lab_draw_rrect(x + 2, y + 2, w - 4, h - 4, 8, 0xf6ecd0ffu); /* not see-through over the boards */
  if (bg)
    lab_draw_image(bg, x, y, w, h, WHITE);
  lab_draw_text(cx, y + 30, 17, 0x1e3a3cffu, LAB_CENTER, 1, title);
  lab_draw_text(cx, y + 52, 13, 0x202020ffu, LAB_CENTER, 1, sub);
  int pressed = -1;
  for (int i = 0; i < n; i++) {
    float by = y + 66 + 48 * (float)i;
    if (ui_button(760 + i, x + 16, by, w - 32, 38, 0))
      pressed = i;
    ui_button_box(x + 16, by, w - 32, 38, labels[i], ui_focused(760 + i), ui_down(760 + i));
  }
  lab_draw_opaque(x + 2, y + 2, w - 4, h - 4, 8);
  ui_default_focus(760);
  return pressed;
}

static void versus_frame(void) {
  if (!lab_versus_active()) {
    ui_pop();
    return;
  }
  char sub[64];
  int w0, w1;
  lab_versus_score(&w0, &w1);
  snprintf(sub, sizeof sub, "Player 1  %d - %d  Player 2", w0, w1);
  if (lab_versus_phase_over()) {
    static const char *const labels[2] = {"Play again", "Quit"};
    int b = vs_panel(w0 == w1 ? "A draw!" : w0 > w1 ? "Player 1 won!" : "Player 2 won!", sub, labels, 2);
    if (b == 0)
      lab_versus_restart_pack();
    else if (b == 1 || ui_back())
      vs_quit();
    return;
  }
  int toggle = lab_versus_pause_pressed(ui_pad);
  if (!lab_versus_paused()) {
    if (toggle) {
      lab_versus_set_paused(1);
      ui_focus(760);
      lab_audio_click();
    }
    return;
  }
  /* each player can make the way they hold their controller level
   * (hardware 2026-09-27: a Joy-Con each, none calibrated, both "flat") --
   * from the menu, or their own ZL (a Joy-Con's stick click) */
  static const char *const labels[5] = {"Resume", "Calibrate player 1", "Calibrate player 2", "Restart level",
                                        "Quit"};
  int b = vs_panel("Paused", sub, labels, 5);
  int cal = b == 1 ? 0 : b == 2 ? 1 : -1;
  if (lab_versus_zl_pressed(0))
    cal = 0;
  else if (lab_versus_zl_pressed(1))
    cal = 1;
  if (cal >= 0) {
    if (dcr_config()->tilt == LAB_TILT_STICK) {
      ui_toast("Motion is off: Settings > Tilt with");
    } else {
      lab_input_calibrate_player(cal);
      ui_toast(cal ? "Player 2: the way you hold it is level" : "Player 1: the way you hold it is level");
    }
    lab_audio_click();
  } else if (b == 0 || toggle || ui_back())
    lab_versus_set_paused(0);
  else if (b == 3)
    lab_versus_restart_level();
  else if (b == 4)
    vs_quit();
}

static void packs_frame(void) {
  if (g_packs_focus_want >= 0) {
    /* a tab switch last frame: this frame's rows are the new list's */
    ui_focus(g_packs_focus_want);
    g_packs_focus_want = -1;
  }
  ui_background("bg_light_blue");
  pic("il_logo", (320 - pic_w("il_logo")) * 0.5f, 480 - 68 - pic_h("il_logo"), WHITE);
  const float top = 45 + 36; /* the list's top */
  /* ---- the list ---- */
  static LabPack *rows[3][1024]; /* the table's size */
  int nrows[3];
  for (int g = 0; g < 3; g++)
    nrows[g] = packs_query(g, rows[g]);
  const float gh = 33, rh = 64;
  float content = 0;
  for (int g = 0; g < 3; g++)
    content += gh + (g_group_open[g] ? rh * (float)nrows[g] : 0);
  float view_h = 480 - top;
  if (!MP.on)
    ui_scroll(0, top, 320, view_h, &g_packs_scroll, content - view_h);
  lab_draw_scissor(0, top, 320, view_h);
  ui_clip(0, top, 320, view_h);
  float y = top - g_packs_scroll;
  int any = 0;
  for (int g = 0; g < 3; g++) {
    /* the group row */
    if (PB(200 + g, 0, y, 320, gh, UI_NORING)) {
      g_group_open[g] = !g_group_open[g];
    }
    lab_draw_rect(0, y, 320, gh, k_group_bg[g]);
    if (ui_focused(200 + g))
      lab_draw_image(tex("item_focused"), 0, y, 320, gh, WHITE);
    /* the expander */
    float ax = 16, ay = y + gh * 0.5f;
    if (g_group_open[g]) {
      lab_draw_line(ax - 5, ay - 3, ax, ay + 3, 2.5f, 0xdcd1a8ffu);
      lab_draw_line(ax, ay + 3, ax + 5, ay - 3, 2.5f, 0xdcd1a8ffu);
    } else {
      lab_draw_line(ax - 3, ay - 5, ax + 3, ay, 2.5f, 0xdcd1a8ffu);
      lab_draw_line(ax + 3, ay, ax - 3, ay + 5, 2.5f, 0xdcd1a8ffu);
    }
    lab_draw_text(40, y + 7 + 14, 14, 0xdcd1a8ffu, LAB_LEFT, 1, k_group_name[g]);
    y += gh;
    if (!g_group_open[g])
      continue;
    for (int i = 0; i < nrows[g]; i++, y += rh) {
      LabPack *k = rows[g][i];
      any = 1;
      int id = 1000 + g * 300 + i;
      if (y + rh < top - 3 * rh || y > 480 + 3 * rh)
        continue; /* far off: not registered (the toolkit holds so many) */
      if (y + rh < top || y > 480) {
        /* just off screen: reachable (and pressable) with the controller */
        if (PB(id, 0, y, 320, rh, UI_NORING | UI_NOTOUCH)) {
          g_mp ? mp_open(k) : open_game(k);
          lab_draw_scissor(0, 0, 0, 0);
          return;
        }
        continue;
      }
      uint32_t bg;
      const char *icon, *info;
      diff_style(k, &bg, &icon, &info);
      lab_draw_rect(0, y, 320, rh, bg);
      lab_draw_rect(0, y + rh - 1, 320, 1, 0x00000038u);
      const LabTex *it = tex(icon);
      if (it)
        lab_draw_image(it, 8, y + 4, 56, 56, WHITE);
      float tx = 8 + 56 + 4 + 16;
      text_fit(tx, y + 18, 10, BLACK, 1, 320 - 50 - tx, k->author);
      text_fit(tx, y + 37, 16, BLACK, 1, 320 - 50 - tx - 4, k->name);
      level_dots(tx - 1, y + 40, k->nlevels, k->nfinished, g_mp ? lab_versus_pack_level(k) : k->current, -1,
                 1.33f);
      /* the info button: the right 50 dp (not for Multi player) */
      if (!g_mp && PB(id + 100000, 270, y, 50, rh, UI_NOFOCUS)) {
        g_ui_pack = k;
        ui_push(SCR_INFO);
        lab_draw_scissor(0, 0, 0, 0);
        return;
      }
      float iw = pic_w(info), ih = pic_h(info);
      if (!g_mp)
        pic(info, 270 + (50 - iw) * 0.5f, y + (rh - ih) * 0.5f, ui_down(id + 100000) ? 0xc0c0c0ffu : WHITE);
      if (PB(id, 0, y, g_mp ? 320 : 270, rh, UI_NORING)) {
        g_mp ? mp_open(k) : open_game(k);
        lab_draw_scissor(0, 0, 0, 0);
        return;
      }
      if (ui_focused(id) || ui_down(id))
        lab_draw_image(tex("item_focused"), 0, y, 320, rh, WHITE);
      if (!g_mp && !MP.on && ui_focused(id) && ui_pressed(HidNpadButton_X | HidNpadButton_Y)) {
        g_ui_pack = k;
        lab_audio_click();
        ui_push(SCR_INFO);
        lab_draw_scissor(0, 0, 0, 0);
        return;
      }
    }
  }
  lab_draw_scissor(0, 0, 0, 0);
  ui_clip(0, 0, 0, 0);
  if (!any) {
    const char *msg = g_official
                          ? "No level packs: the APK's levels could not be unpacked (see debug.log)."
                          : "No downloaded level packs yet.\n\nThousands of level packs made by players "
                            "are on the Labyrinth 2 server: see Download levels in the main menu.";
    lab_draw_text_box(24, top + 150, 272, 14, 0x1e3a3cffu, LAB_CENTER, 1, msg);
  }
  /* keep the focused row in view */
  float fx, fy, fw, fh;
  if (!MP.on && ui_focus_rect(&fx, &fy, &fw, &fh) && ui_focus_id() >= 200 && ui_focus_id() < 700) {
    if (fy < top)
      g_packs_scroll -= top - fy;
    else if (fy + fh > 480)
      g_packs_scroll += fy + fh - 480;
    g_packs_scroll = clampf(g_packs_scroll, 0, fmaxf(0, content - view_h));
  }
  /* ---- the navi bar and the tabs, over the list ---- */
  if (g_mp)
    ui_navibar(NULL, "Multi player");
  else
    ui_navibar("head_choose", "Choose levels");
  float tw = 160, th = 160.0f * pic_h("btn_official") / fmaxf(1, pic_w("btn_official"));
  int sw_off = 0, sw_dl = 0;
  if (PB(1, 0, 45, tw, th, 0))
    sw_off = 1;
  if (PB(2, 160, 45, tw, th, 0))
    sw_dl = 1;
  if (!MP.on && ui_pressed(HidNpadButton_L | HidNpadButton_ZL))
    sw_off = 1;
  if (!MP.on && ui_pressed(HidNpadButton_R | HidNpadButton_ZR))
    sw_dl = 1;
  if ((sw_off && !g_official) || (sw_dl && g_official)) {
    g_official = sw_off ? 1 : 0;
    g_packs_scroll = 0;
    /* the focus to the new list's first pack (or the tab, when it is empty) */
    int first = -1;
    for (int g = 0; g < 3 && first < 0; g++)
      if (g_group_open[g] && packs_query(g, rows[g]))
        first = 1000 + g * 300;
    g_packs_focus_want = first >= 0 ? first : (g_official ? 1 : 2);
  }
  const LabTex *t1 = tex(g_official ? "btn_official_selected" : "btn_official");
  const LabTex *t2 = tex(g_official ? "btn_downloaded_big" : "btn_downloaded_selected_big");
  if (t1)
    lab_draw_image(t1, 0, 45, tw, th, WHITE);
  if (t2)
    lab_draw_image(t2, 160, 45, tw, th, WHITE);
  if (MP.on) {
    mp_frame();
    return;
  }
  ui_default_focus(nrows[0] ? 1000 : nrows[1] ? 1300 : nrows[2] ? 1600 : 1);
  if (ui_back())
    ui_pop();
  ui_hint("A", g_mp ? "Choose" : "Play");
  if (!g_mp)
    ui_hint("X", "Level info");
  ui_hint("L", "Official");
  ui_hint("R", "Downloaded");
  ui_hint("B", "Back");
}

/* =========================================================== level info */
#define MAX_THUMBS 64
static struct {
  LabPack *p;
  int preview;        /* a server pack, not downloaded (t in the Java) */
  int b, c, d;        /* selected, levels, finished */
  LabTex *th[MAX_THUMBS];
  uint8_t th_failed[MAX_THUMBS];
  int thumbs_ok;      /* setupThumbnails done */
  int tw, th_h;       /* thumbnail pixels */
} I;

static void info_release(void) {
  for (int i = 0; i < MAX_THUMBS; i++) {
    lab_tex_free(I.th[i]);
    I.th[i] = NULL;
    I.th_failed[i] = 0;
  }
  if (I.thumbs_ok)
    lab_thumbs_release();
  I.thumbs_ok = 0;
}

static void info_enter(void) {
  info_release();
  I.p = g_ui_pack;
  if (!I.p)
    return;
  I.preview = scr_online_is_preview(I.p);
  I.c = I.p->nlevels;
  I.d = I.p->nfinished;
  I.b = I.p->current;
  if (I.b < 0 || I.b >= I.c)
    I.b = I.c > 0 ? I.c - 1 : 0; /* a table with a current level past the end */
  /* 160 x 240 dp, as the phone's (portrait: drawn turned, as the Java did) */
  float ppd = lab_gfx_px_per_dp();
  I.tw = (int)(160 * ppd), I.th_h = (int)(240 * ppd);
  I.thumbs_ok = lab_thumbs_setup(I.p, I.tw, I.th_h) == 0;
  lab_ui_side_info(NULL, NULL, NULL);
}

static void info_delete_answer(int yes) {
  if (!yes || !I.p)
    return;
  info_release();
  lab_files_delete_pack(I.p->id);
  I.p = g_ui_pack = NULL;
  ui_pop();
}

static void info_select(int b) {
  if (b < 0 || b >= I.c)
    return;
  I.b = b;
  /* b(): a level up to the ones finished becomes the current one */
  if (I.b <= I.d && I.p && !I.preview) {
    I.p->current = I.b;
    lab_levels_save();
  }
}

static void info_frame(void) {
  if (!I.p) {
    ui_pop();
    return;
  }
  ui_background("bg_light_blue");
  ui_navibar("head_levelinfo", "Level info");
  uint32_t bgc;
  const char *icon, *info;
  diff_style(I.p, &bgc, &icon, &info);
  if (!I.p->published && I.p->ownlevel)
    icon = "icon_unpublished_transp";
  const LabTex *it = tex(icon);
  if (it)
    lab_draw_image(it, 16, 45 + 16, 56, 56, WHITE);
  text_fit(16 + 56 + 8, 45 + 36 + 16, 16, BLACK, 1, 240, I.p->name);
  /* the picture: 288 x 192 dp, arrows beside it */
  const float ix = 16, iy = 45 + 90, iw = 288, ih = 192;
  if (I.b < MAX_THUMBS && !I.th[I.b] && !I.th_failed[I.b] && I.thumbs_ok) {
    I.th[I.b] = lab_thumbs_render(I.b);
    I.th_failed[I.b] = !I.th[I.b]; /* the frame is shown instead; no retry every frame */
  }
  LabTex *th = I.b < MAX_THUMBS ? I.th[I.b] : NULL;
  if (th) {
    /* the portrait picture turned a quarter (readBitmapBufferRot90), its
     * rows bottom-up (an FBO): corners of the landscape box */
    const float xy[8] = {ix, iy, ix + iw, iy, ix, iy + ih, ix + iw, iy + ih};
    const float uv[8] = {0, 0, 0, 1, 1, 0, 1, 1};
    lab_draw_quad_raw(th, xy, uv, WHITE, 0);
    lab_draw_frame(ix, iy, iw, ih, 1, 0x00000060u);
  } else {
    const LabTex *f = tex("thumbframe");
    if (f)
      lab_draw_image(f, ix, iy, iw, ih, WHITE);
  }
  int prev = 0, next = 0;
  float aw = pic_w("info_arrow_prev"), ah = pic_h("info_arrow_prev");
  if (I.b > 0) {
    if (ui_button(3, 0, iy + (ih - ah) * 0.5f - 8, 16 + 4, ah + 16, UI_NOFOCUS))
      prev = 1;
    pic("info_arrow_prev", (16 - aw) * 0.5f, iy + (ih - ah) * 0.5f, WHITE);
  }
  if (I.b + 1 < I.c) {
    if (ui_button(4, 300, iy + (ih - ah) * 0.5f - 8, 20, ah + 16, UI_NOFOCUS))
      next = 1;
    pic("info_arrow_next", 304 + (16 - aw) * 0.5f, iy + (ih - ah) * 0.5f, WHITE);
  }
  /* a swipe or a tap on the picture's sides also turns it */
  if (ui_pressed(HidNpadButton_Left | HidNpadButton_ZL | HidNpadButton_L | HidNpadButton_StickLLeft))
    prev = 1;
  if (ui_pressed(HidNpadButton_Right | HidNpadButton_ZR | HidNpadButton_R | HidNpadButton_StickLRight))
    next = 1;
  if (prev && I.b > 0) {
    if (!(ui_pad->down & HidNpadButton_A))
      lab_audio_click();
    info_select(I.b - 1);
  }
  if (next && I.b + 1 < I.c) {
    lab_audio_click();
    info_select(I.b + 1);
  }
  /* the dots: padding 10 * density px -> 10 dp between */
  float dot_e = pic_w("info_dot_white") + 10.0f;
  float dw = dot_e * (float)I.c;
  if (dw > 300) {
    dot_e = 300.0f / (float)I.c;
    dw = 300;
  }
  {
    float x = (320 - dw) * 0.5f, y = iy + ih + 8;
    for (int i = 0; i < I.c; i++)
      pic(i < I.d ? "info_dot_green" : "info_dot_white", x + dot_e * (float)i + 5, y, WHITE);
    pic("info_dot_selected", x + dot_e * (float)I.b + 5, y, WHITE);
  }
  /* the times */
  char best[48], des[48];
  int bt = lab_levels_best_time(I.p->id, I.b);
  int dt = I.preview ? scr_online_preview_time(I.b) : lab_levels_designer_time(I.p->id, I.b);
  fmt_time(best, sizeof best, bt);
  /* your own pack not published yet: your times will be its designer times */
  if (I.p->ownlevel && !I.p->published)
    dt = bt;
  fmt_time(des, sizeof des, dt);
  float ty = iy + ih + 44;
  lab_draw_text(90, ty, 12, BLACK, LAB_CENTER, 1, "Your best time");
  lab_draw_text(90, ty + 16, 12, BLACK, LAB_CENTER, 1, bt ? best : "-");
  lab_draw_text(230, ty, 12, BLACK, LAB_CENTER, 1, "Designer time");
  lab_draw_text(230, ty + 16, 12, BLACK, LAB_CENTER, 1, dt ? des : "-");
  /* the buttons (LevelPackInfoActivityFull.b()), or "complete the previous
   * levels" */
  float pw = pic_w("btn_play_medium"), ph = pic_h("btn_play_medium");
  float px = (320 - pw) * 0.5f, py = 480 - 10 - ph;
  /* the half-width pair: 140 dp each, 5 dp apart */
  float sw = pic_w("btn_small_play"), sh = pic_h("btn_small_play");
  float sx0 = (320 - (2 * sw + 5)) * 0.5f, sx1 = sx0 + sw + 5, sy = 480 - 10 - sh;
  if (I.preview) {
    /* InfoDownload: every level can be looked at before (the phone only
     * showed it on the first) */
    float dw = pic_w("btn_download_medium"), dh = pic_h("btn_download_medium");
    float dx = (320 - dw) * 0.5f, dy = 480 - 10 - dh;
    if (ui_button(6, dx, dy, dw, dh, 0)) {
      LabPack *k = scr_online_keep_preview();
      if (k) {
        I.p = g_ui_pack = k;
        I.preview = 0;
      }
    }
    pic(ui_down(6) ? "btn_download_pressed" : "btn_download_medium", dx, dy, WHITE);
    ui_default_focus(6);
  } else if (I.b <= I.d && I.p->preloaded) {
    if (ui_button(5, px, py, pw, ph, 0)) {
      open_game(I.p);
      return;
    }
    pic(ui_down(5) ? "btn_play_medium_down" : "btn_play_medium", px, py, WHITE);
  } else if (I.b <= I.d) {
    int own = I.p->ownlevel && !I.p->published;
    if (own) {
      if (ui_button(7, sx0, sy, sw, sh, 0) && !scr_online_publishing())
        scr_online_publish(I.p);
      pic(ui_down(7) ? "btn_publish_down" : "btn_publish_up", sx0, sy, WHITE);
    } else {
      if (ui_button(8, sx0, sy, sw, sh, 0))
        ui_confirm("Delete Level Pack?", "Are you sure you want to delete the level pack?", "OK", "Cancel",
                   info_delete_answer);
      pic(ui_down(8) ? "btn_small_delete_down" : "btn_small_delete", sx0, sy, WHITE);
    }
    if (ui_button(5, sx1, sy, sw, sh, 0) && !scr_online_publishing()) {
      open_game(I.p);
      return;
    }
    pic(ui_down(5) ? "btn_small_play_down" : "btn_small_play", sx1, sy, WHITE);
    if (scr_online_publishing()) {
      lab_draw_rect(0, 0, 320, 480, 0x00000060u);
      scr_spinner(160, 240, 18);
    }
  } else {
    const char *msg = "Complete the previous levels to play this one.";
    float w = lab_text_width(12, 1, msg) + 28;
    float x = (320 - w) * 0.5f;
    pic("exclamation_mark", x, 480 - 20 - 16, WHITE);
    lab_draw_text(x + 20, 480 - 20 - 4, 12, BLACK, LAB_LEFT, 1, msg);
  }
  ui_default_focus(5);
  if (ui_back()) {
    info_release();
    ui_pop();
    return;
  }
  ui_hint("A", I.preview ? "Select" : "Play");
  ui_hint("LS", "Previous / next level");
  ui_hint("B", "Back");
}

/* ============================================================ the game */
static int g_game_from;

static void open_game(LabPack *p) {
  if (!p->preloaded) {
    /* a pack from the server or the card: complete (info.xml) and whole
     * (every level) before the engine opens it -- it does not check */
    lab_files_complete_pack(p);
    char why[160], msg[256];
    if (lab_files_check_pack(p, why, sizeof why)) {
      snprintf(msg, sizeof msg, "%s\n\n%s", why,
               p->ownlevel ? "Refresh it on the Create screen." : "Delete it and download it again.");
      ui_confirm("Can't play this level pack", msg, "OK", NULL, NULL);
      return;
    }
  }
  g_ui_pack = p;
  g_game_from = ui_top();
  if (g_game_from == SCR_INFO) {
    /* the thumbnails' drawing state must not outlive into the game */
    info_release();
  }
  if (lab_game_start(p) != 0) {
    ui_toast("The game could not start: see debug.log");
    return;
  }
  ui_push(SCR_GAME);
}

void scr_game_ended(void) {
  if (ui_top() == SCR_GAME)
    ui_pop(); /* back to the list or the info (whose enter makes its pictures again) */
}

static void game_frame(void) {
  /* the side panel: what is being played */
  if (g_ui_pack) {
    char sub[64], sub2[96], t[32];
    snprintf(sub, sizeof sub, "Level %d of %d", g_ui_pack->current + 1, g_ui_pack->nlevels);
    int dt = lab_levels_designer_time(g_ui_pack->id, g_ui_pack->current);
    int bt = lab_levels_best_time(g_ui_pack->id, g_ui_pack->current);
    sub2[0] = 0;
    if (bt) {
      fmt_time(t, sizeof t, bt);
      snprintf(sub2, sizeof sub2, "Your best: %s", t);
    } else if (dt) {
      fmt_time(t, sizeof t, dt);
      snprintf(sub2, sizeof sub2, "Designer: %s", t);
    }
    lab_ui_side_info(g_ui_pack->name, sub, sub2);
  }
}

/* ============================================================= settings */
static int g_settings_over_game;
static float g_cal_k, g_cal_l, g_tilt_o, g_tilt_p;

static void settings_enter(void) {
  g_settings_over_game = lab_game_active();
  lab_ui_side_info(NULL, NULL, NULL);
}

int scr_settings_over_game(void) { return g_settings_over_game; }
void scr_settings_enter(void) { g_settings_over_game = lab_game_active(); }
void scr_settings_back(void);
void scr_settings_back(void) {
  lab_reg_save();
  ui_pop();
  if (g_settings_over_game && ui_top() == SCR_GAME)
    lab_game_resume_after_settings();
}

static void settings_back(void) {
  lab_reg_save();
  ui_pop();
  if (g_settings_over_game && ui_top() == SCR_GAME)
    lab_game_resume_after_settings();
}

static void settings_frame(void) {
  ui_background("bg_light_blue");
  pic("il_logo", (320 - pic_w("il_logo")) * 0.5f, 480 - 68 - pic_h("il_logo"), WHITE);
  ui_navibar("head_settings", "Settings");
  float tilt[3];
  lab_input_tilt(ui_pad, tilt);
  const float rh = 71.0f;
  float y = 45;
  static const char *const tilt_names[3] = {"Stick", "Motion", "Both"};
  /* the three rows of the phone's Settings, then the port's */
  enum { R_CAL, R_SOUND, R_TILTVIEW, R_GHOST, R_TILTWITH, R_CREDITS, R_N };
  for (int r = 0; r < R_N; r++, y += rh) {
    const char *bg = r % 2 ? "bg_settings_sound" : "bg_settings_calib";
    const LabTex *bt = tex(bg);
    if (bt)
      lab_draw_image(bt, 0, y, 320, rh, WHITE);
    static const char *const icons[R_N] = {"icon_calibrate", "icon_sound", "icon_tilt", NULL, "icon_tilt", NULL};
    static const char *const labels[R_N] = {"Calibrate", "Sound FX", "Tilt view", "Ghost ball", "Tilt with", "Credits"};
    if (icons[r])
      pic(icons[r], 9, y + 6, WHITE);
    else if (r == R_GHOST) {
      /* the steel ball, without its "STEEL" caption (its top fifth), faded
       * like a ghost */
      const LabTex *b = tex("ball_steel");
      if (b)
        lab_draw_image_uv(b, 14, y + 8, 52, 52 * 0.8f * lab_tex_dp_h(b) / fmaxf(1, lab_tex_dp_w(b)), 0, 0.2f, 1, 1,
                          0xffffff90u);
    }
    lab_draw_text(80, y + rh * 0.5f + 7, 20, BLACK, LAB_LEFT, 1, labels[r]);
    float bx = 320 - 16 - 80, bh = 44, by = y + (rh - bh) * 0.5f;
    int id = 10 + r;
    int hit = ui_button(id, bx, by, 80, bh, 0);
    int on = 0, toggle = 0;
    const char *btext = "SET";
    if (r == R_CAL) {
      if (hit) {
        lab_input_calibrate();
        ui_toast(dcr_config()->tilt == LAB_TILT_STICK ? "Level position kept (motion is off: ZL in a level)"
                                                      : "Calibrated: this position is level (kept)");
      }
      /* CalibrateIcon: the cross follows the tilt */
      g_cal_k = g_cal_k * 0.8f - 0.2f * tilt[0] * 36.0f;
      g_cal_l = g_cal_l * 0.8f + 0.2f * tilt[1] * 36.0f;
      float cw = pic_w("calibrate_cross");
      float cx = 19 + (54 - cw) * 0.5f + clampf(-g_cal_k, -17, 17), cy = y + 7 + (54 - cw) * 0.5f + clampf(-g_cal_l, -17, 17);
      pic("calibrate_cross", cx, cy, WHITE);
    } else if (r == R_SOUND) {
      on = lab_reg_get_int("setting-sound-effects", 1) == 1;
      toggle = 1;
      btext = on ? "ON" : "OFF";
      if (hit) {
        on = !on;
        lab_reg_set_int("setting-sound-effects", on);
        lab_audio_enable(on);
        lab_audio_click();
      }
    } else if (r == R_TILTVIEW) {
      on = lab_reg_get_int("setting-acc-perspective", 1) == 1;
      toggle = 1;
      btext = on ? "ON" : "OFF";
      if (hit) {
        on = !on;
        lab_reg_set_int("setting-acc-perspective", on);
        lab_game_set_tilt_view(on);
      }
      /* TiltIcon: a box in perspective that leans with the board */
      float ox = 19 + 7, oy = y + 7 + 7, s = 40;
      float fx = 19 + (54 - 25) * 0.5f, fy = y + 7 + (54 - 25) * 0.5f;
      if (on) {
        g_tilt_o = g_tilt_o * 0.7f + tilt[0] * 0.3f;
        g_tilt_p = g_tilt_p * 0.7f + tilt[1] * 0.3f;
        fx = clampf(fx - g_tilt_o * 27, ox, ox + s - 25);
        fy = clampf(fy - g_tilt_p * 27, oy, oy + s - 25);
      }
      uint32_t grey = 0xccccccffu;
      lab_draw_line(fx, fy, ox, oy, 2, grey);
      lab_draw_line(fx + 25, fy, ox + s, oy, 2, grey);
      lab_draw_line(fx, fy + 25, ox, oy + s, 2, grey);
      lab_draw_line(fx + 25, fy + 25, ox + s, oy + s, 2, grey);
      lab_draw_frame(fx, fy, 25, 25, 2, grey);
      lab_draw_frame(ox, oy, s, s, 2, WHITE);
    } else if (r == R_GHOST) {
      /* the engine's own option (a replay of your best run), which the
       * phone's Settings never showed: on by default */
      on = lab_reg_get_int("setting-ghostball", 1) == 1;
      toggle = 1;
      btext = on ? "ON" : "OFF";
      if (hit) {
        on = !on;
        lab_reg_set_int("setting-ghostball", on);
      }
      text_fit(80, y + rh * 0.5f + 24, 10, 0x404040ffu, 1, bx - 8 - 80, "your best run, replayed");
    } else if (r == R_TILTWITH) {
      int tw = dcr_config()->tilt;
      btext = tilt_names[tw];
      if (hit) {
        tw = (tw + 1) % 3;
        dcr_config_set_tilt(tw);
        lab_reg_set_int("port-tilt", tw);
      }
      text_fit(80, y + rh * 0.5f + 24, 10, 0x404040ffu, 1, bx - 8 - 80,
               tw == LAB_TILT_STICK ? "the left stick" : tw == LAB_TILT_MOTION ? "the controller's motion" : "motion and the stick");
    } else {
      btext = "Show";
      if (hit) {
        ui_push(SCR_CREDITS);
        return;
      }
    }
    android_button(bx, by, 80, bh, btext, ui_down(id), toggle, on);
  }
  ui_default_focus(10);
  if (ui_back() || ui_pressed(HidNpadButton_Plus)) {
    settings_back();
    return;
  }
  ui_hint("A", "Change");
  ui_hint("B", g_settings_over_game ? "Back to the game" : "Back");
}

/* =============================================================== awards */
typedef void *(*fn_arr)(void *env, void *cls);
typedef void *(*fn_str_i)(void *env, void *cls, jint i);
typedef jfloat (*fn_f_i)(void *env, void *cls, jint i);
typedef jboolean (*fn_z_i)(void *env, void *cls, jint i);
typedef jint (*fn_i0)(void *env, void *cls);
typedef jfloat (*fn_f0)(void *env, void *cls);

static ScrAwards A;
const ScrAwards *scr_awards(void) { return &A; }
void scr_awards_load(void);

static void *award_fn(const char *name) {
  char sym[160];
  snprintf(sym, sizeof sym, "Java_se_illusionlabs_labyrinth2_managers_%s", name);
  return lab_native(sym);
}

static void awards_enter(void) {
  lab_ui_side_info(NULL, NULL, NULL);
  scr_awards_load();
}

/* the award list and the statistics, from the engine's managers */
void scr_awards_load(void) {
  void *env = g_jni_env;
  void *am = jni_class("se/illusionlabs/labyrinth2/managers/AwardManager")->obj;
  void *sm = jni_class("se/illusionlabs/labyrinth2/managers/StatisticsManager")->obj;
  fn_arr arr = award_fn("AwardManager_getAwardArray");
  fn_str_i nm = award_fn("AwardManager_getAwardName"), ds = award_fn("AwardManager_getAwardDesc"),
           aid = award_fn("AwardManager_getAwardId");
  fn_f_i pr = award_fn("AwardManager_getAwardProgress");
  fn_z_i ach = award_fn("AwardManager_getAwardAchieved"), ball = award_fn("AwardManager_isBallUnlocked");
  fn_i0 cnt = award_fn("StatisticsManager_getNbrAwards"), tp = award_fn("StatisticsManager_getTimePlayed");
  fn_f0 dist = award_fn("StatisticsManager_getBallDistance");
  A.n = 0;
  if (arr) {
    JObj *a = arr(env, am);
    if (a && a->kind == JK_ARRAY) {
      const int32_t *v = a->a.data;
      for (int i = 0; i < a->a.len && A.n < MAX_AWARDS; i++)
        A.idx[A.n++] = v[i];
    }
    jni_release(a);
  }
  for (int i = 0; i < A.n; i++) {
    int k = A.idx[i];
    JObj *s;
    s = nm ? nm(env, am, k) : NULL;
    snprintf(A.name[i], sizeof A.name[i], "%s", jni_utf(s));
    jni_release(s);
    s = ds ? ds(env, am, k) : NULL;
    snprintf(A.desc[i], sizeof A.desc[i], "%s", jni_utf(s));
    jni_release(s);
    s = aid ? aid(env, am, k) : NULL;
    snprintf(A.id[i], sizeof A.id[i], "%s", jni_utf(s));
    jni_release(s);
    A.prog[i] = pr ? pr(env, am, k) : 0;
    A.got[i] = ach ? ach(env, am, k) : 0;
  }
  for (int b = 0; b < 3; b++)
    A.balls[b] = ball ? ball(env, am, b) : 0;
  A.count = cnt ? cnt(env, sm) : 0;
  A.time_played = tp ? tp(env, sm) : 0;
  A.distance = dist ? dist(env, sm) : 0;
  debugPrintf("[awards] %d awards (%d won), time %d s, distance %.1f m, balls %d%d%d\n", A.n, A.count,
              A.time_played, (double)A.distance, A.balls[0], A.balls[1], A.balls[2]);
}

const char *scr_award_icon(const char *id);
const char *scr_award_icon(const char *id) {
  static const char *const map[][2] = {
      {"tutorial", "award_icon_tutorial"}, {"preloaded", "award_icon_preloaded"},
      {"distance-01", "award_icon_distance_1"}, {"distance-02", "award_icon_distance_2"},
      {"distance-03", "award_icon_distance_3"}, {"completed-easy-01", "award_icon_completed_easy_1"},
      {"completed-easy-02", "award_icon_completed_easy_2"}, {"completed-medium-01", "award_icon_completed_medium"},
      {"completed-medium-02", "award_icon_completed_medium"}, {"completed-hard-01", "award_icon_completed_hard"},
      {"completed-hard-02", "award_icon_completed_hard"}, {"completed-clean-holes-01", "award_icon_clean_holes_1"},
      {"completed-clean-holes-02", "award_icon_clean_holes_2"}, {"completed-clean-all-01", "award_icon_clean_all_1"},
      {"completed-clean-all-02", "award_icon_clean_all_2"}, {"time-under-par-01", "award_icon_time_par_1"},
      {"time-under-par-02", "award_icon_time_par_2"}, {"time-under-par-03", "award_icon_time_par_3"},
      {"time-total-01", "award_icon_time_total_1"}, {"time-total-02", "award_icon_time_total_2"},
      {"time-total-03", "award_icon_time_total_3"}, {"bumper-hit-01", "award_icon_bumper"},
      {"holes-01", "award_icon_holes"}, {"cannon-kills-01", "award_icon_cannon"},
      {"milestone-01", "award_icon_milestone_1"}, {"milestone-02", "award_icon_milestone_2"},
      {"milestone-03", "award_icon_milestone_3"}};
  for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
    if (!strcmp(map[i][0], id))
      return map[i][1];
  return NULL;
}

static void awards_frame(void) {
  lab_draw_rect(0, 0, 320, 480, 0xe8e8e0ffu);
  /* the list: below the 162 dp pane */
  const float top = 45 + 162 + pic_h("line_awards"), rh = 64;
  float content = rh * (float)A.n, view_h = 480 - top;
  ui_scroll(0, top, 320, view_h, &A.scroll, content - view_h);
  lab_draw_scissor(0, top, 320, view_h);
  ui_clip(0, top, 320, view_h);
  for (int i = 0; i < A.n; i++) {
    float y = top + rh * (float)i - A.scroll;
    int id = 300 + i;
    ui_button(id, 0, y, 320, rh, UI_NORING);
    if (y + rh < top || y > 480)
      continue;
    lab_draw_rect(0, y, 320, rh, A.got[i] ? 0xd8e394ffu : 0xd6d8d8ffu);
    lab_draw_rect(0, y + rh - 1, 320, 1, 0x00000030u);
    const char *icon = A.got[i] ? scr_award_icon(A.id[i]) : "award_icon_notachieved";
    const LabTex *it = icon ? tex(icon) : NULL;
    if (it)
      lab_draw_image(it, 10, y + 6, 52, 52, WHITE);
    float tx = 10 + 52 + 8;
    text_fit(tx, y + 22, 16, BLACK, 1, 320 - tx - 6, A.name[i]);
    text_fit(tx, y + 38, 12, 0x696969ffu, 1, 320 - tx - 6, A.desc[i]);
    /* ProgressBarView: 244 x 14 dp, the percentage after it */
    float pw = 244 - 40, px = tx, py = y + 44, g = 2;
    uint32_t bgc = A.got[i] ? 0x70754affu : 0xa6a394ffu, fgc = A.got[i] ? 0x8fab30ffu : 0x549ea6ffu;
    lab_draw_rect(px, py, pw, 14, bgc);
    lab_draw_rect(px + g, py + g, clampf(A.prog[i], 0, 1) * (pw - g), 14 - 2 * g, fgc);
    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", (int)(100.0f * A.prog[i]));
    lab_draw_text(px + pw + 6, py + 12, 12, 0x696969ffu, LAB_LEFT, 1, pct);
    if (ui_focused(id))
      lab_draw_image(tex("item_focused"), 0, y, 320, rh, WHITE);
  }
  lab_draw_scissor(0, 0, 0, 0);
  ui_clip(0, 0, 0, 0);
  float fx, fy, fw, fh;
  if (ui_focus_rect(&fx, &fy, &fw, &fh) && ui_focus_id() >= 300) {
    if (fy < top)
      A.scroll -= top - fy;
    else if (fy + fh > 480)
      A.scroll += fy + fh - 480;
    A.scroll = clampf(A.scroll, 0, fmaxf(0, content - view_h));
  }
  /* the pane */
  const LabTex *bg = tex("bg_light_blue");
  if (bg)
    lab_draw_image_uv(bg, 0, 45, 320, 162, 0, 0, 1, 162.0f / 480.0f, WHITE);
  const LabTex *line = tex("line_awards");
  if (line)
    lab_draw_image(line, 0, 45 + 162, 320, lab_tex_dp_h(line), WHITE);
  ui_navibar("head_awards", "Awards");
  int sel = lab_reg_get_int("setting-ball-selected", 0);
  static const char *const names[4] = {"steel", "bronze", "silver", "gold"};
  float bw = pic_w("ball_steel"), x = (320 - (6 + 4 * bw + 12)) * 0.5f + 6;
  for (int b = 0; b < 4; b++) {
    int unlocked = b == 0 || A.balls[b - 1];
    char n[48];
    snprintf(n, sizeof n, "ball_%s%s", names[b], !unlocked ? "_unachived" : sel == b ? "_highlight" : "");
    float bx = x + (float)b * (bw + 4), by = 45 + 6;
    if (ui_button(20 + b, bx, by, bw, pic_h(n), UI_ROUND) && unlocked) {
      lab_reg_set_int("setting-ball-selected", b);
      lab_reg_save();
    }
    pic(n, bx, by, WHITE);
  }
  char s1[64], s2[96], s3[96];
  snprintf(s1, sizeof s1, "Number of awards: %d", A.count);
  {
    int t = A.time_played, m = t / 60, h = m / 60;
    char buf[96] = "Time played: ";
    if (h > 0) {
      snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " %d h", h);
      m -= h * 60;
      t -= h * 3600;
    }
    if (m > 0) {
      snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " %d m", m);
      t -= m * 60;
    }
    if (t > 0 || (h <= 0 && m <= 0))
      snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " %d s", t);
    snprintf(s2, sizeof s2, "%s", buf);
  }
  {
    float d = A.distance;
    char b[64] = "";
    if (d > 1000.0f) {
      int km = (int)(d / 1000.0f);
      snprintf(b, sizeof b, "%d km, ", km);
      d -= (float)(km * 1000);
    }
    char m[32];
    snprintf(m, sizeof m, "%.2f", (double)d);
    char *e = m + strlen(m) - 1;
    while (e > m && *e == '0')
      *e-- = 0;
    if (*e == '.')
      *e = 0;
    snprintf(s3, sizeof s3, "Distance rolled: %s%s m", b, m);
  }
  float lx = (320 - (6 + 4 * bw + 12)) * 0.5f + 12;
  lab_draw_text(lx, 45 + 160 - 8 - 34, 12, 0x101010ffu, LAB_LEFT, 1, s1);
  lab_draw_text(lx, 45 + 160 - 8 - 17, 12, 0x101010ffu, LAB_LEFT, 1, s2);
  lab_draw_text(lx, 45 + 160 - 8, 12, 0x101010ffu, LAB_LEFT, 1, s3);
  ui_default_focus(20 + sel);
  if (ui_back())
    ui_pop();
  ui_hint("A", "Choose the ball");
  ui_hint("RS", "Scroll");
  ui_hint("B", "Back");
}

/* ============================================================== credits */
const char *const k_credits[4][2] = {
    {"Development: ", "Andreas Alptun, Carl Loodberg, Jim Winberg, Marcus Andersson, Olof Hedman."},
    {"Art Direction: ", "Mirabelle Looft"},
    {"3D Graphics: ", "Christoffer Lindwall"},
    {"Nintendo Switch port: ", "aks796"},
};

static void credits_frame(void) {
  const LabTex *bg = tex("bg_credits");
  if (bg)
    lab_draw_image(bg, 0, 45, 320, 435, WHITE);
  ui_navibar("head_credits", "Credits");
  const float x = 15, w = 290;
  const char *const(*lines)[2] = k_credits;
  float y = 480 - 35 - 150;
  const LabTex *pop = tex("bg_credits_popup");
  if (pop)
    lab_draw_image(pop, x, y, w, 150, WHITE);
  float ty = y + 10;
  for (unsigned i = 0; i < 4; i++) {
    char buf[192];
    snprintf(buf, sizeof buf, "%s%s", lines[i][0], lines[i][1]);
    ty += lab_draw_text_box(x + 10, ty, w - 20, 12, BLACK, LAB_LEFT, 0, buf) + 8;
  }
  if (ui_back() || ui_pressed(HidNpadButton_A))
    ui_pop();
  ui_hint("B", "Back");
}

/* ========================================== pieces for the online screens */
void scr_pic(const char *name, float x, float y, uint32_t rgba) { pic(name, x, y, rgba); }
float scr_pic_w(const char *name) { return pic_w(name); }
float scr_pic_h(const char *name) { return pic_h(name); }
void scr_text_fit(float x, float y, float size, uint32_t rgba, int bold, float w, const char *s) {
  text_fit(x, y, size, rgba, bold, w, s);
}
void scr_android_button(float x, float y, float w, float h, const char *label, int down, int toggle, int on) {
  android_button(x, y, w, h, label, down, toggle, on);
}
void scr_diff_style(const LabPack *k, uint32_t *bg, const char **icon, const char **info) {
  diff_style(k, bg, icon, info);
}
void scr_level_dots(float x, float y, int n, int finished, int current, int selected, float pad) {
  level_dots(x, y, n, finished, current, selected, pad);
}
void scr_open_game(LabPack *p) { open_game(p); }

/* Android's indeterminate ProgressBar: a ring of dots going round */
void scr_spinner(float cx, float cy, float r) {
  float t = ui_time() * 1.4f;
  for (int i = 0; i < 10; i++) {
    float a = (float)i / 10.0f * 6.2831853f;
    float k = fmodf(t - (float)i / 10.0f + 10.0f, 1.0f); /* 0: the head */
    uint32_t al = (uint32_t)(255.0f * (0.25f + 0.75f * (1.0f - k)));
    float d = r * 0.22f;
    lab_draw_rrect(cx + sinf(a) * r - d, cy - cosf(a) * r - d, 2 * d, 2 * d, d, 0xffffff00u | al);
  }
}

/* ============================================================ dispatch */
void scr_enter(int s) {
  if (lab_hd_on()) {
    hd_enter(s);
    return;
  }
  scr_enter_android(s);
}

void scr_enter_android(int s) {
  switch (s) {
  case SCR_SPLASH: g_splash_t0 = ui_time(); break;
  case SCR_MAIN: mm_enter(); break;
  case SCR_PACKS: packs_enter(); break;
  case SCR_INFO: info_enter(); break;
  case SCR_SETTINGS: settings_enter(); break;
  case SCR_AWARDS: awards_enter(); break;
  case SCR_DOWNLOAD: scr_download_enter(); break;
  case SCR_CREATE: scr_create_enter(); break;
  case SCR_HOWTO: scr_howto_enter(); break;
  default: break;
  }
}

void scr_frame(int s) {
  if (lab_hd_on()) {
    hd_frame(s);
    return;
  }
  scr_frame_android(s);
}

void scr_frame_android(int s) {
  switch (s) {
  case SCR_SPLASH: splash_frame(); break;
  case SCR_MAIN: mm_frame(); break;
  case SCR_PACKS: packs_frame(); break;
  case SCR_INFO: info_frame(); break;
  case SCR_SETTINGS: settings_frame(); break;
  case SCR_AWARDS: awards_frame(); break;
  case SCR_CREDITS: credits_frame(); break;
  case SCR_CREATE: scr_create_frame(); break;
  case SCR_DOWNLOAD: scr_download_frame(); break;
  case SCR_HOWTO: scr_howto_frame(); break;
  case SCR_GAME: game_frame(); break;
  case SCR_VERSUS: versus_frame(); break;
  default: break;
  }
}
