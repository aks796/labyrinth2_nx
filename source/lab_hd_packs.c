/* lab_hd_packs.c -- the iPad menus' Single player and Multi player: a split
 * view, the level packs on the left (iPad levels / iPhone levels, then
 * Official / Downloaded / Faves, grouped Ongoing / New / Finished), the
 * chosen pack's info on the right: its icon, name and author, your best and
 * the designer's time, its levels as pictures (the engine's thumbnails, a
 * cover flow: the chosen one in front), the dots, Play. Multi player lists
 * the iPhone packs; choosing one opens Local play: two players on this
 * console, a board each (lab_versus.c). (The iPad's Wi-Fi and Bluetooth
 * play needed code the Android engine does not have: left out.) MIT.
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

#define WHITE 0xffffffffu

const char *dcr_game_root(void); /* main.c */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ============================================================ a pack's row */
void hd_diff_style(int difficulty, int tutorial, uint32_t *bg, const char **icon, const char **play) {
  if (tutorial)
    *bg = 0xcfe2e4ffu, *icon = "menugraphics/icon_tutorial", *play = "menugraphics/icon_play_blue";
  else if (difficulty == 1)
    *bg = 0xf5d9a6ffu, *icon = "menugraphics/icon_medium", *play = "menugraphics/icon_play_orange";
  else if (difficulty == 2)
    *bg = 0xc9c4a3ffu, *icon = "menugraphics/icon_hard", *play = "menugraphics/icon_play_black";
  else
    *bg = 0xd9e394ffu, *icon = "menugraphics/icon_easy", *play = "menugraphics/icon_play_green";
}

static void level_dots(float x, float y, int n, int finished, int current, float step) {
  if (finished > n)
    finished = n;
  for (int i = 0; i < n && i < 40; i++)
    hd_pic(i < finished ? "menugraphics/info_dot_green" : "menugraphics/info_dot_white", x + step * (float)i, y, WHITE);
  if (current >= 0 && current < n && current < 40)
    hd_pic("menugraphics/info_dot_selected", x + step * (float)current, y, WHITE);
}

float hd_group_header(float y, const char *pic) {
  hd_pic_wh(pic, 0, y, HD_LW, 15, WHITE);
  return 15;
}

int hd_pack_row(int id, float y, const LabPack *k, int selected, const char *button_icon) {
  uint32_t bg;
  const char *icon, *play;
  hd_diff_style(k->difficulty, k->tutorial, &bg, &icon, &play);
  if (k->ownlevel && !k->published)
    icon = "menugraphics/icon_unpublished";
  if (!strncmp(k->id, "YPAD0000.15", 11) || k->theme == 3)
    icon = "menugraphics/icon_brio";
  lab_draw_rect(0, y, HD_LW, 70, bg);
  lab_draw_rect(0, y + 69, HD_LW, 1, 0x00000030u);
  hd_pic_wh(icon, 5, y + 5, 60, 60, WHITE);
  if (selected)
    hd_pic_wh("menugraphics-ipad/item_focused_button", 0, y, HD_LW, 70, WHITE);
  uint32_t fg = selected ? 0xffffffffu : 0x1a1a1aff, fg2 = selected ? 0xe0f0f4ffu : 0x505a5cff;
  float tx = 76, tw = HD_LW - tx - 50;
  hd_text_fit(tx, y + 20, 12, fg2, 1, tw, k->author);
  hd_text_fit(tx, y + 41, 18, fg, 1, tw, k->name);
  if (k->nlevels > 0 && !button_icon)
    level_dots(tx, y + 48, k->nlevels, k->nfinished, k->current, 13);
  int r = 0;
  const char *b = button_icon ? button_icon : play;
  if (selected) /* on the blue row: the white ring */
    b = strstr(b, "download") ? "menugraphics/icon_download_button" : "menugraphics/icon_play_button";
  /* its button centred on the lit row's round cut-out (item_focused_button:
   * 294, 33.2), the plain and the ringed pictures alike -- their circles sit
   * at 15.8, 16.8 and 14.5, 14.6 in them (hardware 2026-09-27: the ringed
   * one 3.5 points left of the cut-out, a second ring beside it) */
  const float hx = HD_LW - 26.0f, hy = 33.2f;
  float bx = hx - (selected ? 14.5f : 15.8f), by = y + hy - (selected ? 14.6f : 16.8f);
  if (ui_button(id + 100000, HD_LW - 50, y, 50, 70, UI_NOFOCUS))
    r = 2;
  hd_pic(b, bx, by, ui_down(id + 100000) ? 0xc0c0c0ffu : WHITE);
  /* a finger's (the controller works in the list as a whole: hd_list) */
  if (ui_button(id, 0, y, HD_LW - 50, 70, UI_NOFOCUS))
    r = r ? r : 1;
  return r;
}

/* ============================================================ the info pane */
#define MAX_TH 64
static struct {
  int has;
  LabPack p;          /* a copy (the row may be a preview) */
  LabPack *row;       /* the table's row, if it is one */
  int level;
  float pos;          /* where the flow is (animated towards level) */
  LabTex *th[MAX_TH];
  uint8_t failed[MAX_TH], cached_no[MAX_TH];
  int setup;          /* 0 not yet, 1 done, -1 failed */
  int avail;          /* its levels in its zip (only those are drawn) */
  float zip_checked;  /* when the zip was last looked for */
  int tw, th_h;
  float shown_at;     /* when it was chosen */
} I;

void hd_info_release(void) {
  for (int i = 0; i < MAX_TH; i++) {
    if (I.th[i])
      lab_tex_free(I.th[i]);
    I.th[i] = NULL;
    I.failed[i] = I.cached_no[i] = 0;
  }
  if (I.setup > 0)
    lab_thumbs_release();
  I.setup = 0;
}

void hd_info_show(const LabPack *p) {
  if (!p) {
    hd_info_release();
    I.has = 0;
    return;
  }
  if (I.has && !strcmp(I.p.id, p->id) && I.p.ipad == p->ipad) {
    I.p = *p; /* its progress may have moved */
    I.row = lab_levels_find(p->id);
    return;
  }
  hd_info_release();
  I.has = 1;
  I.p = *p;
  I.row = lab_levels_find(p->id);
  I.level = p->current >= 0 && p->current < p->nlevels ? p->current : 0;
  I.pos = (float)I.level;
  I.shown_at = ui_time();
  I.avail = 0;
  I.zip_checked = -1;
}

const LabPack *hd_info_pack(void) { return I.has ? &I.p : NULL; }
int hd_info_level(void) { return I.level; }

static LabTex *thumb(int i) {
  if (i < 0 || i >= MAX_TH || i >= I.p.nlevels)
    return NULL;
  return I.th[i];
}

/* ---- the levels' pictures
 * The engine draws one (ThumbnailManager.renderThumbnail) in about half a
 * second, and the frame waits for it (hardware 2026-09-26: the menus
 * stuttered while a pack's ten pictures were drawn, one a frame). So each
 * picture is drawn once and kept (data/thumbs/<pack>-<board>-<revision>-
 * <level>.png, rows as the engine drew them), read from there after; a new
 * one is drawn only while the player is not pressing anything, the chosen
 * level first. */
static float g_last_input;

static void note_input(void) {
  const LabPad *p = ui_pad;
  if (!p)
    return;
  if (p->held || p->touch || fabsf(p->lx) > 0.2f || fabsf(p->ly) > 0.2f || fabsf(p->rx) > 0.2f ||
      fabsf(p->ry) > 0.2f)
    g_last_input = ui_time();
}

static void cache_path(char *out, size_t cap, int level) {
  snprintf(out, cap, "%s/data/thumbs/%s-%c-%d-%d.png", dcr_game_root(), I.p.id, I.p.ipad ? 'i' : 'p', I.p.revision,
           level);
}

static LabTex *cached(int level) {
  char path[512];
  cache_path(path, sizeof path, level);
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = n > 0 && n < (8 << 20) ? malloc((size_t)n) : NULL;
  size_t got = d ? fread(d, 1, (size_t)n, f) : 0;
  fclose(f);
  LabTex *t = NULL;
  if (d && got == (size_t)n) {
    int w = 0, h = 0;
    uint8_t *px = lab_image_decode(d, (size_t)n, &w, &h);
    if (px) {
      t = lab_tex_from_rgba(px, w, h);
      free(px);
    }
  }
  free(d);
  return t;
}

static void thumbs_step(void) {
  note_input();
  if (!I.has || I.p.nlevels <= 0)
    return;
  float now = ui_time();
  /* what is kept: one a frame, the chosen level outwards */
  for (int d = 0; d < MAX_TH; d++)
    for (int s = 0; s < 2; s++) {
      int i = I.level + (s ? -d : d);
      if (i < 0 || i >= I.p.nlevels || i >= MAX_TH || I.th[i] || I.cached_no[i])
        continue;
      I.th[i] = cached(i);
      I.cached_no[i] = !I.th[i];
      if (I.th[i])
        return;
    }
  /* drawing a new one: not while the player moves about, nor for a pack
   * whose zip is not here yet (one looked at before it is downloaded: the
   * engine's first picture of nothing crashed, hardware 2026-09-26) */
  /* the pictures have the focus: the player is looking at the levels, each
   * one drawn in turn; else only the one in front, once the ring has rested
   * on the pack a moment (hardware 2026-09-27: browsing the lists drew a
   * pack's ten pictures a second apart, the menus at 26 fps) */
  int looking = ui_focus_id() == 600;
  if (now - g_last_input < (looking ? 0.5f : 0.8f) || now - I.shown_at < 0.35f || lab_game_active())
    return;
  if (I.setup == 0) {
    /* only levels its zip has: the engine reads a level's file without
     * looking whether it is there (a server pack not here yet, or one
     * whose list said more levels than it has, crashed it: hardware
     * 2026-09-26). Looked for again every second (its preview arriving). */
    if (I.avail <= 0) {
      if (I.zip_checked >= 0 && now - I.zip_checked < 1.0f)
        return;
      I.zip_checked = now;
      I.avail = lab_files_pack_levels(I.p.id, I.p.ipad, I.p.nlevels < MAX_TH ? I.p.nlevels : MAX_TH);
      if (I.avail <= 0)
        return;
      if (I.avail < I.p.nlevels && I.avail < MAX_TH)
        debugPrintf("[thumbs] %s: %d of its %d levels in its zip: pictures of those only\n", I.p.id, I.avail,
                    I.p.nlevels);
    }
    /* the size as shown */
    float k = ui_px_per_unit();
    I.th_h = (int)((HD_CANVAS_H - 51 - 46 - 70 - 44 - 53 - 108) * k); /* as tall as shown */
    I.tw = I.p.ipad ? I.th_h * 3 / 4 : I.th_h * 2 / 3;
    I.setup = lab_thumbs_setup(&I.p, I.tw, I.th_h) == 0 ? 1 : -1;
    return;
  }
  if (I.setup < 0)
    return;
  /* only those the flow shows (about five each side): more as it moves */
  for (int d = 0; d <= (looking ? 5 : 0); d++)
    for (int s = 0; s < 2; s++) {
      int i = I.level + (s ? -d : d);
      if (i < 0 || i >= I.p.nlevels || i >= MAX_TH || I.th[i] || I.failed[i])
        continue;
      if (i >= I.avail) {
        I.failed[i] = 1; /* not in its zip */
        continue;
      }
      uint8_t *px = NULL;
      I.th[i] = lab_thumbs_render_px(i, &px);
      I.failed[i] = !I.th[i];
      if (px) {
        char path[512];
        cache_path(path, sizeof path, i);
        if (lab_files_write_png(path, px, I.tw, I.th_h) != 0)
          debugPrintf("[thumbs] %s level %d: not kept (the SD card)\n", I.p.id, i + 1);
        free(px);
      }
      g_last_input = ui_time() - 0.5f; /* the next one a little after */
      return;
    }
}

/* a picture as a quad: its left and right edges (x, their centre y, their
 * heights), with its reflection under it */
static void flow_quad(const LabTex *t, float xl, float xr, float cy, float hl, float hr, uint32_t a) {
  float xy[8] = {xl, cy - hl * 0.5f, xr, cy - hr * 0.5f, xl, cy + hl * 0.5f, xr, cy + hr * 0.5f};
  const float uv[8] = {0, 1, 1, 1, 0, 0, 1, 0};
  if (t)
    lab_draw_quad_raw(t, xy, uv, 0xffffff00u | a, 1);
  else {
    const LabTex *e = hd_tex("CoverFlow_Empty");
    const float ue[8] = {0, 0, e ? e->tw : 1, 0, 0, e ? e->th : 1, e ? e->tw : 1, e ? e->th : 1};
    if (e)
      lab_draw_quad_raw(e, xy, ue, 0xffffff00u | a, 1);
  }
  /* the reflection: the lower third turned over, fading in strips */
  if (!t)
    return;
  const int n = 8;
  for (int i = 0; i < n; i++) {
    float f0 = (float)i / n, f1 = (float)(i + 1) / n; /* of the reflection's height (a third) */
    float yl0 = cy + hl * 0.5f + hl * 0.33f * f0, yl1 = cy + hl * 0.5f + hl * 0.33f * f1;
    float yr0 = cy + hr * 0.5f + hr * 0.33f * f0, yr1 = cy + hr * 0.5f + hr * 0.33f * f1;
    float q[8] = {xl, yl0, xr, yr0, xl, yl1, xr, yr1};
    float v0 = f0 * 0.33f, v1 = f1 * 0.33f; /* from the bottom up (the texture is bottom-up) */
    float u[8] = {0, v0, 1, v0, 0, v1, 1, v1};
    uint32_t ra = (uint32_t)((float)a * 0.30f * (1.0f - f0));
    lab_draw_quad_raw(t, q, u, 0xffffff00u | ra, 1);
  }
}

static void flow(float x0, float w, float cy, float H, int n, int *tap) {
  float asp = I.p.ipad ? 0.75f : 2.0f / 3.0f, W = H * asp;
  float cx = x0 + w * 0.45f;
  I.pos += ((float)I.level - I.pos) * 0.2f;
  if (fabsf((float)I.level - I.pos) < 0.002f)
    I.pos = (float)I.level;
  /* far ones first */
  int order[128], m = 0;
  for (int dd = 12; dd >= 0; dd--)
    for (int s = 0; s < 2; s++) {
      int i = (int)floorf(I.pos + 0.5f) + (s ? dd : -dd);
      if (dd == 0 && s)
        continue;
      if (i >= 0 && i < n && m < 128)
        order[m++] = i;
    }
  for (int k = 0; k < m; k++) {
    int i = order[k];
    float d = (float)i - I.pos, ad = fabsf(d), sg = d < 0 ? -1.0f : 1.0f;
    /* in front (d = 0): flat, full size; aside (|d| >= 1): turned, smaller,
     * 60 points apart beyond the first */
    float t = clampf(ad, 0, 1);
    float wd = W * (1.0f - 0.62f * t);                   /* its width as seen */
    float off = t * (W * 0.5f + 44) + (ad > 1 ? (ad - 1) * 58 : 0);
    float hn = H * (1.0f - 0.06f * t), hf = H * (1.0f - 0.2f * t); /* near edge, far edge */
    float xc = cx + sg * off;
    float xl = xc - wd * 0.5f, xr = xc + wd * 0.5f;
    float hl = sg > 0 ? hn : hf, hr = sg > 0 ? hf : hn;
    if (xr < x0 - 20 || xl > x0 + w + 20)
      continue;
    uint32_t a = (uint32_t)(255.0f * clampf(1.4f - ad * 0.12f, 0.3f, 1.0f));
    flow_quad(thumb(i), xl, xr, cy, hl, hr, a);
    if (tap && ui_pad->touch_ended && ui_pad->tx >= xl && ui_pad->tx < xr && ui_pad->ty > cy - hl * 0.5f &&
        ui_pad->ty < cy + hl * 0.5f)
      *tap = i;
  }
}

static void fmt_time(char *out, size_t cap, int ms) {
  if (ms <= 0) {
    snprintf(out, cap, "-");
    return;
  }
  int s = ms / 1000, cs = (ms % 1000) / 10;
  if (s >= 60)
    snprintf(out, cap, "%d m %d.%02d s", s / 60, s % 60, cs);
  else
    snprintf(out, cap, "%d.%02d s", s, cs);
}

/* A name broken at spaces into lines of at most w at size sz: how many (at
 * most cap); 0 when a word alone is wider (or it needs more lines). */
static int name_lines(const char *name, float sz, float w, int *st, int *ln, int cap) {
  int n = 0;
  size_t len = strlen(name), pos = 0;
  char buf[160];
  while (pos < len) {
    if (n == cap)
      return 0;
    size_t best = 0; /* the longest run from pos that fits, ending at a space or the end */
    for (size_t e = pos + 1; e <= len && e - pos < sizeof buf; e++) {
      if (e < len && name[e] != ' ')
        continue;
      memcpy(buf, name + pos, e - pos);
      buf[e - pos] = 0;
      if (lab_text_width(sz, 1, buf) > w)
        break;
      best = e;
    }
    if (!best)
      return 0;
    st[n] = (int)pos, ln[n] = (int)(best - pos), n++;
    pos = best;
    while (pos < len && name[pos] == ' ')
      pos++;
  }
  return n;
}

/* A pack's name in full at the top of the pane: smaller, then on two lines,
 * then three (hardware 2026-09-26: "A Little Bit of Everythi..." was all it
 * showed), in the width left of the times, above the pictures. The lines
 * it took. */
static int info_name(float x, float y1, float w, const char *name, const char *author) {
  const uint32_t ca = 0x5a6c72ffu, cn = 0x1e2c32ffu;
  float sz = 25;
  while (sz > 19 && lab_text_width(sz, 1, name) > w)
    sz -= 1;
  int st[3], ln[3], n = 0;
  float lsz = 19, ay = 34, ly = 60, lstep = 24;
  if (lab_text_width(sz, 1, name) > w) {
    n = name_lines(name, 19, w, st, ln, 2);
    if (!n)
      n = name_lines(name, 17, w, st, ln, 3), lsz = 17, ay = 28, ly = 50, lstep = 21;
  }
  if (n < 2) {
    hd_text_fit(x, y1 + 46, 15, ca, 1, w, author);
    hd_text_fit(x, y1 + 76, sz, cn, 1, w, name); /* one line (a word too long for it: cut) */
    return 1;
  }
  hd_text_fit(x, y1 + ay, 15, ca, 1, w, author);
  for (int i = 0; i < n; i++) {
    char a[160];
    int l = ln[i] < (int)sizeof a - 1 ? ln[i] : (int)sizeof a - 1;
    memcpy(a, name + st[i], (size_t)l);
    a[l] = 0;
    hd_text_fit(x, y1 + ly + lstep * (float)i, lsz, cn, 1, w, a);
  }
  return n;
}

/* the header strip: "Level info" (uplist, 701 wide) across the pane */
static void info_header(float x0, float w) {
  const LabTex *t = hd_tex("menugraphics-ipad/uplist");
  if (!t)
    return;
  float nw = lab_tex_pt_w(t), nh = lab_tex_pt_h(t);
  lab_draw_image(t, x0, 44, fminf(nw, w), nh, WHITE);
  if (w > nw)
    lab_draw_image_uv(t, x0 + nw, 44, w - nw, nh, 0.85f, 0, 1, 1, WHITE);
}

int hd_info_frame(int mode, int designer_only) {
  float cw = hd_cw(), x0 = HD_LW, w = cw - HD_LW;
  info_header(x0, w);
  if (!I.has) {
    lab_draw_text(x0 + w * 0.5f, HD_CANVAS_H * 0.55f, 18, 0x40606aff, LAB_CENTER, 1,
                  mode == HD_INFO_DOWNLOAD ? "Choose a level pack to see its levels" : "Choose a level pack");
    return HD_ACT_NONE;
  }
  int act = HD_ACT_NONE;
  LabPack *k = &I.p;
  if (I.row)
    I.p = *I.row;
  /* the header's button */
  const char *hb = NULL, *hbd = NULL;
  if (mode == HD_INFO_PLAY) {
    int fave = I.row && I.row->fave;
    hb = fave ? "menugraphics-ipad/btn_faves_remove" : "menugraphics-ipad/btn_faves_add";
    hbd = hb;
  } else if (mode == HD_INFO_DOWNLOAD) {
    hb = "menugraphics-ipad/btn_moreinfo_up", hbd = "menugraphics-ipad/btn_moreinfo_down";
  } else if (mode == HD_INFO_OWN && !k->published) {
    hb = "menugraphics-ipad/btn_delete_up", hbd = "menugraphics-ipad/btn_delete_down";
  }
  if (hb && hd_tex(hb)) {
    float bw = hd_w(hb), bh = hd_h(hb);
    if (hd_pic_button(603, hb, hbd, cw - 16 - bw, 44 + (53 - bh) * 0.5f, 0))
      act = mode == HD_INFO_PLAY ? HD_ACT_FAVE : mode == HD_INFO_DOWNLOAD ? HD_ACT_MOREINFO : HD_ACT_DELETE;
  }
  /* the pack */
  uint32_t bg;
  const char *icon, *play;
  hd_diff_style(k->difficulty, k->tutorial, &bg, &icon, &play);
  char big[96];
  snprintf(big, sizeof big, "menugraphics-ipad/%s", strchr(icon, '/') + 1);
  if (k->ownlevel && !k->published)
    snprintf(big, sizeof big, "menugraphics-ipad/icon_unpublished");
  if (k->theme == 3 || !strncmp(k->id, "YPAD0000.15", 11))
    snprintf(big, sizeof big, "menugraphics-ipad/icon_brio");
  float y1 = 44 + 53;
  hd_pic_wh(big, x0 + 26, y1 + 18, 78, 78, WHITE);
  /* the name: as wide as there is left of the times (the trophy beside them) */
  float tx = x0 + 124, tw = (cw - 340 - 38) - tx;
  int lines = info_name(tx, y1, tw, k->name, k->author);
  if (mode == HD_INFO_DOWNLOAD) {
    const LabTex *se = hd_tex("menugraphics/emptyStars"), *sf = hd_tex("menugraphics/stars");
    if (se && sf) {
      /* under the name (beside the times, three lines of it) */
      float sc = lines > 1 ? 1.1f : 1.3f, sy = y1 + (lines > 1 ? 91 : 86);
      float sx = lines > 2 ? cw - 340 : tx;
      if (lines > 2)
        sy = y1 + 84;
      float sw = lab_tex_pt_w(se) * sc, sh = lab_tex_pt_h(se) * sc, f = clampf((float)k->rating * 0.2f, 0, 1);
      lab_draw_image(se, sx, sy, sw, sh, WHITE);
      if (f > 0.01f)
        lab_draw_image_uv(sf, sx, sy, sw * f, sh, 0, 0, f, 1, WHITE);
    }
  }
  /* the times: yours and the designer's */
  int lvl = I.level;
  int best = designer_only ? 0 : lab_levels_best_time(k->id, lvl);
  int dt = lab_levels_designer_time(k->id, lvl);
  char a[48], b[48];
  fmt_time(a, sizeof a, best);
  fmt_time(b, sizeof b, dt);
  float rx = cw - 340;
  if (best > 0 && dt > 0 && best <= dt)
    hd_pic("menugraphics/trophy", rx - 30, y1 + 30, WHITE);
  lab_draw_text(rx, y1 + 46, 17, 0x6d8f8cffu, LAB_LEFT, 1, "Your best time:");
  lab_draw_text(rx, y1 + 70, 17, 0x6d8f8cffu, LAB_LEFT, 1, "Designer time:");
  lab_draw_text(rx + 180, y1 + 46, 17, 0x1e2c32ffu, LAB_LEFT, 1, a);
  lab_draw_text(rx + 180, y1 + 70, 17, 0x1e2c32ffu, LAB_LEFT, 1, b);
  /* the levels */
  thumbs_step();
  int n = k->nlevels;
  int top = n - 1;
  if (mode == HD_INFO_PLAY || mode == HD_INFO_OWN)
    top = k->nfinished < n ? k->nfinished : n - 1; /* up to the first not finished */
  if (top < 0)
    top = 0;
  int tap = -1;
  /* the flow between the pack's line and the dots, the Play button under */
  const float by = HD_CANVAS_H - 51 - 46, top_c = 44 + 53 + 108, bot_c = by - 34 - 36;
  float cy = (top_c + bot_c) * 0.5f, H = bot_c - top_c;
  int cur = ui_button(600, x0 + 20, cy - H * 0.5f - 10, w - 40, H + 20, UI_NORING | UI_NOTOUCH);
  (void)cur;
  flow(x0, w, cy, H, n, &tap);
  static int flow_had; /* the pictures had the focus last frame */
  static float next;   /* when a held left / right steps again */
  if (ui_focused(600)) {
    /* left and right choose the level (repeating while held); left at the
     * first goes back to the list */
    const LabPad *pp = ui_pad;
    const uint64_t L = HidNpadButton_Left | HidNpadButton_StickLLeft, R = HidNpadButton_Right | HidNpadButton_StickLRight;
    if (!(I.level == 0 && (pp->down & L)))
      ui_hold_nav(0);
    float fw = H * (I.p.ipad ? 0.75f : 2.0f / 3.0f);
    lab_draw_ring(x0 + w * 0.45f - fw * 0.5f - 6, cy - H * 0.5f - 6, fw + 12, H + 12, 8, 3, 0xffd23cffu);
    int dir = (pp->held & L) ? -1 : (pp->held & R) ? 1 : 0;
    float now = ui_time();
    int step = 0;
    if (!flow_had)
      next = now + 0.35f; /* the press that brought the focus here does not step */
    else if (pp->down & (L | R))
      step = dir, next = now + 0.35f;
    else if (dir && now >= next)
      step = dir, next = now + 0.12f;
    if (step < 0 && I.level > 0)
      I.level--, lab_audio_click();
    if (step > 0 && I.level < top)
      I.level++, lab_audio_click();
    /* A on the pictures: the level in front */
    if (ui_pressed(HidNpadButton_A))
      act = mode == HD_INFO_DOWNLOAD ? HD_ACT_DOWNLOAD : HD_ACT_PLAY;
  }
  flow_had = ui_focused(600);
  if (tap >= 0 && tap <= top && tap != I.level) {
    I.level = tap;
    lab_audio_click();
  }
  /* the dots */
  float step = 18, dx = x0 + w * 0.45f - step * (float)n * 0.5f, dy = by - 30;
  for (int i = 0; i < n && i < 40; i++) {
    const char *d = i == I.level ? "menugraphics/info_dot_black" : i < k->nfinished ? "menugraphics/info_dot_green"
                                                                                  : "menugraphics/info_dot_white";
    hd_pic_wh(d, dx + step * (float)i, dy, 14, 15, WHITE);
  }
  /* the buttons */
  if (mode == HD_INFO_OWN) {
    float bx = x0 + w * 0.45f - 184 - 10;
    if (!k->published && hd_pic_button(601, "menugraphics-ipad/btn_publish_up", "menugraphics-ipad/btn_publish_down",
                                       bx, by, 0))
      act = HD_ACT_PUBLISH;
    if (hd_pic_button(602, "menugraphics-ipad/btn_play_small_up", "menugraphics-ipad/btn_play_small_down",
                      k->published ? x0 + w * 0.45f - 92 : bx + 184 + 20, by, 0))
      act = HD_ACT_PLAY;
  } else {
    const char *up = mode == HD_INFO_DOWNLOAD ? "menugraphics-ipad/btn_download_up" : "menugraphics-ipad/btn_play_up";
    const char *dn = mode == HD_INFO_DOWNLOAD ? "menugraphics-ipad/btn_download_down" : "menugraphics-ipad/btn_play_down";
    if (hd_pic_button(601, up, dn, x0 + w * 0.45f - 183, by, 0))
      act = mode == HD_INFO_DOWNLOAD ? HD_ACT_DOWNLOAD : HD_ACT_PLAY;
  }
  return act;
}

void hd_open_game(LabPack *p, int level) {
  if (!p)
    return;
  if (level >= 0 && level < p->nlevels && level <= p->nfinished) {
    p->current = level;
    lab_levels_save();
  }
  hd_info_release(); /* the engine's screen is the game's now */
  scr_open_game(p);
}

/* ============================================================ the lists */
static struct {
  int multi, ipad, seg;
  HdScroll scroll[2][3];
  HdList list;
  char sel[64];      /* the row chosen (its id) */
  int sel_ipad;
} S = {.ipad = 1};

/* from the main menu: the list as a whole focused, not in it yet */
void hd_packs_set_multi(int on) {
  S.multi = on;
  hd_list_reset(&S.list);
}

void hd_packs_enter(void) { lab_ui_side_info(NULL, NULL, NULL); }

static const int k_kind[3] = {1, 0, 2}; /* the segments: Official, Downloaded, Faves */

/* ---------------------------------------------------- Multi player's popup */
static struct {
  int on, start_after;
  LabPack *p;
  int back_focus;
} MP;

static void mp_close(void) {
  MP.on = MP.start_after = 0;
  lab_input_local_play(0);
  ui_focus(MP.back_focus);
}

static void mp_go(void) {
  LabPack *p = MP.p;
  MP.on = MP.start_after = 0;
  ui_focus(MP.back_focus);
  hd_info_release();
  if (p && lab_versus_start(p) == 0)
    ui_push(SCR_VERSUS);
  else
    lab_input_local_play(0);
}

static void mp_controllers_done(int ok) {
  if (MP.on && MP.start_after && ok)
    mp_go();
  MP.start_after = 0;
}

static void mp_controllers(int then_start) {
  lab_input_local_play(1);
  MP.start_after = then_start;
  if (lab_controllers_request(mp_controllers_done) != 0)
    MP.start_after = 0;
}

static void mp_open(LabPack *p) {
  if (!p->preloaded) {
    lab_files_complete_pack(p);
    char why[160], msg[256];
    if (lab_files_check_pack(p, why, sizeof why)) {
      snprintf(msg, sizeof msg, "%s\n\n%s", why, "Delete it and download it again.");
      ui_confirm("Can't play this level pack", msg, "OK", NULL, NULL);
      return;
    }
  }
  memset(&MP, 0, sizeof MP);
  MP.on = 1;
  MP.p = p;
  MP.back_focus = ui_focus_id();
  ui_focus(710);
}

static void mp_frame(void) {
  float cw = hd_cw();
  lab_draw_rect(0, 0, cw, HD_CANVAS_H, 0x00000070u);
  const float w = 480;
  const char *body = MP.p && MP.p->ipad
                         ? "Two players, an iPad board each, the same level: the first ball in the goal wins it. "
                           "A Joy-Con each (held sideways), or two controllers."
                         : "Two players, a board each, the same level: the first ball in the goal wins it. A Joy-Con "
                           "each (held sideways), or two controllers.";
  int starts[24], lens[24];
  float k = ui_px_per_unit();
  float bh_text = (float)lab_text_wrap(body, 16 * k, 0, (w - 48) * k, starts, lens, 24) * 16 * 1.3f;
  float h = 92 + bh_text + 16 + 72 + 116 + 20;
  float x = (cw - w) * 0.5f, y = (HD_CANVAS_H - h) * 0.5f;
  /* the iPad's popup: light blue glass, a white rim */
  lab_draw_rrect(x - 4, y + 4, w + 8, h + 8, 16, 0x00000050u);
  lab_draw_rrect(x, y, w, h, 14, 0xf4f8faffu);
  lab_draw_rrect(x + 3, y + 3, w - 6, h - 6, 12, 0x9fcbd9ffu);
  lab_draw_text(cw * 0.5f, y + 38, 23, 0x14323cffu, LAB_CENTER, 1, "Local play");
  hd_text_fit(x + 24, y + 66, 16, 0x14323cffu, 1, w - 48, MP.p->name);
  float ty = y + 86;
  lab_draw_text_box(x + 24, ty, w - 48, 16, 0x14323cffu, LAB_CENTER, 0, body);
  ty += bh_text + 16;
  for (int i = 0; i < 2; i++) {
    const char *n = lab_input_player_name(i);
    char line[96];
    snprintf(line, sizeof line, "Player %d: %s", i + 1, n ? n : "no controller yet");
    float ly = ty + 14 + 32 * (float)i;
    hd_pic_wh(i ? "menugraphics/multiplayerpluppgreen" : "menugraphics/multiplayerpluppyellow", x + 40, ly - 11, 22, 22,
              WHITE);
    hd_text_fit(x + 72, ly + 6, 18, n ? 0x14323cffu : 0x9a3a2affu, 1, w - 100, line);
  }
  ty += 72;
  int p2 = lab_input_p2_connected();
  float bw = w - 80, bx = x + 40;
  if (ui_button(710, bx, ty, bw, 48, 0)) {
    if (p2)
      mp_go();
    else
      mp_controllers(1);
    return;
  }
  ui_button_box(bx, ty, bw, 48, p2 ? "Start" : "Start: connect player 2", ui_focused(710), ui_down(710));
  float hw = (bw - 12) * 0.5f;
  if (ui_button(711, bx, ty + 60, hw, 48, 0))
    mp_controllers(0);
  ui_button_box(bx, ty + 60, hw, 48, "Controllers", ui_focused(711), ui_down(711));
  if (ui_button(712, bx + hw + 12, ty + 60, hw, 48, 0)) {
    mp_close();
    return;
  }
  ui_button_box(bx + hw + 12, ty + 60, hw, 48, "Cancel", ui_focused(712), ui_down(712));
  ui_default_focus(710);
  if (ui_back()) {
    mp_close();
    return;
  }
  ui_hint("A", "Select");
  ui_hint("B", "Cancel");
}

/* ---------------------------------------------------- the screen */
static void play(LabPack *k, int level) {
  if (S.multi)
    mp_open(k);
  else
    hd_open_game(k, level);
}

static const char *const k_groups[3] = {"menugraphics/ongoing", "menugraphics/new", "menugraphics/finished"};

void hd_packs_frame(void) {
  float cw = hd_cw();
  int modal = MP.on;
  hd_split_bg();
  float y = 44;
  float tabs_y = y;
  y += 49;
  float seg_y = y;
  y += 48;
  const float top = y;
  /* ---- the list ---- */
  int kind = k_kind[S.seg];
  static LabPack *rows[3][1024];
  int nrows[3] = {0, 0, 0};
  int ngroups = kind == 2 ? 1 : 3;
  for (int g = 0; g < ngroups; g++) {
    nrows[g] = lab_levels_query_dev(kind == 2 ? -1 : g, kind, S.ipad, rows[g], 1024);
    if (S.multi) { /* not the tutorials */
      int m = 0;
      for (int i = 0; i < nrows[g]; i++)
        if (!rows[g][i]->tutorial)
          rows[g][m++] = rows[g][i];
      nrows[g] = m;
    }
  }
  /* the rows in order, where each is in the list (the groups' titles
   * between them) */
  static LabPack *flat[3 * 1024];
  static float fy[3 * 1024], fh[3 * 1024];
  static int fg[3 * 1024];
  int nf = 0;
  float content = 0;
  for (int g = 0; g < ngroups; g++) {
    if (!nrows[g])
      continue;
    if (ngroups > 1)
      content += 15;
    for (int i = 0; i < nrows[g]; i++, nf++, content += 70)
      flat[nf] = rows[g][i], fy[nf] = content, fh[nf] = 70, fg[nf] = g;
  }
  int chosen = -1;
  for (int i = 0; i < nf && chosen < 0; i++)
    if (S.sel_ipad == S.ipad && !strcmp(S.sel, flat[i]->id))
      chosen = i;
  if (chosen < 0 && nf) { /* the first when none is */
    chosen = 0;
    snprintf(S.sel, sizeof S.sel, "%s", flat[0]->id);
    S.sel_ipad = S.ipad;
  }
  float view = HD_CANVAS_H - top;
  HdScroll *scroll = &S.scroll[S.ipad][S.seg];
  int la = modal ? HD_LIST_NONE : hd_list(&S.list, scroll, top, view, content, fy, fh, nf, chosen, 601);
  float at = modal ? scroll->at : hd_scroll(scroll, 0, top, HD_LW, view, content);
  lab_draw_scissor(0, top, HD_LW, view);
  ui_clip(0, top, HD_LW, view);
  int tap = 0, tap_i = -1;
  for (int i = 0; i < nf; i++) {
    float ry = top - at + fy[i];
    if (ngroups > 1 && (i == 0 || fg[i] != fg[i - 1]) && ry > top - 20 && ry - 15 < HD_CANVAS_H)
      hd_group_header(ry - 15, k_groups[fg[i]]);
    if (ry + 70 < top || ry > HD_CANVAS_H)
      continue;
    LabPack *k = flat[i];
    if (modal) {
      uint32_t bg;
      const char *icon, *pl;
      hd_diff_style(k->difficulty, k->tutorial, &bg, &icon, &pl);
      lab_draw_rect(0, ry, HD_LW, 70, bg);
      hd_pic_wh(icon, 5, ry + 5, 60, 60, WHITE);
      hd_text_fit(76, ry + 41, 18, 0x1a1a1aff, 1, HD_LW - 126, k->name);
      continue;
    }
    int r = hd_pack_row(1000 + i, ry, k, hd_list_lit(&S.list, i, chosen), NULL);
    if (r)
      tap = r, tap_i = i;
  }
  if (!modal)
    hd_scroll_bar(scroll, HD_LW, top, view, content);
  lab_draw_scissor(0, 0, 0, 0);
  ui_clip(0, 0, 0, 0);
  if (!content) {
    const char *msg = kind == 2 ? "No faves yet.\n\nA level pack's Add to faves (or X in the list) puts it here."
                    : kind == 0 ? "No downloaded level packs yet.\n\nThousands of players' packs are on the "
                                  "Labyrinth 2 servers: see Download levels in the main menu."
                                : "No level packs here.";
    lab_draw_text_box(24, top + 120, HD_LW - 48, 16, 0x1e3a3cffu, LAB_CENTER, 1, msg);
  }
  /* the row the controller rests on is chosen (its info shown); at once
   * when it is played, or right takes it to the pane; a finger's too */
  int now_i = !modal ? hd_list_dwell(&S.list, chosen, 0.15f) : -1;
  if (la != HD_LIST_NONE)
    now_i = S.list.cur;
  if (tap)
    now_i = tap_i, S.list.cur = tap_i;
  if (now_i >= 0 && now_i < nf) {
    snprintf(S.sel, sizeof S.sel, "%s", flat[now_i]->id);
    S.sel_ipad = S.ipad;
    chosen = now_i;
  }
  LabPack *sel_row = chosen >= 0 ? flat[chosen] : NULL;
  hd_info_show(sel_row);
  /* ---- the tabs over the list ---- */
  if (!modal) {
    if (hd_device_tabs(tabs_y, &S.ipad)) {
      S.sel[0] = 0;
      hd_info_show(NULL);
      S.list.cur = 0; /* the other list from its top */
      S.scroll[S.ipad][S.seg] = (HdScroll){0};
    }
    static const char *const segs[3] = {"menugraphics/tab_preload", "menugraphics/tab_downloads",
                                        "menugraphics/tab_favorites"};
    if (hd_segments(20, seg_y, segs, &S.seg)) {
      S.sel[0] = 0;
      hd_info_show(NULL);
      S.list.cur = 0;
      S.scroll[S.ipad][S.seg] = (HdScroll){0};
    }
  } else {
    hd_pic(S.ipad ? "menugraphics-ipad/tab_ipad_chosen" : "menugraphics-ipad/tab_ipad", 0, tabs_y, WHITE);
    hd_pic(S.ipad ? "menugraphics-ipad/tab_iphone" : "menugraphics-ipad/tab_iphone_chosen", 160, tabs_y, WHITE);
    static const char *const segs[3] = {"menugraphics/tab_preload", "menugraphics/tab_downloads",
                                        "menugraphics/tab_favorites"};
    for (int i = 0; i < 3; i++) {
      char n[96];
      snprintf(n, sizeof n, "%s%s", segs[i], S.seg == i ? "_selected" : "");
      hd_pic(n, 107.0f * (float)i, seg_y, WHITE);
    }
  }
  hd_split_line();
  /* ---- the info ---- */
  int act = modal ? HD_ACT_NONE : hd_info_frame(HD_INFO_PLAY, 0);
  /* ---- the navigation bar ---- */
  int back = hd_navibar(S.multi ? "menugraphics/head_multi" : "menugraphics/head_single",
                        S.multi ? "Multi player" : "Single player", "menugraphics/back_main_menu", "Main menu");
  /* the ghost ball: on / off */
  int ghost = lab_reg_get_int("setting-ghostball", 1) == 1;
  if (!S.multi && !modal &&
      hd_pic_button(604, ghost ? "menugraphics/ghost_on" : "menugraphics/ghost_off", NULL, cw - 8 - 54, 8, 0)) {
    lab_reg_set_int("setting-ghostball", !ghost);
    lab_reg_save();
  }
  if (modal) {
    mp_frame();
    return;
  }
  /* ---- what was pressed ---- */
  /* A in the list, or a finger on a row's play button: its next level
   * (another level: right, to the pictures) */
  if ((la == HD_LIST_A || tap == 2) && sel_row) {
    play(sel_row, sel_row->current);
    return;
  }
  LabPack *row = sel_row ? lab_levels_find(sel_row->id) : NULL;
  if (act == HD_ACT_PLAY && row) {
    play(row, hd_info_level());
    return;
  }
  if (act == HD_ACT_FAVE && row) {
    row->fave = !row->fave;
    lab_levels_save();
    ui_toast(row->fave ? "Added to your faves" : "Removed from your faves");
  }
  /* X in the list: the lit row to (or from) the faves */
  if (S.list.in && ui_pressed(HidNpadButton_X) && S.list.cur < nf) {
    LabPack *k = lab_levels_find(flat[S.list.cur]->id);
    if (k) {
      k->fave = !k->fave;
      lab_levels_save();
      lab_audio_click();
      ui_toast(k->fave ? "Added to your faves" : "Removed from your faves");
    }
  }
  if (ui_pressed(HidNpadButton_Y) && row) {
    play(row, hd_info_level());
    return;
  }
  ui_default_focus(nf ? HD_ID_LIST : 20 + S.seg);
  int fid = ui_focus_id();
  int in_info = fid >= 600 && fid < 610;
  if (!back && in_info && nf && ui_back()) {
    ui_focus(HD_ID_LIST); /* B in the info: back in the list, at its row */
    lab_audio_click();
  } else if (back || ui_back()) {
    hd_info_show(NULL);
    ui_pop();
    return;
  }
  if (S.list.in) {
    ui_hint("A", "Play");
    if (!S.multi)
      ui_hint("X", "Faves");
    ui_hint("R", "Next list");
    ui_hint("B", "Leave list");
  } else if (fid == HD_ID_LIST) {
    ui_hint("A", "Browse");
    ui_hint("ZL", "iPad / iPhone");
    ui_hint("R", "Next list");
    ui_hint("B", "Back");
  } else if (in_info) {
    ui_hint("A", "Press");
    ui_hint("R", "Next list");
    ui_hint("B", "The list");
  } else {
    ui_hint("A", "Select");
    ui_hint("ZL", "iPad / iPhone");
    ui_hint("R", "Next list");
    ui_hint("B", "Back");
  }
}
