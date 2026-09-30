/* lab_ui.c -- the toolkit the rebuilt screens are made with, the screen
 * stack, the side panels, and the controls over a running game.
 *
 * FOCUS. Every frame a screen registers its buttons (ui_button, dp rects as
 * drawn). The focus moves spatially: from the focused button's centre, the
 * nearest button in the direction pressed (distance along it + twice the
 * distance across), with the D-pad or the left stick (repeating while
 * held). A presses the focused button; B is the screen's back. The ring is
 * hidden after the touchscreen is used, until a button is. A finger presses
 * a button when it lifts over the one it went down on (Android's click).
 *
 * OVER A GAME. The engine's own screens in a level (the pause overlay, the
 * level's end, the rating) are touch screens: the touchscreen goes straight
 * to the engine, and a pointer (the right stick, or the left stick while
 * the game is paused) with A as the finger reaches them from a controller.
 * + and B open/close the pause overlay (Android's Back/Menu), - sets the
 * motion neutral. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "lab.h"
#include "lab_gl.h"
#include "lab_ui.h"
#include "util.h"

const LabPad *ui_pad;
static const LabPad g_zero_pad; /* no input at all (a frame without the pad) */
static LabPad g_nopad;          /* this frame's pad with the first A taken out */

/* ------------------------------------------------------------ items */
typedef struct {
  int id, flags;
  float x, y, w, h;
  float cx, cy, cw, ch; /* touch clip */
} Item;
#define MAXI 400
static Item g_items[MAXI];
static int g_nitems;
static int g_focus = -1, g_default = -1;
static int g_touch_mode;   /* the ring is hidden (the touchscreen was used) */
static int g_touch_id = -1; /* the button the finger went down on */
static int g_touch_cancel;
static float g_touch_x0, g_touch_y0, g_touch_x, g_touch_y;
static float g_clip[4];
static int g_modal;        /* a dialog is up: the screen's buttons are dead */
static int g_changed;      /* the screen changed during this frame: its items are the old screen's */
static u64 g_t0;
/* the canvas the screens are drawn on: the phone's 320 x 480 dp, or the
 * iPad menus' points (HD_CANVAS_H tall, as wide as the screen is: lab_hd*.c) */
static float g_cw = 320, g_ch = 480;
static int g_hold_nav; /* this frame the screen took the D-pad's left / right (1) or all (2) */
void ui_hold_nav(int all) { g_hold_nav = all ? 2 : 1; }
static float g_px_per_unit = 3;

float ui_canvas_w(void) { return g_cw; }
float ui_canvas_h(void) { return g_ch; }
float ui_px_per_unit(void) { return g_px_per_unit; }

float ui_time(void) { return (float)armTicksToNs(armGetSystemTick() - g_t0) * 1e-9f; }

static int in_rect(float px, float py, float x, float y, float w, float h) {
  return px >= x && py >= y && px < x + w && py < y + h;
}

void ui_clip(float x, float y, float w, float h) {
  g_clip[0] = x, g_clip[1] = y, g_clip[2] = w, g_clip[3] = h;
}

int ui_button(int id, float x, float y, float w, float h, int flags) {
  if (g_nitems < MAXI)
    g_items[g_nitems++] = (Item){id, flags, x, y, w, h, g_clip[0], g_clip[1], g_clip[2], g_clip[3]};
  if (g_modal)
    return 0;
  const LabPad *p = ui_pad;
  int hit = 0;
  int inclip = g_clip[2] <= 0 || in_rect(g_touch_x, g_touch_y, g_clip[0], g_clip[1], g_clip[2], g_clip[3]);
  if (!(flags & UI_NOTOUCH) && p->touch_began && p->touch_in && in_rect(p->tx, p->ty, x, y, w, h) &&
      (g_clip[2] <= 0 || in_rect(p->tx, p->ty, g_clip[0], g_clip[1], g_clip[2], g_clip[3])))
    g_touch_id = id;
  if (p->touch_ended && g_touch_id == id && !g_touch_cancel && inclip &&
      in_rect(g_touch_x, g_touch_y, x, y, w, h))
    hit = 1;
  if (!(flags & UI_NOFOCUS) && g_focus == id && (p->down & HidNpadButton_A) && !g_touch_mode)
    hit = 1;
  if (hit) {
    if (!(flags & UI_NOFOCUS))
      g_focus = id;
    if (!(flags & UI_SILENT))
      lab_audio_click();
  }
  return hit;
}

int ui_focused(int id) { return g_focus == id && !g_touch_mode && !g_modal; }

int ui_down(int id) {
  if (g_modal)
    return 0;
  if (g_touch_id == id && ui_pad->touch && !g_touch_cancel)
    for (int i = 0; i < g_nitems; i++)
      if (g_items[i].id == id && in_rect(g_touch_x, g_touch_y, g_items[i].x, g_items[i].y, g_items[i].w, g_items[i].h))
        return 1;
  return ui_focused(id) && (ui_pad->held & HidNpadButton_A);
}

void ui_focus(int id) { g_focus = id; }
int ui_focus_id(void) { return g_focus; }
void ui_default_focus(int id) { g_default = id; }

int ui_focus_rect(float *x, float *y, float *w, float *h) {
  for (int i = 0; i < g_nitems; i++)
    if (g_items[i].id == g_focus) {
      *x = g_items[i].x, *y = g_items[i].y, *w = g_items[i].w, *h = g_items[i].h;
      return 1;
    }
  return 0;
}

int ui_back(void) { return !g_modal && (ui_pad->down & HidNpadButton_B); }

/* this frame's press of these used up: what comes after in the frame does
 * not see it (B leaving a list is not the screen's Back too) */
void ui_eat(uint64_t buttons) {
  if (ui_pad != &g_nopad) {
    g_nopad = *ui_pad;
    ui_pad = &g_nopad;
  }
  g_nopad.down &= ~buttons;
}
int ui_pressed(uint64_t b) { return !g_modal && (ui_pad->down & b); }

void ui_scroll(float x, float y, float w, float h, float *offset, float max) {
  const LabPad *p = ui_pad;
  static int armed, dragging;
  static float last_y;
  if (max < 0)
    max = 0;
  if (!g_modal && p->touch && p->touch_in) {
    if (p->touch_began) {
      armed = in_rect(p->tx, p->ty, x, y, w, h);
      dragging = 0;
      last_y = p->ty;
    } else if (armed) {
      if (!dragging && fabsf(p->ty - g_touch_y0) > 8.0f) {
        dragging = 1;
        g_touch_cancel = 1; /* a drag, not a tap */
      }
      if (dragging)
        *offset -= p->ty - last_y;
      last_y = p->ty;
    }
  } else {
    armed = dragging = 0;
  }
  /* the right stick */
  if (!g_modal && fabsf(p->ry) > 0.2f)
    *offset -= p->ry * 9.0f;
  if (*offset > max)
    *offset = max;
  if (*offset < 0)
    *offset = 0;
}

/* ------------------------------------------------------------ navigation */
static float g_rep_next;
static int g_rep_dir = -1;

static int nav_dir(const LabPad *p) {
  int dir = -1; /* 0 up 1 right 2 down 3 left */
  if (p->held & HidNpadButton_Up)
    dir = 0;
  else if (p->held & HidNpadButton_Down)
    dir = 2;
  else if (p->held & HidNpadButton_Left)
    dir = 3;
  else if (p->held & HidNpadButton_Right)
    dir = 1;
  else if (p->ly > 0.6f)
    dir = 0;
  else if (p->ly < -0.6f)
    dir = 2;
  else if (p->lx < -0.6f)
    dir = 3;
  else if (p->lx > 0.6f)
    dir = 1;
  float now = ui_time();
  if (dir < 0) {
    g_rep_dir = -1;
    return -1;
  }
  if (dir != g_rep_dir) {
    g_rep_dir = dir;
    g_rep_next = now + 0.38f;
    return dir;
  }
  if (now >= g_rep_next) {
    g_rep_next = now + 0.11f;
    return dir;
  }
  return -1;
}

static const Item *find_item(int id) {
  for (int i = 0; i < g_nitems; i++)
    if (g_items[i].id == id)
      return &g_items[i];
  return NULL;
}

/* An item's rect as seen: cut to its list's view (0 if none of it shows). */
static int seen_rect(const Item *it, float *x, float *y, float *w, float *h) {
  *x = it->x, *y = it->y, *w = it->w, *h = it->h;
  if (it->cw <= 0)
    return 1;
  float x0 = fmaxf(it->x, it->cx), y0 = fmaxf(it->y, it->cy);
  float x1 = fminf(it->x + it->w, it->cx + it->cw), y1 = fminf(it->y + it->h, it->cy + it->ch);
  if (x1 <= x0 || y1 <= y0)
    return 0;
  *x = x0, *y = y0, *w = x1 - x0, *h = y1 - y0;
  return 1;
}

static void navigate(int dir) {
  const Item *cur = find_item(g_focus);
  if (!cur)
    return;
  static const float dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
  int sideways = dir == 1 || dir == 3;
  float ux, uy, uw, uh;
  if (!seen_rect(cur, &ux, &uy, &uw, &uh))
    ux = cur->x, uy = cur->y, uw = cur->w, uh = cur->h;
  float cx = ux + uw * 0.5f, cy = uy + uh * 0.5f;
  const Item *best = NULL;
  float best_s = 1e30f;
  for (int i = 0; i < g_nitems; i++) {
    const Item *it = &g_items[i];
    if (it == cur || (it->flags & UI_NOFOCUS))
      continue;
    /* what of it can be seen: sideways, only that (a list's rows scrolled
     * out of its view, or half under the tabs below it, must not take the
     * focus from the tabs beside each other); up and down, the rows out of
     * view still count, so the list scrolls on to them */
    float ix0, iy0, iw, ih;
    int same_list = cur->cw > 0 && it->cw == cur->cw && it->ch == cur->ch && it->cx == cur->cx && it->cy == cur->cy;
    if (!seen_rect(it, &ix0, &iy0, &iw, &ih)) {
      if (sideways || !same_list) /* from outside the list (its tabs): only what shows */
        continue;
      ix0 = it->x, iy0 = it->y, iw = it->w, ih = it->h;
    }
    float ix = ix0 + iw * 0.5f, iy = iy0 + ih * 0.5f;
    /* edge distance along the direction: rows that overlap count as "in line" */
    float along = (ix - cx) * dx[dir] + (iy - cy) * dy[dir];
    float across = fabsf((ix - cx) * dy[dir] - (iy - cy) * dx[dir]);
    if (along <= 1.0f)
      continue;
    float overlap = !sideways ? fminf(ux + uw, ix0 + iw) - fmaxf(ux, ix0) : fminf(uy + uh, iy0 + ih) - fmaxf(uy, iy0);
    /* in line: overlapping by more than a shared edge (a list's last row and
     * the tabs under it touch, to a rounding error) */
    float s = along + (overlap > 1.0f ? 0.0f : across * 2.0f);
    /* what lies in line first, however far (right from a list's row: the
     * pane beside it, not the tabs above it) */
    if (overlap <= 1.0f)
      s += 100000.0f;
    /* up and down a list, its next row before what is under the list (a
     * row just out of view lies as far as the tabs below it) */
    if (!sideways && same_list)
      s -= 8.0f;
    if (s < best_s) {
      best_s = s;
      best = it;
    }
  }
  if (best)
    g_focus = best->id;
}

/* ------------------------------------------------------------ hints */
static Hint g_hints[10];
static int g_nhints;

void ui_hint(const char *button, const char *what) {
  if (g_nhints < 10) {
    snprintf(g_hints[g_nhints].b, sizeof g_hints[0].b, "%s", button);
    snprintf(g_hints[g_nhints].w, sizeof g_hints[0].w, "%s", what);
    g_nhints++;
  }
}

/* ------------------------------------------------------------ stack */
static int g_stack[12], g_stack_focus[12], g_depth;

int ui_top(void) { return g_depth ? g_stack[g_depth - 1] : SCR_SPLASH; }

void ui_push(int s) {
  if (g_depth) {
    g_stack_focus[g_depth - 1] = g_focus;
  }
  if (g_depth < 12)
    g_stack[g_depth++] = s;
  g_focus = -1;
  g_touch_id = -1;
  g_changed = 1;
  scr_enter(s);
}

void ui_reset(int s) {
  g_depth = 0;
  ui_push(s);
}

void ui_pop(void) {
  if (g_depth <= 1)
    return;
  g_depth--;
  g_focus = g_stack_focus[g_depth - 1];
  g_touch_id = -1;
  g_changed = 1;
  scr_enter(g_stack[g_depth - 1]);
}

/* ------------------------------------------------------------ dialogs */
static struct {
  int on;
  char title[64], text[256], yes[24], no[24];
  void (*fn)(int);
  int sel;
  int fresh; /* opened this frame: the press that opened it is not an answer */
} D;

void ui_confirm(const char *title, const char *text, const char *yes, const char *no, void (*fn)(int)) {
  D.on = 1;
  snprintf(D.title, sizeof D.title, "%s", title);
  snprintf(D.text, sizeof D.text, "%s", text);
  snprintf(D.yes, sizeof D.yes, "%s", yes);
  snprintf(D.no, sizeof D.no, "%s", no ? no : "");
  D.fn = fn;
  D.sel = no ? 1 : 0;
  D.fresh = 1;
}

static char g_toast[128];
static float g_toast_until;
int ui_dialog_open(void) { return D.on; }

void ui_toast(const char *text) {
  snprintf(g_toast, sizeof g_toast, "%s", text);
  g_toast_until = ui_time() + 3.0f;
}

void ui_button_box(float x, float y, float w, float h, const char *label, int focus, int down) {
  lab_draw_rrect(x, y, w, h, 6, down ? 0x2f6f73ffu : focus ? 0x3f9aa0ffu : 0x3a7f84ffu);
  lab_draw_rrect(x + 2, y + 2, w - 4, h * 0.45f, 5, 0xffffff28u);
  lab_draw_text(x + w * 0.5f, y + h * 0.5f + 5.5f, 15, 0xfff4e6ffu, LAB_CENTER, 1, label);
}

/* an iOS 5 alert's button: glossy, white text */
static void hd_alert_button(float x, float y, float w, float h, const char *label, int focus, int down, int main) {
  lab_draw_rrect(x, y, w, h, 8, 0x0a1830ffu);
  lab_draw_rrect(x + 1, y + 1, w - 2, h - 2, 7, down ? 0x2a4a7effu : main ? 0x4a6ea8ffu : 0x3b5b8fffu);
  lab_draw_rrect(x + 2, y + 2, w - 4, h * 0.48f, 6, down ? 0xffffff18u : 0xffffff40u);
  if (focus && !down)
    lab_draw_rrect(x + 1, y + 1, w - 2, h - 2, 7, 0xffffff20u);
  lab_draw_text(x + w * 0.5f, y + h * 0.5f + 6.5f, 18, 0xffffffffu, LAB_CENTER, 1, label);
}

static void dialog_frame(const LabPad *p) {
  const int hd = g_cw > 400;
  lab_draw_rect(0, 0, g_cw, g_ch, hd ? 0x00000070u : 0x000000a0u);
  float w = hd ? 420 : 280, x = (g_cw - w) * 0.5f;
  float ts = hd ? 16.0f : 13.0f; /* the text's size */
  float th = 0;
  {
    /* measure the text */
    int starts[32], lens[32];
    float px = ts * g_px_per_unit;
    int n = lab_text_wrap(D.text, px, 0, (w - 32) * g_px_per_unit, starts, lens, 32);
    th = (float)n * ts * 1.3f;
  }
  float bh = hd ? 44 : 38;
  float h = (hd ? 64 : 58) + th + bh + (hd ? 30 : 26), y = (g_ch - h) * 0.5f;
  if (hd) {
    /* UIAlertView: a dark blue glass with a white rim */
    lab_draw_rrect(x - 4, y - 2, w + 8, h + 10, 14, 0x00000050u);
    lab_draw_rrect(x, y, w, h, 12, 0xe8eef8ffu);
    lab_draw_rrect(x + 2, y + 2, w - 4, h - 4, 10, 0x152a52f0u);
    lab_draw_rrect(x + 3, y + 3, w - 6, 34, 9, 0xffffff26u);
    lab_draw_text(x + w * 0.5f, y + 36, 20, 0xffffffffu, LAB_CENTER, 1, D.title);
    lab_draw_text_box(x + 16, y + 52, w - 32, ts, 0xf0f4ffffu, LAB_CENTER, 0, D.text);
  } else {
    const LabTex *bg = lab_tex("bg_credits_popup");
    lab_draw_rrect(x - 3, y - 3, w + 6, h + 6, 10, 0x00000060u);
    if (bg)
      lab_draw_image(bg, x, y, w, h, 0xffffffffu);
    else
      lab_draw_rrect(x, y, w, h, 8, 0xf6ecd0ffu);
    lab_draw_text(x + w * 0.5f, y + 30, 17, 0x1e3a3cffu, LAB_CENTER, 1, D.title);
    lab_draw_text_box(x + 16, y + 44, w - 32, 13, 0x202020ffu, LAB_CENTER, 0, D.text);
  }
  int two = D.no[0] != 0;
  float bw = two ? (w - 42) * 0.5f : (hd ? 200 : 140), by = y + h - bh - (hd ? 16 : 12);
  float bx0 = two ? x + 16 : x + (w - bw) * 0.5f, bx1 = x + w - 16 - bw;
  if (two && (p->down & (HidNpadButton_Left | HidNpadButton_Right)))
    D.sel ^= 1;
  if (two && (p->lx < -0.6f))
    D.sel = 0;
  if (two && (p->lx > 0.6f))
    D.sel = 1;
  if (hd) {
    hd_alert_button(bx0, by, bw, bh, D.yes, D.sel == 0, 0, !two);
    if (two)
      hd_alert_button(bx1, by, bw, bh, D.no, D.sel == 1, 0, 0);
  } else {
    ui_button_box(bx0, by, bw, bh, D.yes, D.sel == 0, 0);
    if (two)
      ui_button_box(bx1, by, bw, bh, D.no, D.sel == 1, 0);
  }
  if (!g_touch_mode)
    lab_draw_ring(D.sel == 0 ? bx0 : bx1, by, bw, bh, hd ? 8 : 6, 2.5f, 0xffd23cffu);
  int answer = -1;
  if (D.fresh) {
    D.fresh = 0;
    return;
  }
  if (p->down & HidNpadButton_A)
    answer = D.sel == 0;
  if (p->down & HidNpadButton_B)
    answer = 0;
  if (p->touch_ended) {
    if (in_rect(g_touch_x, g_touch_y, bx0, by, bw, bh))
      answer = 1;
    else if (two && in_rect(g_touch_x, g_touch_y, bx1, by, bw, bh))
      answer = 0;
  }
  if (answer >= 0) {
    lab_audio_click();
    D.on = 0;
    if (D.fn)
      D.fn(answer);
  }
}

/* ------------------------------------------------------------ side panels */
void ui_glyph(float x, float y, const char *b, float s);
void ui_glyph(float x, float y, const char *b, float s) {
  /* a Switch-style button: coloured circle (A B X Y), pill (L R ZL ZR, +/-),
   * or a stick */
  uint32_t fill = 0x2a2a2affu, text = 0xffffffffu;
  int pill = strlen(b) > 1 && strcmp(b, "LS") && strcmp(b, "RS");
  if (!strcmp(b, "A")) fill = 0xd8453cffu;
  else if (!strcmp(b, "B")) fill = 0xe8b923ffu, text = 0x202020ffu;
  else if (!strcmp(b, "X")) fill = 0x3c6fd8ffu;
  else if (!strcmp(b, "Y")) fill = 0x3ca44cffu;
  if (!strcmp(b, "LS") || !strcmp(b, "RS")) {
    lab_draw_rrect(x, y, s, s, s * 0.5f, 0x2a2a2affu);
    lab_draw_rrect(x + s * 0.2f, y + s * 0.2f, s * 0.6f, s * 0.6f, s * 0.3f, 0x5a5a5affu);
    lab_draw_text(x + s * 0.5f, y + s * 0.69f, s * 0.42f, 0xffffffffu, LAB_CENTER, 1, b[0] == 'L' ? "L" : "R");
    return;
  }
  float w = pill ? s * 1.5f : s;
  if (!strcmp(b, "+") || !strcmp(b, "-"))
    w = s;
  lab_draw_rrect(x, y, w, s, s * 0.5f, fill);
  lab_draw_text(x + w * 0.5f, y + s * 0.7f, s * (pill ? 0.46f : 0.6f), text, LAB_CENTER, 1, b);
}

static char g_side_title[128], g_side_sub[128], g_side_sub2[128];
void lab_ui_side_info(const char *title, const char *sub, const char *sub2);
void lab_ui_side_info(const char *title, const char *sub, const char *sub2) {
  snprintf(g_side_title, sizeof g_side_title, "%s", title ? title : "");
  snprintf(g_side_sub, sizeof g_side_sub, "%s", sub ? sub : "");
  snprintf(g_side_sub2, sizeof g_side_sub2, "%s", sub2 ? sub2 : "");
}

static Hint g_shown[10];
static int g_nshown;

static void side_panels(int ww, int wh, float px, float py, float pw, float ph) {
  if (!dcr_config()->side_panels)
    return;
  const LabTex *bg = lab_hd_on() ? lab_tex("ipa:textures-ipad/common/main-menu-bg") : NULL;
  if (!bg)
    bg = lab_tex("main_menu2_bg");
  float s = (float)wh / 720.0f; /* window pixels per 720p pixel */
  if (bg) {
    /* the paper texture across the width, as the menu's background */
    float bw = (float)ww, bh = bw * (float)bg->h / (float)bg->w;
    lab_draw_image(bg, 0, ((float)wh - bh) * 0.5f, bw, bh, 0xb8c8ccffu);
  } else {
    lab_draw_rect(0, 0, (float)ww, (float)wh, 0x3d8aa0ffu);
  }
  /* a shadow along the picture's edges (not while it turns) */
  int upright = lab_gfx_upright();
  for (int i = 0; i < 8 && upright >= 0; i++) {
    uint32_t a = (uint32_t)(70 - i * 8);
    lab_draw_rect(px - (float)(i + 1) * 2 * s, 0, 2 * s, (float)wh, a);
    lab_draw_rect(px + pw + (float)i * 2 * s, 0, 2 * s, (float)wh, a);
  }
  if (upright != 1)
    return; /* a level turned to fill the screen: no room beside it */
  float lw = px; /* each panel's width */
  /* left: the game, or what is being played */
  if (g_side_title[0]) {
    float y = (float)wh * 0.36f;
    lab_draw_text_box(24 * s, y, lw - 48 * s, 26 * s, 0xfff4e6ffu, LAB_CENTER, 1, g_side_title);
    lab_draw_text(lw * 0.5f, y + 80 * s, 20 * s, 0xe8f4f4ffu, LAB_CENTER, 1, g_side_sub);
    lab_draw_text(lw * 0.5f, y + 112 * s, 16 * s, 0xd0e4e4ffu, LAB_CENTER, 0, g_side_sub2);
  } else {
    const LabTex *art = lab_tex("splashscreen_166");
    if (art) {
      float aw = lw - 60 * s, ah = aw * (float)art->h / (float)art->w;
      if (ah > (float)wh - 120 * s) {
        ah = (float)wh - 120 * s;
        aw = ah * (float)art->w / (float)art->h;
      }
      float ax = (lw - aw) * 0.5f, ay = ((float)wh - ah) * 0.5f;
      lab_draw_rrect(ax - 4 * s, ay - 4 * s, aw + 8 * s, ah + 8 * s, 10 * s, 0x00000050u);
      lab_draw_image(art, ax, ay, aw, ah, 0xffffffffu);
    }
  }
  /* right: the controls */
  float rx = px + pw + 40 * s, y = (float)wh * 0.5f - (float)g_nshown * 23 * s;
  for (int i = 0; i < g_nshown; i++) {
    ui_glyph(rx, y + (float)i * 46 * s, g_shown[i].b, 30 * s);
    lab_draw_text(rx + 58 * s, y + (float)i * 46 * s + 21 * s, 19 * s, 0xfff4e6ffu, LAB_LEFT, 1, g_shown[i].w);
  }
}

/* ------------------------------------------------------------ common pieces */
void ui_background(const char *drawable) {
  const LabTex *t = lab_tex(drawable);
  if (t)
    lab_draw_image(t, 0, 0, 320, 480, 0xffffffffu);
  else
    lab_draw_rect(0, 0, 320, 480, 0x8fbfd0ffu);
}

void ui_navibar(const char *header, const char *fallback) {
  const LabTex *bar = lab_tex("navi_bar");
  if (bar)
    lab_draw_image(bar, 0, 0, 320, 45, 0xffffffffu);
  else
    lab_draw_rect(0, 0, 320, 45, 0x3d8aa0ffu);
  const LabTex *h = header ? lab_tex(header) : NULL;
  if (h) {
    float w = lab_tex_dp_w(h), hh = lab_tex_dp_h(h);
    lab_draw_image(h, (320 - w) * 0.5f, (45 - hh) * 0.5f, w, hh, 0xffffffffu);
  } else if (fallback) {
    lab_draw_text(160, 30, 20, 0xf4ecd8ffu, LAB_CENTER, 1, fallback);
  }
}

/* ------------------------------------------------------------ frames */
static void begin_input(const LabPad *pad, int menus) {
  ui_pad = pad ? pad : &g_zero_pad;
  const LabPad *p = ui_pad;
  if (p->touch && p->touch_in) {
    g_touch_x = p->tx, g_touch_y = p->ty;
    if (p->touch_began) {
      g_touch_x0 = p->tx, g_touch_y0 = p->ty;
      g_touch_cancel = 0;
      g_touch_id = -1;
    }
    g_touch_mode = 1;
  }
  if (p->down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y |
                 HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right |
                 HidNpadButton_StickLUp | HidNpadButton_StickLDown | HidNpadButton_StickLLeft |
                 HidNpadButton_StickLRight | HidNpadButton_L | HidNpadButton_R)) {
    if (menus && g_touch_mode && (p->down & HidNpadButton_A)) {
      /* the first A after touching only shows the ring */
      g_touch_mode = 0;
      g_nopad = *p;
      g_nopad.down &= ~HidNpadButton_A;
      ui_pad = &g_nopad;
    }
    g_touch_mode = 0;
  }
  g_nitems = 0;
  g_nhints = 0;
  g_default = -1;
  ui_clip(0, 0, 0, 0);
}

/* the ring glides from the item it was on to the next (hardware
 * 2026-09-26: a ring jumping about read as jerky) */
static float g_ring[5];  /* x, y, w, h, corner */
static int g_ring_on;    /* drawn last frame */

static void draw_ring(const Item *f) {
  float t[5] = {f->x, f->y, f->w, f->h, 6};
  if (f->flags & UI_ROUND) {
    float r = fmaxf(f->w, f->h) * 0.5f;
    t[0] = f->x + f->w * 0.5f - r, t[1] = f->y + f->h * 0.5f - r, t[2] = t[3] = 2 * r, t[4] = r;
  }
  if (!g_ring_on)
    memcpy(g_ring, t, sizeof g_ring);
  for (int i = 0; i < 5; i++) {
    g_ring[i] += (t[i] - g_ring[i]) * 0.38f;
    if (fabsf(t[i] - g_ring[i]) < 0.3f)
      g_ring[i] = t[i];
  }
  float pulse = 0.75f + 0.25f * sinf(ui_time() * 6.0f);
  uint32_t col = 0xffd23c00u | (uint32_t)(255 * pulse);
  lab_draw_ring(g_ring[0], g_ring[1], g_ring[2], g_ring[3], fminf(g_ring[4], fminf(g_ring[2], g_ring[3]) * 0.5f),
                3.0f, col);
  g_ring_on = 2;
}

static void end_input(void) {
  const LabPad *p = ui_pad;
  g_ring_on = g_ring_on > 1; /* not drawn this frame yet */
  if (g_changed) {
    /* a new screen: the items seen were the old one's -- keep the focus it
     * was given, move nothing, draw no ring */
    g_changed = 0;
    g_nitems = 0;
    g_ring_on = 0; /* the next screen's ring starts where its item is */
    nav_dir(p); /* the press that changed the screen does not repeat on the new one */
    memcpy(g_shown, g_hints, sizeof g_hints);
    g_nshown = g_nhints;
    return;
  }
  if (!g_modal) {
    if (!find_item(g_focus)) {
      g_focus = g_default >= 0 && find_item(g_default) ? g_default : -1;
      for (int i = 0; g_focus < 0 && i < g_nitems; i++)
        if (!(g_items[i].flags & UI_NOFOCUS))
          g_focus = g_items[i].id;
    }
    int dir = nav_dir(p);
    if (g_hold_nav == 2 || (g_hold_nav == 1 && (dir == 1 || dir == 3)))
      dir = -1;
    g_hold_nav = 0;
    if (dir >= 0 && !g_touch_mode) {
      /* the way it is pushed on the screen, when a level's picture is
       * turned (a level's end stays turned): the board's direction */
      static const float vx[4] = {0, 1, 0, -1}, vy[4] = {1, 0, -1, 0};
      float x = vx[dir], y = vy[dir];
      lab_gfx_view_to_board(&x, &y);
      dir = y > 0.5f ? 0 : x > 0.5f ? 1 : y < -0.5f ? 2 : 3;
      navigate(dir);
    }
  }
  if (p->touch_ended)
    g_touch_id = -1;
  /* the ring */
  const Item *f = find_item(g_focus);
  if (f && !g_touch_mode && !g_modal && !(f->flags & (UI_NORING | UI_NOFOCUS)))
    draw_ring(f);
  else
    g_ring_on = 0;
  if (g_toast[0] && ui_time() < g_toast_until) {
    float ts = g_cw > 400 ? 17.0f : 13.0f, th = ts * 2.3f;
    float w = lab_text_width(ts, 1, g_toast) + ts * 2.2f;
    /* the iPad menus: under the navigation bar, off the buttons at the
     * bottom (Play, Download, the how-to's) */
    float ty = g_cw > 400 ? 52.0f : g_ch - th - ts * 3.8f;
    lab_draw_rrect(g_cw * 0.5f - w * 0.5f, ty, w, th, th * 0.5f, 0x000000c0u);
    lab_draw_text(g_cw * 0.5f, ty + th - th * 0.33f, ts, 0xffffffffu, LAB_CENTER, 1, g_toast);
  }
  memcpy(g_shown, g_hints, sizeof g_hints);
  g_nshown = g_nhints;
}

void lab_ui_init(void) {
  g_t0 = armGetSystemTick();
  lab_gfx_side_hook = side_panels;
  ui_push(SCR_SPLASH);
}

void lab_ui_frame(const LabPad *pad) {
  begin_input(pad, 1);
  g_modal = D.on;
  if (lab_hd_on()) {
    /* the iPad menus: landscape, in points */
    lab_gfx_begin_hd();
    lab_gfx_set_hd_canvas_h(HD_CANVAS_H);
    g_cw = lab_gfx_hd_canvas_w(), g_ch = HD_CANVAS_H;
    g_px_per_unit = (float)lab_gfx_hd_h() / HD_CANVAS_H;
    lab_draw_begin(g_cw, g_ch, lab_gfx_hd_w(), lab_gfx_hd_h());
    /* see-through over local play's boards, and over a level (its Settings) */
    int over = lab_gfx_race_active() || (ui_top() == SCR_SETTINGS && scr_settings_over_game() && lab_game_active());
    lgl.ClearColor(0, 0, 0, over ? 0.0f : 1.0f);
    lgl.Clear(GL_COLOR_BUFFER_BIT);
    scr_frame(ui_top());
    end_input();
    if (D.on) {
      g_modal = 0;
      dialog_frame(ui_pad);
      g_nhints = 0;
      ui_hint("A", "OK");
      if (D.no[0])
        ui_hint("B", D.no);
      memcpy(g_shown, g_hints, sizeof g_hints);
      g_nshown = g_nhints;
    }
    lab_hd_hints(g_shown, g_nshown);
    lab_draw_end();
    return;
  }
  g_cw = 320, g_ch = 480;
  g_px_per_unit = lab_gfx_px_per_dp();
  lab_draw_begin(320, 480, lab_gfx_surface_w(), lab_gfx_surface_h());
  /* local play: over the two boards, see-through where nothing is drawn */
  lgl.ClearColor(0, 0, 0, lab_gfx_race_active() ? 0.0f : 1.0f);
  lgl.Clear(GL_COLOR_BUFFER_BIT);
  scr_frame(ui_top());
  end_input();
  if (D.on) {
    /* the dialog's own frame of input */
    g_modal = 0;
    dialog_frame(ui_pad);
    g_nhints = 0;
    ui_hint("A", "OK");
    if (D.no[0])
      ui_hint("B", D.no);
    memcpy(g_shown, g_hints, sizeof g_hints);
    g_nshown = g_nhints;
  }
  lab_draw_end();
}

/* ------------------------------------------------------------ over a game */
static struct {
  float x, y;          /* the pointer, dp */
  float shown_until;   /* s */
  int finger;          /* A is down as a finger (at fx, fy) */
  float fx, fy;
  int touch;           /* the touchscreen's finger is down in the game */
  int paused;          /* a popup is up (known, or guessed from + presses) */
  int pointer_mode;    /* the right stick moved the pointer: A taps there */
} P = {160, 300, 0, 0, 0, 0, 0, 0, 0};

void lab_ui_game_frame(const LabPad *pad) {
  begin_input(pad, 0);
  const LabPad *p = ui_pad;
  float now = ui_time();
  /* the touchscreen, straight to the engine */
  if (p->touch && p->touch_in) {
    lab_game_touch(P.touch ? 2 : 0, p->tx, p->ty);
    P.touch = 1;
  } else if (P.touch) {
    lab_game_touch(1, g_touch_x, g_touch_y);
    P.touch = 0;
  }
  /* the engine's popup (pause, a level's end, the rating): known from its
   * state when this is the 1.29 build, else guessed from our + presses */
  int popup = lab_game_popup_open();
  if (popup >= 0)
    P.paused = popup;
  int overlay = P.paused;
  /* its buttons, for the focus ring */
  LabBtn btn[24];
  int nb = overlay ? lab_game_overlay_buttons(btn, 24) : 0;
  if (!overlay)
    P.pointer_mode = 0;
  /* the pointer: the right stick (or the left one on a popup with no
   * buttons found) */
  float mx = p->rx, my = p->ry;
  if (overlay && !nb && fabsf(mx) < 0.2f && fabsf(my) < 0.2f)
    mx = p->lx, my = p->ly;
  float m = sqrtf(mx * mx + my * my);
  if (m > 0.2f) {
    float sp = dcr_config()->pointer_speed * (m - 0.2f) / 0.8f;
    float bx = mx / m, by = my / m;
    lab_gfx_view_to_board(&bx, &by); /* the way it goes on the (turned) screen */
    P.x += bx * sp * 1.6f;
    P.y -= by * sp * 1.6f;
    P.x = fminf(fmaxf(P.x, 0), 320), P.y = fminf(fmaxf(P.y, 0), 480);
    P.shown_until = now + 3.0f;
    P.pointer_mode = 1;
    if (P.finger)
      lab_game_touch(2, P.x, P.y);
  }
  if (nb && (p->down & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)))
    P.pointer_mode = 0;
  if (nb && (fabsf(p->lx) > 0.6f || fabsf(p->ly) > 0.6f))
    P.pointer_mode = 0; /* the left stick moves the ring */
  int focus_mode = nb && !P.pointer_mode;
  if (focus_mode) {
    /* the overlay's buttons as the toolkit's: the ring, the D-pad */
    for (int i = 0; i < nb; i++)
      ui_button(9000 + (int)((btn[i].ptr >> 3) & 0xffff), btn[i].x, btn[i].y, btn[i].w, btn[i].h,
                UI_NOTOUCH | UI_SILENT | UI_NORING); /* the engine plays its own click; it glows (below) */
    ui_default_focus(9000 + (int)((btn[0].ptr >> 3) & 0xffff));
    g_touch_mode = 0; /* the ring shows over a game's popup */
  } else if (overlay) {
    P.shown_until = now + 0.5f; /* the pointer stays while a popup is up */
  }
  /* A: a finger, on the focused button's centre or at the pointer */
  if ((p->down & HidNpadButton_A) && !P.finger) {
    float fx = P.x, fy = P.y;
    if (focus_mode) {
      float bx, by, bw, bh;
      if (ui_focus_rect(&bx, &by, &bw, &bh) || (g_nitems && (ui_focus(g_items[0].id), ui_focus_rect(&bx, &by, &bw, &bh))))
        fx = bx + bw * 0.5f, fy = by + bh * 0.5f;
    } else {
      P.shown_until = now + 3.0f;
    }
    lab_game_touch(0, fx, fy);
    P.finger = 1, P.fx = fx, P.fy = fy;
  }
  if (P.finger && !(p->held & HidNpadButton_A)) {
    lab_game_touch(1, P.fx, P.fy);
    P.finger = 0;
  }
  if (p->down & (HidNpadButton_Plus | HidNpadButton_B)) {
    /* Android's Back / Menu: closes a popup, or opens the pause overlay */
    lab_game_show_menu();
    if (popup < 0)
      P.paused = !P.paused;
  }
  if ((p->down & HidNpadButton_Minus) && dcr_config()->tilt != LAB_TILT_STICK) {
    lab_input_calibrate();
    ui_toast("Motion: this position is level (kept)");
  }
  /* ZR: the level full screen (its top to the left) or upright */
  if ((p->down & HidNpadButton_ZR) && dcr_config()->layout == LAB_LAYOUT_PORTRAIT) {
    int next = dcr_config()->level_layout == LAB_LAYOUT_PORTRAIT ? LAB_LAYOUT_ROTATED_LEFT : LAB_LAYOUT_PORTRAIT;
    dcr_config_set_level_layout(next);
    lab_reg_set_int("port-level-layout", next);
    ui_toast(next == LAB_LAYOUT_PORTRAIT ? "Upright" : "Full screen");
  }
  /* ZL: motion (gyro) tilt on / off -- on: the controller's motion, the
   * stick adding to it; flat is level (- sets the way it is held) */
  if (p->down & HidNpadButton_ZL) {
    int on = dcr_config()->tilt == LAB_TILT_STICK;
    int t = on ? LAB_TILT_BOTH : LAB_TILT_STICK;
    dcr_config_set_tilt(t);
    lab_reg_set_int("port-tilt", t);
    ui_toast(on ? "Motion on: tilt the controller" : "Motion off: the stick");
  }
  /* drawn over the engine's frame: the ring, the pointer, a toast */
  if (lab_game_ipad()) {
    /* the iPad board's surface: the overlay space widened to 3:4 */
    lab_gfx_begin_ipad();
    lab_draw_begin_at(-20, 0, 360, 480, lab_gfx_ipad_w(), lab_gfx_ipad_h());
  } else {
    lab_draw_begin(320, 480, lab_gfx_surface_w(), lab_gfx_surface_h());
  }
  if (focus_mode) {
    end_input(); /* navigation */
    /* the focused button glows, steadily (pvztouch's Zombatar glow) */
    float gx, gy, gw, gh;
    if (ui_focus_rect(&gx, &gy, &gw, &gh))
      lab_draw_glow(gx, gy, gw, gh);
  } else {
    g_nitems = 0;
    memcpy(g_shown, g_hints, sizeof g_hints);
    g_nshown = g_nhints;
  }
  if (!focus_mode && (now < P.shown_until || P.finger)) {
    float a = P.finger ? 1.0f : fminf(1.0f, (P.shown_until - now) * 2.0f);
    uint32_t al = (uint32_t)(a * 255);
    lab_draw_rrect(P.x - 9, P.y - 9, 18, 18, 9, al * 5 / 10);
    lab_draw_ring(P.x - 7, P.y - 7, 14, 14, 7, 2.5f, 0xffffff00u | al);
    lab_draw_rrect(P.x - 2.5f, P.y - 2.5f, 5, 5, 2.5f, (P.finger ? 0xffd23c00u : 0xffffff00u) | al);
  }
  if (g_toast[0] && now < g_toast_until) {
    float w = lab_text_width(13, 1, g_toast) + 28;
    lab_draw_rrect(160 - w * 0.5f, 430, w, 30, 15, 0x000000c0u);
    lab_draw_text(160, 450, 13, 0xffffffffu, LAB_CENTER, 1, g_toast);
  }
  lab_draw_end();
  g_nhints = 0;
  if (!overlay) {
    ui_hint("LS", dcr_config()->tilt == LAB_TILT_MOTION ? "Tilt (motion)" : "Tilt the board");
    ui_hint("+", "Pause");
  } else {
    ui_hint(focus_mode ? "A" : "A", focus_mode ? "Press" : "Tap");
    ui_hint(focus_mode ? "LS" : (nb ? "RS" : "LS"), focus_mode ? "Choose" : "Pointer");
    ui_hint("+", "Back");
  }
  ui_hint("RS", "Pointer");
  if (dcr_config()->tilt != LAB_TILT_STICK)
    ui_hint("-", "Level position");
  ui_hint("ZL", dcr_config()->tilt == LAB_TILT_STICK ? "Motion on" : "Motion off");
  if (dcr_config()->layout == LAB_LAYOUT_PORTRAIT)
    ui_hint("ZR", "Turn the view");
  memcpy(g_shown, g_hints, sizeof g_hints);
  g_nshown = g_nhints;
}

int lab_ui_game_paused(void) { return P.paused; }

void lab_ui_open_settings_over_game(void) {
  P.paused = 0;
  ui_push(SCR_SETTINGS);
}

void lab_ui_game_ended(void) {
  P.paused = 0;
  P.finger = 0;
  lab_ui_side_info(NULL, NULL, NULL);
  scr_game_ended();
}

static volatile int g_quit;
int lab_ui_quit_requested(void) { return g_quit; }
void scr_request_quit(void) { g_quit = 1; }
