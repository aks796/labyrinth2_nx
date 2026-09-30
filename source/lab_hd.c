/* lab_hd.c -- the iPad menus: Labyrinth 2 HD's screens, rebuilt, landscape.
 *
 * With the player's own .ipa of Labyrinth 2 HD in the game's folder (read by
 * lab_apk.c), the menus are the iPad's instead of the phone's: drawn from
 * its pictures (menugraphics-ipad/, menugraphics/, the main menu's atlas
 * textures-ipad-full/main-menu.png) on a landscape canvas in iPad points,
 * 768 tall and as wide as the screen (1365 at 16:9; the iPad's 1024 plus
 * room). The toolkit is lab_ui.c's (buttons, focus, the D-pad, touches);
 * the screens:
 *
 *   splash        Default-Landscape while the sounds decode
 *   main menu     the atlas: the three bars at their angle (Single player,
 *                 Multi player, Download levels), the title, the board;
 *                 Settings and Create at the top left, the ball (Awards)
 *   settings      a popover under Settings (Calibrate, Sound FX, Tilt view,
 *                 the port's rows, Credits), also over a level
 *   awards        a popover over the ball: the four balls, the counts, the
 *                 list
 *   packs         Single player / Multi player (lab_hd_packs.c)
 *   download,     lab_hd_online.c
 *   create
 *
 * A level is the engine's, as with the phone's menus (lab_ui.c, lab_game.c):
 * iPhone levels on the portrait surface, iPad levels on the 3:4 one. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "lab.h"
#include "lab_hd.h"
#include "lab_ui.h"
#include "util.h"

int lab_hd_on(void) { return lab_ipa_present() && dcr_config()->menus == 0; }

/* ============================================================ pieces */
float hd_cw(void) { return ui_canvas_w(); }
float hd_ch(void) { return ui_canvas_h(); }

const LabTex *hd_tex(const char *name) {
  char n[160];
  snprintf(n, sizeof n, "ipa:%s", name);
  return lab_tex(n);
}

float hd_w(const char *name) { return lab_tex_pt_w(hd_tex(name)); }
float hd_h(const char *name) { return lab_tex_pt_h(hd_tex(name)); }

void hd_pic(const char *name, float x, float y, uint32_t rgba) {
  const LabTex *t = hd_tex(name);
  if (t)
    lab_draw_image(t, x, y, lab_tex_pt_w(t), lab_tex_pt_h(t), rgba);
}

void hd_pic_wh(const char *name, float x, float y, float w, float h, uint32_t rgba) {
  const LabTex *t = hd_tex(name);
  if (t)
    lab_draw_image(t, x, y, w, h, rgba);
}

/* one line cut with "..." to fit w */
void hd_text_fit(float x, float y, float size, uint32_t c, int bold, float w, const char *s) {
  if (lab_text_width(size, bold, s) <= w) {
    lab_draw_text(x, y, size, c, LAB_LEFT, bold, s);
    return;
  }
  char buf[200];
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

/* A picture button (up / down pictures); 1 when pressed. The focus: a
 * soft glow behind it, as the phone's menus here. */
int hd_pic_button(int id, const char *up, const char *down, float x, float y, int flags) {
  float w = hd_w(up), h = hd_h(up);
  int hit = ui_button(id, x, y, w, h, flags | UI_NORING);
  hd_pic(ui_down(id) && down ? down : up, x, y, 0xffffffffu);
  if (ui_focused(id))
    lab_draw_glow(x, y, w, h);
  return hit;
}

/* the navigation bar: navi_bar_ipad across the top, the title (its
 * picture, else text), the back button's picture at the left */
int hd_navibar(const char *title_pic, const char *title_text, const char *back_pic, const char *back_text) {
  float cw = hd_cw();
  hd_pic_wh("menugraphics-ipad/navi_bar_ipad", 0, 0, cw, 44, 0xffffffffu);
  if (title_pic && hd_tex(title_pic)) {
    float w = hd_w(title_pic), h = hd_h(title_pic);
    hd_pic(title_pic, (cw - w) * 0.5f, (44 - h) * 0.5f, 0xffffffffu);
  } else if (title_text) {
    lab_draw_text(cw * 0.5f, 30, 21, 0xf4ecd8ffu, LAB_CENTER, 1, title_text);
  }
  int back = 0;
  if (back_pic) {
    char dn[96];
    snprintf(dn, sizeof dn, "%s_pressed", back_pic);
    if (hd_tex(back_pic)) {
      back = hd_pic_button(HD_ID_BACK, back_pic, dn, 8, 8, 0);
    } else if (back_text) {
      float w = lab_text_width(13, 1, back_text) + 26;
      back = ui_button(HD_ID_BACK, 8, 8, w, 28, 0);
      lab_draw_rrect(8, 8, w, 28, 6, ui_down(HD_ID_BACK) ? 0x1f4f5cffu : 0x2f6f80ffu);
      lab_draw_text(8 + w * 0.5f, 27, 13, 0xf4ecd8ffu, LAB_CENTER, 1, back_text);
    }
  }
  return back;
}

/* the split view's panes: the list's (left, LW wide) and the info's */
void hd_split_bg(void) {
  float cw = hd_cw();
  const LabTex *bg = hd_tex("menugraphics-ipad/bg-light-blue");
  if (bg) {
    lab_draw_image(bg, 0, 44, HD_LW, hd_ch() - 44, 0xa8c0c8ffu);
    lab_draw_image(bg, HD_LW, 44, cw - HD_LW, hd_ch() - 44, 0xffffffffu);
  } else {
    lab_draw_rect(0, 44, cw, hd_ch() - 44, 0x9fc4d0ffu);
  }
}

void hd_split_line(void) {
  hd_pic_wh("menugraphics-ipad/splitview_separator", HD_LW - 1.5f, 44, 3, hd_ch() - 44, 0xffffffffu);
}

/* ---- the lists: eased scrolling (hardware 2026-09-26: the lists jumped a
 * row's height at a time), a little bounce at their ends, iOS's scroll
 * indicator while they move */
float hd_scroll(HdScroll *s, float x, float top, float w, float view, float content) {
  float max = fmaxf(0, content - view), was = s->to;
  ui_scroll(x, top, w, view, &s->to, max);
  if (s->to != was) {
    s->at = s->to; /* a finger or the right stick: straight there */
  } else {
    s->at += (s->to - s->at) * 0.3f;
    if (fabsf(s->to - s->at) < 0.5f)
      s->at = s->to;
  }
  if (s->at > max)
    s->at = max;
  if (s->at < 0)
    s->at = 0;
  s->bump *= 0.72f;
  if (fabsf(s->bump) < 0.3f)
    s->bump = 0;
  if (fabsf(s->at - s->last_at) > 0.05f || s->bump != 0)
    s->moved = ui_time();
  s->last_at = s->at;
  return s->at - s->bump;
}

void hd_scroll_bar(const HdScroll *s, float right, float top, float view, float content) {
  float since = ui_time() - s->moved;
  if (content <= view + 1 || since > 0.9f)
    return;
  float a = since < 0.6f ? 1.0f : (0.9f - since) / 0.3f;
  float len = fmaxf(36.0f, view * view / content), f = fminf(fmaxf(s->at / (content - view), 0), 1);
  lab_draw_rrect(right - 7, top + 3 + (view - 6 - len) * f, 3.5f, len, 1.75f, (uint32_t)(a * 112.0f));
}

/* ---- a list the controller works in (hardware 2026-09-28: its rows were
 * the screen's buttons; the stick went from one to the next spatially --
 * off the list, to the pane or the tabs, when pushed a little sideways --
 * at one pace, the list scrolling only as the ring reached its edge). Now
 * the list is one thing the focus goes to, the ring round it; A goes in.
 * In it, up and down (the left stick, the D-pad) move the lit row, faster
 * the longer they are held, the list scrolling so a row more shows ahead
 * of it; the right stick or a finger scroll it, the row coming along;
 * right, pushed on purpose, goes to the info pane; B out of the list. From
 * the pane, left or B come back in at the chosen row. */
static void list_reveal(HdScroll *s, float view, float content, const float *ry, const float *rh, int n, int cur) {
  float max = fmaxf(0, content - view), y0 = ry[cur], y1 = ry[cur] + rh[cur];
  float m = fminf(rh[cur] + 8, view * 0.22f); /* a row ahead */
  float to = s->to;
  if (cur == 0)
    to = 0; /* the first: from the top (its group's title over it) */
  else if (y0 - m < to)
    to = y0 - m;
  if (cur == n - 1)
    to = max;
  else if (y1 + m > to + view)
    to = y1 + m - view;
  s->to = to < 0 ? 0 : to > max ? max : to;
  /* never out of sight while the list eases after it (held down: 30 a second) */
  if (y1 > s->at + view)
    s->at = fminf(y1 - view, max);
  if (y0 < s->at)
    s->at = fmaxf(y0, 0);
}

static void list_enter(HdList *L, HdScroll *s, float view, float content, const float *ry, const float *rh, int n,
                       int chosen) {
  L->in = 1;
  L->cur = chosen >= 0 && chosen < n ? chosen : L->cur < n ? L->cur : 0;
  L->moved = ui_time();
  L->dir = 2; /* what is held as it comes in does not step */
  list_reveal(s, view, content, ry, rh, n, L->cur);
}

int hd_list(HdList *L, HdScroll *s, float top, float view, float content, const float *ry, const float *rh, int n,
            int chosen, int pane_main) {
  float now = ui_time();
  int f = ui_focus_id(), from_pane = L->prev >= 600 && L->prev < 610;
  L->prev = f;
  if (n <= 0) {
    L->in = 0;
    return HD_LIST_NONE; /* nothing to go to: the focus is elsewhere */
  }
  L->cur = L->cur < 0 ? 0 : L->cur >= n ? n - 1 : L->cur;
  if (f != HD_ID_LIST)
    L->in = 0; /* the focus went elsewhere (a finger on a tab) */
  else if (from_pane && !L->in)
    list_enter(L, s, view, content, ry, rh, n, chosen); /* back from the pane */
  if (L->in && fabsf(s->to - L->to_was) > 0.5f) {
    /* the right stick or a finger moved the list: the row comes along, the
     * nearest one wholly in view */
    float lo = s->to, hi = s->to + view;
    int c = L->cur;
    if (ry[c] < lo - 0.5f)
      while (c < n - 1 && ry[c] < lo - 0.5f)
        c++;
    else if (ry[c] + rh[c] > hi + 0.5f)
      while (c > 0 && ry[c] + rh[c] > hi + 0.5f)
        c--;
    if (c != L->cur)
      L->cur = c, L->moved = now;
  }
  int act = HD_LIST_NONE;
  int hit = ui_button(HD_ID_LIST, 6, top + 6, HD_LW - 12, view - 12, UI_NOTOUCH | UI_SILENT | (L->in ? UI_NORING : 0));
  if (hit && !L->in) {
    list_enter(L, s, view, content, ry, rh, n, chosen);
    lab_audio_click();
    ui_eat(HidNpadButton_A);
  } else if (L->in && ui_focused(HD_ID_LIST)) {
    ui_hold_nav(1); /* up, down, left, right: the list's */
    const LabPad *p = ui_pad;
    int up = (p->held & HidNpadButton_Up) || p->ly > 0.5f, dn = (p->held & HidNpadButton_Down) || p->ly < -0.5f;
    int dir = up && !dn ? -1 : dn && !up ? 1 : 0;
    /* right: the D-pad, or the stick pushed that way and not up or down */
    int stick_right = p->lx > 0.8f && fabsf(p->ly) < 0.35f;
    int right = (p->down & HidNpadButton_Right) || (stick_right && !L->stick_right);
    L->stick_right = stick_right;
    if (hit) {
      act = HD_LIST_A;
      lab_audio_click();
    } else if (ui_back()) {
      L->in = 0;
      ui_eat(HidNpadButton_B);
      lab_audio_click();
    } else if (right) {
      L->in = 0;
      ui_focus(pane_main);
      act = HD_LIST_RIGHT;
    } else if (L->dir == 2) {
      if (!dir)
        L->dir = 0; /* let go since it came in */
    } else {
      int step = 0;
      if (dir != L->dir) {
        L->dir = dir;
        step = dir;
        L->held = now, L->next = now + 0.32f;
      } else if (dir && now >= L->next) {
        float h = now - L->held;
        float iv = h < 1.1f ? 0.1f : h < 2.4f ? 0.055f : 0.032f;
        if (!(p->held & (HidNpadButton_Up | HidNpadButton_Down)) && fabsf(p->ly) < 0.85f)
          iv = fmaxf(iv, 0.13f); /* the stick pushed part way: slower */
        step = dir;
        L->next = now + iv;
      }
      if (step) {
        int to = L->cur + step;
        if (to < 0 || to >= n) {
          if (fabsf(s->bump) < 4)
            s->bump = step < 0 ? 12.0f : -12.0f; /* its end: a little bounce */
        } else {
          L->cur = to, L->moved = now, s->moved = now;
          list_reveal(s, view, content, ry, rh, n, to);
        }
      }
    }
  }
  L->to_was = s->to;
  return act;
}

int hd_list_dwell(const HdList *L, int chosen, float dwell) {
  return L->in && L->cur != chosen && ui_time() - L->moved >= dwell ? L->cur : -1;
}

int hd_list_lit(const HdList *L, int i, int chosen) { return L->in ? i == L->cur : i == chosen; }

void hd_list_reset(HdList *L) {
  L->in = 0;
  L->cur = 0;
}

/* the iPad levels / iPhone levels tabs (ZL / ZR too); 1 when it changed */
int hd_device_tabs(float y, int *ipad) {
  int ch = -1;
  if (ui_button(HD_ID_TABS, 0, y, 160, 49, UI_NORING))
    ch = 1;
  if (ui_button(HD_ID_TABS + 1, 160, y, 160, 49, UI_NORING))
    ch = 0;
  if (ui_pressed(HidNpadButton_ZL))
    ch = 1;
  if (ui_pressed(HidNpadButton_ZR))
    ch = 0;
  hd_pic(*ipad ? "menugraphics-ipad/tab_ipad_chosen" : "menugraphics-ipad/tab_ipad", 0, y, 0xffffffffu);
  hd_pic(*ipad ? "menugraphics-ipad/tab_iphone" : "menugraphics-ipad/tab_iphone_chosen", 160, y, 0xffffffffu);
  for (int i = 0; i < 2; i++)
    if (ui_focused(HD_ID_TABS + i))
      lab_draw_glow(160.0f * (float)i + 4, y + 4, 152, 41);
  if (ch >= 0 && ch != *ipad) {
    *ipad = ch;
    return 1;
  }
  return 0;
}

/* three segments (pictures <name> / <name>_selected, 107 x 48); L / R too */
int hd_segments(int id0, float y, const char *const names[3], int *sel) {
  int ch = -1;
  for (int i = 0; i < 3; i++) {
    float x = 107.0f * (float)i;
    if (ui_button(id0 + i, x, y, 107, 48, UI_NORING))
      ch = i;
    char n[96];
    snprintf(n, sizeof n, "%s%s", names[i], *sel == i ? "_selected" : "");
    hd_pic(n, x, y, 0xffffffffu);
    if (ui_focused(id0 + i))
      lab_draw_glow(x + 6, y + 6, 95, 36);
  }
  if (ui_pressed(HidNpadButton_L) && *sel > 0)
    ch = *sel - 1;
  if (ui_pressed(HidNpadButton_R) && *sel < 2)
    ch = *sel + 1;
  if (ch >= 0 && ch != *sel) {
    *sel = ch;
    return 1;
  }
  return 0;
}

/* an iOS 5 popover: a dark blue frame, rounded, its arrow pointing at
 * (ax, ay) from above (up = 1: the popover is under it) or below */
void hd_popover(float x, float y, float w, float h, float ax, int up) {
  const uint32_t rim = 0x0c1c38f4u, glass = 0x1e3a66f4u;
  lab_draw_rrect(x - 3, y + 2, w + 6, h + 6, 14, 0x00000050u);
  lab_draw_rrect(x, y, w, h, 12, rim);
  lab_draw_rrect(x + 1, y + 1, w - 2, 40, 11, glass);
  lab_draw_rrect(x + 2, y + 2, w - 4, 20, 10, 0xffffff18u);
  /* the arrow (up < 0: none, a panel on its own) */
  if (up < 0)
    return;
  float s = 16;
  ax = fminf(fmaxf(ax, x + 24), x + w - 24);
  for (int i = 0; i < (int)s; i++) {
    float t = (float)i;
    if (up)
      lab_draw_rect(ax - t, y - s + t, 2 * t + 1, 1.2f, i < 4 ? glass : rim);
    else
      lab_draw_rect(ax - t, y + h + s - t - 1, 2 * t + 1, 1.2f, rim);
  }
}

/* an iOS switch: ON (blue) / OFF (white) */
void hd_switch(float x, float y, int on, int focus, int down) {
  const float w = 94, h = 27;
  lab_draw_rrect(x, y, w, h, 13.5f, down ? 0x707070ffu : 0x8a8a8affu);
  lab_draw_rrect(x + 1, y + 1, w - 2, h - 2, 12.5f, on ? 0x2f78e0ffu : 0xf4f4f4ffu);
  if (on)
    lab_draw_rrect(x + 2, y + 2, w - 4, h * 0.45f, 11, 0xffffff30u);
  lab_draw_text(on ? x + 32 : x + 62, y + 19, 15, on ? 0xffffffffu : 0x7a7a7affu, LAB_CENTER, 1, on ? "ON" : "OFF");
  float kx = on ? x + w - h : x;
  lab_draw_rrect(kx, y, h, h, 13.5f, 0x9a9a9affu);
  lab_draw_rrect(kx + 1, y + 1, h - 2, h - 2, 12.5f, 0xf8f8f8ffu);
  if (focus)
    lab_draw_ring(x - 3, y - 3, w + 6, h + 6, 16, 2.5f, 0xffd23cffu);
}

/* an iOS rounded rect button (the settings' SET / Show) */
void hd_rbutton(float x, float y, float w, float h, const char *label, int focus, int down) {
  lab_draw_rrect(x, y, w, h, 6, 0x1d3f7cffu);
  lab_draw_rrect(x + 1, y + 1, w - 2, h - 2, 5, down ? 0x1f5ab8ffu : 0x3a7ee8ffu);
  lab_draw_rrect(x + 2, y + 2, w - 4, h * 0.45f, 4, 0xffffff34u);
  lab_draw_text(x + w * 0.5f, y + h * 0.5f + 6, 16, 0xffffffffu, LAB_CENTER, 1, label);
  if (focus)
    lab_draw_ring(x - 3, y - 3, w + 6, h + 6, 8, 2.5f, 0xffd23cffu);
}

/* the controls, bottom right: the button, what it does */
void lab_hd_hints(const Hint *h, int n) {
  if (!n)
    return;
  const float s = 20, gap = 14, y = hd_ch() - s - 8;
  float w = 12;
  for (int i = 0; i < n; i++)
    w += s * (strlen(h[i].b) > 1 ? 1.5f : 1.0f) + 6 + lab_text_width(13, 1, h[i].w) + gap;
  float x = hd_cw() - w - 8;
  lab_draw_rrect(x, y - 6, w, s + 12, (s + 12) * 0.5f, 0x00000088u);
  x += 12;
  for (int i = 0; i < n; i++) {
    float gw = s * (strlen(h[i].b) > 1 && strcmp(h[i].b, "+") && strcmp(h[i].b, "-") ? 1.5f : 1.0f);
    lab_draw_rrect(x - 1.5f, y - 1.5f, gw + 3, s + 3, (s + 3) * 0.5f, 0xffffff70u);
    ui_glyph(x, y, h[i].b, s);
    x += s * (strlen(h[i].b) > 1 && strcmp(h[i].b, "+") && strcmp(h[i].b, "-") ? 1.5f : 1.0f) + 6;
    lab_draw_text(x, y + s * 0.72f, 13, 0xffffffffu, LAB_LEFT, 1, h[i].w);
    x += lab_text_width(13, 1, h[i].w) + gap;
  }
}

/* ============================================================ splash */
static float g_splash_t0;

/* Default-Landscape is the iPad's 4:3: beside it (hardware 2026-09-28:
 * black bars) its background goes on -- blue, lighter bands rising to the
 * right at the menu's 34 degrees, nothing else at its edges -- each side
 * its edge column drawn out along the bands (a sheared quad: the colour at
 * (x, y) is the edge's at y + (x - edge) tan 34 as it rises) */
static void splash_sides(const LabTex *t, float sx, float sw, float h) {
  const float k = 0.6745f; /* tan 34 */
  float cw = hd_cw(), tw = t->tw, th = t->th;
  float ul = 1.5f / (float)t->w * tw, ur = ((float)t->w - 1.5f) / (float)t->w * tw;
  /* left: x in [0, sx], the edge at sx */
  float xl[8] = {0, 0, sx, 0, 0, h, sx, h};
  float vl[8] = {ul, -k * sx / h * th, ul, 0, ul, (1.0f - k * sx / h) * th, ul, th};
  lab_draw_quad_raw(t, xl, vl, 0xffffffffu, 0);
  /* right: x in [sx + sw, cw], the edge at sx + sw */
  float e = sx + sw, d = cw - e;
  float xr[8] = {e, 0, cw, 0, e, h, cw, h};
  float vr[8] = {ur, 0, ur, k * d / h * th, ur, th, ur, (1.0f + k * d / h) * th};
  lab_draw_quad_raw(t, xr, vr, 0xffffffffu, 0);
}

static void splash_frame(void) {
  float cw = hd_cw();
  lab_draw_rect(0, 0, cw, hd_ch(), 0x000000ffu);
  const LabTex *t = hd_tex("Default-Landscape");
  if (t) {
    float h = hd_ch(), w = h * (float)t->w / (float)t->h;
    if (cw > w + 1)
      splash_sides(t, (cw - w) * 0.5f, w, h);
    lab_draw_image(t, (cw - w) * 0.5f, 0, w, h, 0xffffffffu);
  }
  float el = ui_time() - g_splash_t0;
  if ((lab_audio_ready() && el > 1.0f) || el > 8.0f)
    ui_reset(SCR_MAIN);
}

/* ============================================================ main menu
 * main-menu.png (1024 x 1024; @2x 2048): the bars' strip (rows 0-355: the
 * paper, yellow / orange / grey lines at 106 / 190 / 273), the labels in
 * cream and pressed (dark), the icons at the top, the title, the board. On
 * the iPad the strip lay across the screen at about -34 degrees, the labels
 * in its bands. */
typedef struct {
  float x, y, w, h;
} Rc;

static const Rc k_strip = {0, 0, 1024, 356};
static const Rc k_lbl[3][2] = {
    {{1, 358, 325, 50}, {1, 408, 325, 50}},     /* Single player */
    {{0, 458, 302, 50}, {0, 508, 302, 50}},     /* Multi player (a row lower: 457 took the foot of the dark
                                                 * Single player's "g", a line over the "u", hardware 2026-09-27) */
    {{355, 358, 391, 49}, {355, 408, 391, 49}}, /* Download levels */
};
static const Rc k_title = {0, 614, 364, 78};
static const Rc k_settings[2] = {{354, 466, 64, 68}, {354, 542, 64, 68}};
static const Rc k_create[2] = {{545, 469, 58, 63}, {545, 545, 58, 63}};
static const Rc k_board = {644, 643, 368, 346};
/* the bands (the strip's rows): the title's, then each label's */
static const float k_band[4][2] = {{0, 105}, {119, 189}, {204, 272}, {289, 355}};

#define MM_ANG (-34.0f * 3.14159265f / 180.0f)
#define MM_STRIP_S 1.46f  /* the strip, across its bands */
#define MM_LABEL_S 1.15f  /* the labels and the title */
static const float k_origin[2] = {2.3f, 314.6f}; /* the bands' frame: its (0, 0) on the screen */

static float g_mm_t0;
static int g_mm_pop; /* 0, or the popover over the menu: 1 settings, 2 awards, 3 credits */

static const LabTex *atlas(void) { return hd_tex("textures-ipad-full/main-menu"); }

/* The main menu is laid out as the iPad's, 768 points tall ("menu
 * points"), and drawn at the canvas's scale: the same picture on the screen,
 * while the screens and popovers are drawn a fifth bigger. */
static float MS(void) { return hd_ch() / 768.0f; }
static float mcw(void) { return hd_cw() / MS(); } /* the canvas's width in menu points */

/* the bands' frame -> the screen */
static void mm_pt(float lx, float ly, float *sx, float *sy) {
  float c = cosf(MM_ANG), s = sinf(MM_ANG), m = MS();
  *sx = (k_origin[0] + lx * c - ly * s) * m;
  *sy = (k_origin[1] + lx * s + ly * c) * m;
}

/* a rect of the atlas drawn in the bands' frame at (lx, ly), scale k,
 * stretched to w_scale along its bands; the screen box it covers out */
static void mm_part(const Rc *r, float lx, float ly, float k, float stretch, uint32_t rgba, Rc *box) {
  const LabTex *t = atlas();
  if (!t)
    return;
  float atl = (float)t->w / 1024.0f; /* the atlas is @1x or @2x */
  float w = r->w * k * stretch, h = r->h * k;
  float xy[8], uv[8];
  const float cx[4] = {0, 1, 0, 1}, cy[4] = {0, 0, 1, 1};
  float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
  for (int i = 0; i < 4; i++) {
    mm_pt(lx + cx[i] * w, ly + cy[i] * h, &xy[i * 2], &xy[i * 2 + 1]);
    uv[i * 2] = (r->x + cx[i] * r->w) * atl / (float)t->w * t->tw;
    uv[i * 2 + 1] = (r->y + cy[i] * r->h) * atl / (float)t->h * t->th;
    minx = fminf(minx, xy[i * 2]), maxx = fmaxf(maxx, xy[i * 2]);
    miny = fminf(miny, xy[i * 2 + 1]), maxy = fmaxf(maxy, xy[i * 2 + 1]);
  }
  lab_draw_quad_raw(t, xy, uv, rgba, 1);
  if (box)
    *box = (Rc){minx, miny, maxx - minx, maxy - miny};
}

/* a rect of the atlas upright at (x, y), scale k (menu points) */
static void mm_flat(const Rc *r, float x, float y, float k, uint32_t rgba) {
  const LabTex *t = atlas();
  if (!t)
    return;
  float atl = (float)t->w / 1024.0f, m = MS();
  lab_draw_image_uv(t, x * m, y * m, r->w * k * m, r->h * k * m, r->x * atl / (float)t->w, r->y * atl / (float)t->h,
                    (r->x + r->w) * atl / (float)t->w, (r->y + r->h) * atl / (float)t->h, rgba);
}

static void mm_background(void) {
  float cw = hd_cw();
  const LabTex *bg = hd_tex("textures-ipad/common/main-menu-bg");
  if (bg)
    lab_draw_image(bg, 0, 0, cw, hd_ch(), 0xffffffffu);
  else
    lab_draw_rect(0, 0, cw, hd_ch(), 0x2e7898ffu);
  /* the strip across the screen, long enough for its width */
  mm_part(&k_strip, -760, 0, MM_STRIP_S, 2400.0f / (1024.0f * MM_STRIP_S), 0xffffffffu, NULL);
}

static void mm_open(int i) {
  static const int to[3] = {SCR_PACKS, SCR_PACKS, SCR_DOWNLOAD};
  hd_packs_set_multi(i == 1);
  if (i == 2)
    hd_online_reset();
  ui_push(to[i]);
}

static void mm_labels(int interactive) {
  float now = ui_time();
  float f = fminf(fmaxf((now - g_mm_t0) / 0.4f, 0), 1);
  float e = 1.0f - (1.0f - f) * (1.0f - f) * (1.0f - f);
  float slide = (1.0f - e) * -500.0f;
  uint32_t a = (uint32_t)(255.0f * e);
  /* the title in the first band */
  float tl = k_title.h * MM_LABEL_S;
  mm_part(&k_title, 70 + slide * 0.6f, (k_band[0][1] * MM_STRIP_S - tl) * 0.5f, MM_LABEL_S, 1, 0xffffff00u | a,
          NULL);
  for (int i = 0; i < 3; i++) {
    int id = 100 + i;
    int focus = interactive && ui_focused(id);
    const LabPad *p = ui_pad;
    int down = interactive && (ui_down(id) || (focus && (p->held & HidNpadButton_A)));
    float lh = k_lbl[i][0].h * MM_LABEL_S;
    float ly = (k_band[i + 1][0] + k_band[i + 1][1]) * 0.5f * MM_STRIP_S - lh * 0.5f;
    float lx = -24 + slide * (1.0f + 0.25f * (float)i);
    Rc box;
    if (focus && f >= 1) {
      float gx, gy, w = k_lbl[i][0].w * MM_LABEL_S;
      mm_pt(lx + w * 0.5f, ly + lh * 0.5f, &gx, &gy);
      lab_draw_glow_at(gx, gy, (w * 0.5f + 14) * MS(), (lh * 0.5f + 4) * MS(), MM_ANG * 180.0f / 3.14159265f, 1.3f);
    }
    mm_part(&k_lbl[i][down ? 1 : 0], lx, ly, MM_LABEL_S, 1, 0xffffff00u | a, &box);
    if (interactive && ui_button(id, box.x, box.y, box.w, box.h, UI_NORING))
      mm_open(i);
  }
}

static void mm_board(void) {
  float k = 0.95f;
  mm_flat(&k_board, mcw() - 226 - k_board.w * k * 0.5f, 248 - k_board.h * k * 0.5f, k, 0xffffffffu);
}

/* the ball at the bottom right: Awards (the phone's drawable, with its
 * caption) */
static const char *ball_name(int down) {
  static const char *const n[4][2] = {{"steel", "steel_down"}, {"bronze", "bronze_down"},
                                      {"silver", "silver_down"}, {"gold", "gold_down"}};
  int b = lab_reg_get_int("setting-ball-selected", 0);
  if (b < 0 || b > 3)
    b = 0;
  return n[b][down ? 1 : 0];
}

/* (canvas points) */
static void ball_rect(float *x, float *y, float *w, float *h) {
  const LabTex *t = lab_tex(ball_name(0));
  float m = MS();
  *w = (t ? lab_tex_dp_w(t) * 1.1f : 60) * m, *h = (t ? lab_tex_dp_h(t) * 1.1f : 70) * m;
  *x = hd_cw() - 30 * m - *w, *y = hd_ch() - 48 - *h; /* over the controls' strip (its own caption) */
}

static void mm_icons(int interactive) {
  const float k = 1.05f;
  const Rc *set = &k_settings[interactive && ui_down(1) ? 1 : 0];
  const Rc *cre = &k_create[interactive && ui_down(2) ? 1 : 0];
  float sx = 75 - k_settings[0].w * k * 0.5f, sy = 18;
  float cx = 170 - k_create[0].w * k * 0.5f, cy = 20;
  float m = MS();
  if (interactive) {
    if (ui_button(1, sx * m, sy * m, k_settings[0].w * k * m, k_settings[0].h * k * m, UI_NORING)) {
      g_mm_pop = 1;
      ui_focus(200);
    }
    if (ui_button(2, cx * m, cy * m, k_create[0].w * k * m, k_create[0].h * k * m, UI_NORING)) {
      hd_online_reset();
      ui_push(SCR_CREATE);
    }
    if (ui_focused(1))
      lab_draw_glow_behind(sx * m, sy * m, k_settings[0].w * k * m, k_settings[0].h * k * m);
    if (ui_focused(2))
      lab_draw_glow_behind(cx * m, cy * m, k_create[0].w * k * m, k_create[0].h * k * m);
  }
  mm_flat(set, sx, sy, k, 0xffffffffu);
  mm_flat(cre, cx, cy, k, 0xffffffffu);
  float bx, by, bw, bh;
  ball_rect(&bx, &by, &bw, &bh);
  if (interactive) {
    if (ui_button(3, bx, by, bw, bh, UI_NORING)) {
      scr_awards_load();
      g_mm_pop = 2;
      ui_focus(300);
    }
    if (ui_focused(3))
      lab_draw_glow_behind(bx, by, bw, bh);
  }
  const LabTex *b = lab_tex(ball_name(interactive && ui_down(3)));
  if (b)
    lab_draw_image(b, bx, by, bw, bh, 0xffffffffu);
}

static void quit_answer(int yes) {
  if (yes)
    scr_request_quit();
}

static void settings_pop(int over_game);
static void awards_pop(void);
static void credits_pop(void);

/* the main menu drawn (under a popover too) */
static void mm_draw(int interactive) {
  mm_background();
  mm_board();
  mm_labels(interactive);
  mm_icons(interactive);
}

static void mm_frame(void) {
  int pop = g_mm_pop;
  mm_draw(!pop);
  if (pop == 1) {
    settings_pop(0);
    return;
  }
  if (pop == 2) {
    awards_pop();
    return;
  }
  if (pop == 3) {
    credits_pop();
    return;
  }
  ui_default_focus(100);
  if (ui_back())
    ui_confirm("Labyrinth 2", "Quit the game?", "Quit", "Cancel", quit_answer);
  if (ui_pressed(HidNpadButton_Plus)) {
    g_mm_pop = 1;
    ui_focus(200);
  }
  ui_hint("A", "Select");
  ui_hint("+", "Settings");
  ui_hint("B", "Quit");
}

/* ============================================================ settings */
static float g_cal_k, g_cal_l, g_tilt_o, g_tilt_p;
static const char *const k_tilt_names[3] = {"Stick", "Motion", "Both"};

static void pop_close(int over_game) {
  lab_reg_save();
  if (over_game) {
    scr_settings_back();
    return;
  }
  int was = g_mm_pop;
  g_mm_pop = 0;
  ui_focus(was == 2 ? 3 : 1);
}

static void settings_pop(int over_game) {
  enum { R_CAL, R_SOUND, R_TILTVIEW, R_GHOST, R_TILTWITH, R_CREDITS, R_N };
  static const char *const bgs[R_N] = {"menugraphics/bg_settings_calib", "menugraphics/bg_settings_sound",
                                       "menugraphics/bg_settings_tiltwalls_empty", "menugraphics/bg_settings_tactile",
                                       "menugraphics/bg_settings_tiltwalls_empty", "menugraphics/bg_settings_calib"};
  static const char *const labels[R_N] = {"Calibrate", "Sound FX", "Tilt view", "Ghost ball", "Tilt with", "Credits"};
  const float rw = 320, rh = 71, pad = 8, top = 44;
  const int rows = over_game ? R_CREDITS : R_N; /* over a level: no Credits */
  float w = rw + 2 * pad, h = top + rh * rows + pad;
  float x = over_game ? (hd_cw() - w) * 0.5f : 24, y = over_game ? (hd_ch() - h) * 0.5f : 104 * MS();
  if (y + h > hd_ch() - 8)
    y = hd_ch() - 8 - h;
  if (over_game) {
    lab_draw_rect(0, 0, hd_cw(), hd_ch(), 0x00000070u);
    hd_popover(x, y, w, h, x + w * 0.5f, -1);
  } else {
    hd_popover(x, y, w, h, 75 * MS(), 1);
  }
  if (hd_tex("menugraphics/head_settings")) {
    float tw = hd_w("menugraphics/head_settings"), th = hd_h("menugraphics/head_settings");
    hd_pic("menugraphics/head_settings", x + (w - tw) * 0.5f, y + (top - th) * 0.5f, 0xffffffffu);
  } else {
    lab_draw_text(x + w * 0.5f, y + 28, 18, 0xffffffffu, LAB_CENTER, 1, "Settings");
  }
  float tilt[3];
  lab_input_tilt(ui_pad, tilt);
  for (int r = 0; r < rows; r++) {
    float ry = y + top + rh * (float)r, rx = x + pad;
    hd_pic_wh(bgs[r], rx, ry, rw, rh, 0xffffffffu);
    lab_draw_text(rx + 88, ry + rh * 0.5f + 7, 19, 0x202020ffu, LAB_LEFT, 1, labels[r]);
    float bx = rx + rw - 12 - 94, by = ry + (rh - 27) * 0.5f;
    int id = 200 + r;
    int hit = ui_button(id, bx - 4, by - 6, 102, 39, UI_NORING);
    int focus = ui_focused(id), down = ui_down(id);
    if (r == R_CAL) {
      if (hit) {
        lab_input_calibrate();
        ui_toast(dcr_config()->tilt == LAB_TILT_STICK ? "Level position kept (motion is off: ZL in a level)"
                                                      : "Calibrated: this position is level (kept)");
      }
      /* the cross follows the tilt */
      g_cal_k = g_cal_k * 0.8f - 0.2f * tilt[0] * 36.0f;
      g_cal_l = g_cal_l * 0.8f + 0.2f * tilt[1] * 36.0f;
      float cw = hd_w("menugraphics/calibrate_cross");
      float cx = rx + 11 + (54 - cw) * 0.5f + fminf(fmaxf(-g_cal_k, -17), 17);
      float cy = ry + 8 + (54 - cw) * 0.5f + fminf(fmaxf(-g_cal_l, -17), 17);
      hd_pic("menugraphics/calibrate_cross", cx, cy, 0xffffffffu);
      hd_rbutton(bx + 14, by - 1, 80, 29, "SET", focus, down);
    } else if (r == R_SOUND || r == R_TILTVIEW || r == R_GHOST) {
      const char *key = r == R_SOUND ? "setting-sound-effects" : r == R_TILTVIEW ? "setting-acc-perspective"
                                                                                 : "setting-ghostball";
      int on = lab_reg_get_int(key, 1) == 1;
      if (hit) {
        on = !on;
        lab_reg_set_int(key, on);
        if (r == R_SOUND) {
          lab_audio_enable(on);
          lab_audio_click();
        }
        if (r == R_TILTVIEW)
          lab_game_set_tilt_view(on); /* a level under these too */
      }
      if (r == R_TILTVIEW) {
        /* a box in perspective that leans with the board */
        float ox = rx + 18, oy = ry + 14, s = 40;
        float fx = rx + 18 + (s - 25) * 0.5f, fy = ry + 14 + (s - 25) * 0.5f;
        if (on) {
          g_tilt_o = g_tilt_o * 0.7f + tilt[0] * 0.3f;
          g_tilt_p = g_tilt_p * 0.7f + tilt[1] * 0.3f;
          fx = fminf(fmaxf(fx - g_tilt_o * 27, ox), ox + s - 25);
          fy = fminf(fmaxf(fy - g_tilt_p * 27, oy), oy + s - 25);
        }
        uint32_t grey = 0xc8c8c8ffu;
        lab_draw_line(fx, fy, ox, oy, 2, grey);
        lab_draw_line(fx + 25, fy, ox + s, oy, 2, grey);
        lab_draw_line(fx, fy + 25, ox, oy + s, 2, grey);
        lab_draw_line(fx + 25, fy + 25, ox + s, oy + s, 2, grey);
        lab_draw_frame(fx, fy, 25, 25, 2, grey);
        lab_draw_frame(ox, oy, s, s, 2, 0xffffffffu);
      } else if (r == R_GHOST) {
        const LabTex *b = hd_tex("menugraphics/ball_steel");
        if (b)
          lab_draw_image_uv(b, rx + 16, ry + 12, 46, 46 * 0.8f * lab_tex_pt_h(b) / fmaxf(1, lab_tex_pt_w(b)), 0, 0.2f,
                            1, 1, 0xffffff90u);
      }
      hd_switch(bx, by, on, focus, down);
    } else if (r == R_TILTWITH) {
      int tw = dcr_config()->tilt;
      if (hit) {
        tw = (tw + 1) % 3;
        dcr_config_set_tilt(tw);
        lab_reg_set_int("port-tilt", tw);
      }
      /* the tile: a stick, a controller tilting, or both */
      float icx = rx + 42, icy = ry + 36;
      if (tw != LAB_TILT_MOTION) {
        float sx = tw == LAB_TILT_BOTH ? icx - 9 : icx;
        lab_draw_rrect(sx - 12, icy - 12, 24, 24, 12, 0xffffff40u);
        lab_draw_rrect(sx - 7 + tilt[0] * 5, icy - 7 - tilt[1] * 5, 14, 14, 7, 0xffffffffu);
      }
      if (tw != LAB_TILT_STICK) {
        float mx = tw == LAB_TILT_BOTH ? icx + 12 : icx;
        float a = fminf(fmaxf(tilt[0], -1), 1) * 0.5f, ca = cosf(a), sa = sinf(a);
        for (int k = -9; k <= 9; k += 3) /* a bar leaning with the tilt */
          lab_draw_rrect(mx + ca * (float)k - 2, icy + sa * (float)k - 2, 4, 4, 2, 0xffffffffu);
      }
      hd_rbutton(bx + 4, by - 1, 90, 29, k_tilt_names[tw], focus, down);
      lab_draw_text(rx + 88, ry + rh * 0.5f + 24, 12, 0x404040ffu, LAB_LEFT, 1,
                    tw == LAB_TILT_STICK ? "the left stick" : tw == LAB_TILT_MOTION ? "the controller's motion"
                                                                                  : "motion and the stick");
    } else {
      /* the tile: an "i" */
      lab_draw_rrect(rx + 25, ry + 19, 34, 34, 17, 0xffffffffu);
      lab_draw_text(rx + 42, ry + 45, 26, 0x101010ffu, LAB_CENTER, 1, "i");
      if (hit) {
        if (over_game) {
          ui_toast("Credits: from the main menu's Settings");
        } else {
          g_mm_pop = 3;
          ui_focus(250);
          return;
        }
      }
      hd_rbutton(bx + 4, by - 1, 90, 29, "Show", focus, down);
    }
  }
  ui_default_focus(200);
  if (ui_back() || ui_pressed(HidNpadButton_Plus)) {
    pop_close(over_game);
    return;
  }
  ui_hint("A", "Change");
  ui_hint("B", over_game ? "Back to the game" : "Close");
}

/* ============================================================ awards */
static void awards_pop(void) {
  const ScrAwards *A = scr_awards();
  static float scroll;
  float bx, by, bw, bh;
  ball_rect(&bx, &by, &bw, &bh);
  const float w = 420, pad = 8;
  float h = fminf(620, by - 18 - 12);
  float x = hd_cw() - 20 - w, y = by - 18 - h;
  hd_popover(x, y, w, h, bx + bw * 0.5f, 0);
  float ix = x + pad, iw = w - 2 * pad, top = y + pad;
  /* the pane: the balls, the counts */
  const float pane = 170;
  hd_pic_wh("menugraphics-ipad/bg-light-blue", ix, top, iw, pane, 0xffffffffu);
  int sel = lab_reg_get_int("setting-ball-selected", 0);
  static const char *const names[4] = {"steel", "bronze", "silver", "gold"};
  float bwid = hd_w("menugraphics/ball_steel"), gap = (iw - 4 * bwid) / 5;
  /* the focused one glows as the ball on the menu does (behind them all:
   * the ones beside it over its halo), no ring -- round the ball, not its
   * picture (73 x 84: its name over it, the ball 68 across at 36.5, 47) */
  for (int b = 0; b < 4; b++)
    if (ui_focused(300 + b)) {
      float px = ix + gap + (float)b * (bwid + gap), k = bwid / 73.0f;
      lab_draw_scissor(ix, top, iw, pane);
      lab_draw_glow_behind(px + 2.5f * k, top + 8 + 13 * k, 68 * k, 68 * k);
      lab_draw_scissor(0, 0, 0, 0);
    }
  for (int b = 0; b < 4; b++) {
    int unlocked = b == 0 || A->balls[b - 1];
    char n[64];
    snprintf(n, sizeof n, "menugraphics/ball_%s%s", names[b], !unlocked ? "_unachived" : sel == b ? "_highlight" : "");
    float px = ix + gap + (float)b * (bwid + gap), py = top + 8;
    if (ui_button(300 + b, px, py, bwid, hd_h(n), UI_NORING) && unlocked) {
      lab_reg_set_int("setting-ball-selected", b);
      lab_reg_save();
    }
    hd_pic(n, px, py, 0xffffffffu);
  }
  char s[96];
  float ty = top + 8 + 84 + 20;
  snprintf(s, sizeof s, "Number of awards: %d", A->count);
  lab_draw_text(ix + 20, ty, 14, 0x101010ffu, LAB_LEFT, 1, s);
  int t = A->time_played;
  if (t >= 3600)
    snprintf(s, sizeof s, "Time played: %d h %d m %d s", t / 3600, t / 60 % 60, t % 60);
  else
    snprintf(s, sizeof s, "Time played: %d m %d s", t / 60, t % 60);
  lab_draw_text(ix + 20, ty + 18, 14, 0x101010ffu, LAB_LEFT, 1, s);
  snprintf(s, sizeof s, "Distance rolled: %.2f m (%.2f feet)", (double)A->distance, (double)A->distance * 3.2808);
  lab_draw_text(ix + 20, ty + 36, 14, 0x101010ffu, LAB_LEFT, 1, s);
  hd_pic_wh("menugraphics/line_awards", ix, top + pane, iw, 3, 0xffffffffu);
  /* the list */
  const float rh = 72, lt = top + pane + 3, lb = y + h - pad;
  float content = rh * (float)A->n;
  ui_scroll(ix, lt, iw, lb - lt, &scroll, content - (lb - lt));
  lab_draw_scissor(ix, lt, iw, lb - lt);
  ui_clip(ix, lt, iw, lb - lt);
  for (int i = 0; i < A->n; i++) {
    float ry = lt + rh * (float)i - scroll;
    int id = 310 + i;
    ui_button(id, ix, ry, iw, rh, UI_NORING);
    if (ry + rh < lt || ry > lb)
      continue;
    lab_draw_rect(ix, ry, iw, rh, A->got[i] ? 0xd8e394ffu : 0xd6dadaffu);
    lab_draw_rect(ix, ry + rh - 1, iw, 1, 0x00000030u);
    /* the iPad's icon: the phone's name with dashes (completed-clean-holes-01:
     * award_icon_clean_holes_1, award-icon-clean-holes-1), else the phone's */
    const char *ph = A->got[i] ? scr_award_icon(A->id[i]) : "award_icon_notachieved";
    const LabTex *it = NULL;
    if (ph) {
      char icon[96];
      snprintf(icon, sizeof icon, "menugraphics/%s", ph);
      for (char *c = icon; *c; c++)
        if (*c == '_')
          *c = '-';
      it = hd_tex(icon);
      if (!it)
        it = lab_tex(ph);
    }
    if (it)
      lab_draw_image(it, ix + 8, ry + 6, 60, 60, 0xffffffffu);
    float tx = ix + 80;
    hd_text_fit(tx, ry + 24, 17, 0x101010ffu, 1, iw - 90, A->name[i]);
    hd_text_fit(tx, ry + 42, 12, 0x505050ffu, 1, iw - 90, A->desc[i]);
    float pw = iw - 90 - 44, px = tx, py = ry + 50, g = 2;
    uint32_t bgc = A->got[i] ? 0x70754affu : 0xa6a394ffu, fgc = A->got[i] ? 0x8fab30ffu : 0x549ea6ffu;
    lab_draw_rect(px, py, pw, 14, bgc);
    lab_draw_rect(px + g, py + g, fminf(fmaxf(A->prog[i], 0), 1) * (pw - 2 * g), 14 - 2 * g, fgc);
    snprintf(s, sizeof s, "%d%%", (int)(100.0f * A->prog[i]));
    lab_draw_text(px + pw + 6, py + 12, 12, 0x505050ffu, LAB_LEFT, 1, s);
    if (ui_focused(id))
      lab_draw_ring(ix + 2, ry + 2, iw - 4, rh - 4, 6, 2.5f, 0xffd23cffu);
  }
  lab_draw_scissor(0, 0, 0, 0);
  ui_clip(0, 0, 0, 0);
  float fx, fy, fw, fh;
  if (ui_focus_rect(&fx, &fy, &fw, &fh) && ui_focus_id() >= 310) {
    if (fy < lt)
      scroll -= lt - fy;
    else if (fy + fh > lb)
      scroll += fy + fh - lb;
    scroll = fminf(fmaxf(scroll, 0), fmaxf(0, content - (lb - lt)));
  }
  ui_default_focus(300 + sel);
  if (ui_back()) {
    pop_close(0);
    return;
  }
  ui_hint("A", "Choose the ball");
  ui_hint("RS", "Scroll");
  ui_hint("B", "Close");
}

/* ============================================================ credits */
static void credits_pop(void) {
  float y = 104 * MS(), ih = hd_ch() - 12 - y - 16, iw = ih * 320.0f / 580.0f;
  const float w = iw + 16, h = ih + 16;
  float x = 24;
  hd_popover(x, y, w, h, 75 * MS(), 1);
  hd_pic_wh("menugraphics-ipad/bg_credits", x + 8, y + 8, iw, ih, 0xffffffffu);
  float ty = y + 8 + ih - 200;
  lab_draw_rrect(x + 18, ty - 10, w - 36, 196, 8, 0xffffffd0u);
  for (unsigned i = 0; i < 4; i++) {
    char buf[192];
    snprintf(buf, sizeof buf, "%s%s", k_credits[i][0], k_credits[i][1]);
    ty += lab_draw_text_box(x + 28, ty, w - 56, 14, 0x101010ffu, LAB_LEFT, 0, buf) + 8;
  }
  ui_button(250, x, y, w, h, UI_NORING | UI_NOTOUCH);
  if (ui_back() || ui_pressed(HidNpadButton_A)) {
    g_mm_pop = 1;
    ui_focus(205);
  }
  ui_hint("B", "Back");
}

/* ============================================================ the screens */
void hd_enter(int s) {
  switch (s) {
  case SCR_SPLASH: g_splash_t0 = ui_time(); break;
  case SCR_MAIN:
    g_mm_t0 = ui_time() + 0.15f;
    g_mm_pop = 0;
    lab_ui_side_info(NULL, NULL, NULL);
    break;
  case SCR_SETTINGS: scr_settings_enter(); break;
  case SCR_PACKS: hd_packs_enter(); break;
  case SCR_DOWNLOAD: hd_download_enter(); break;
  case SCR_CREATE: hd_create_enter(); break;
  case SCR_HOWTO: hd_howto_enter(); break;
  default: scr_enter_android(s); break;
  }
}

void hd_frame(int s) {
  switch (s) {
  case SCR_SPLASH: splash_frame(); break;
  case SCR_MAIN: mm_frame(); break;
  case SCR_SETTINGS:
    /* over a level (its pause screen's Settings): the level under it, as
     * it is (lab_gfx.c: this surface over the level's picture) */
    if (!scr_settings_over_game() || !lab_game_active())
      mm_draw(0);
    settings_pop(scr_settings_over_game() && lab_game_active());
    break;
  case SCR_AWARDS:
    mm_draw(0);
    awards_pop();
    break;
  case SCR_PACKS: hd_packs_frame(); break;
  case SCR_DOWNLOAD: hd_download_frame(); break;
  case SCR_CREATE: hd_create_frame(); break;
  case SCR_HOWTO: hd_howto_frame(); break;
  default: scr_frame_android(s); break;
  }
}
