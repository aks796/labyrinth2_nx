/* lab_hd_online.c -- the iPad menus' Download levels and Create.
 *
 *   download   the split view: iPad levels / iPhone levels (each from its
 *              own server: Labyrinth 2 HD's, contentsystem-ipad, and the
 *              phone game's), the lists of the bottom bar -- New
 *              (Newcomers / What's hot / Random), All levels (Easy / Medium
 *              / Hard), Top 25 (Rated / Downloaded / Played), By ID -- 25 a
 *              page; the chosen pack's info on the right (its levels once
 *              its zip is here), Download. More info: the author's packs.
 *   create     your own packs (the phone server's, made in the web editor
 *              with this console's ID and PIN: lab_screens_online.c does the
 *              refresh and the publishing), Refresh / New at the bottom;
 *              New: how to make them (the editor's address, a QR code). MIT.
 */
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "lab.h"
#include "lab_hd.h"
#include "lab_online.h"
#include "lab_ui.h"
#include "util.h"

#define WHITE 0xffffffffu

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static int on_screen(void) {
  int s = ui_top();
  return s == SCR_DOWNLOAD || s == SCR_CREATE || s == SCR_HOWTO;
}

static void online_error(const LabOnResult *r) {
  if (!on_screen())
    return;
  if (r->status == LAB_ON_OFFLINE)
    ui_confirm("Not connected", r->msg, "OK", NULL, NULL);
  else if (r->status == LAB_ON_REFUSED)
    ui_confirm("Connection Failed!", r->msg, "OK", NULL, NULL);
  else
    ui_confirm("Connection Failed!", "Sorry! Couldn't reach the Labyrinth server. Please try later.", "OK", NULL,
               NULL);
}

/* the server's name, author, difficulty, rating over the pack's info.xml */
static void from_list(LabPack *k, const LabNetPack *np, int ipad) {
  snprintf(k->id, sizeof k->id, "%s", np->lid);
  if (np->name[0])
    snprintf(k->name, sizeof k->name, "%s", np->name);
  if (np->author[0])
    snprintf(k->author, sizeof k->author, "%s", np->author);
  if (np->difficulty >= 0)
    k->difficulty = np->difficulty;
  k->rating = np->rating;
  k->theme = np->theme;
  k->revision = np->rev;
  k->reqver = np->rqv;
  k->tutorial = 0;
  k->preloaded = k->ownlevel = 0;
  k->published = 1;
  k->myrating = k->mydifficulty = -1;
  k->ipad = ipad;
}

/* ============================================================ the lists */
typedef struct {
  const char *lt;
  int max;
  LabNetPack *it;
  int n, cap, pages, more, loading, failed;
  unsigned gen;
  HdScroll scroll;
} OnList;

enum { L_NEW, L_HOT, L_RND, L_EASY, L_MED, L_HARD, L_RATED, L_DLS, L_PLAYED, L_SEARCH, L_COUNT };
static const char *const k_lt[L_COUNT] = {"new", "hot", "rnd", "all_esy", "all_med", "all_hrd", "rtg", "dls", "mpl", "ath"};
static OnList g_lists[2][L_COUNT];

static void list_clear(OnList *l) {
  free(l->it);
  l->it = NULL;
  l->n = l->cap = l->pages = l->more = l->loading = l->failed = 0;
  l->scroll.to = l->scroll.at = 0;
  l->gen++;
}

typedef struct {
  int dev;
  OnList *l;
  unsigned gen;
} ListReq;

static char g_search_uid[2][24], g_search_text[2][40];

static void list_done(const LabOnResult *r, void *arg) {
  ListReq q = *(ListReq *)arg;
  free(arg);
  OnList *l = q.l;
  if (q.gen != l->gen)
    return;
  l->loading = 0;
  if (r->status != LAB_ON_OK) {
    l->failed = 1;
    online_error(r);
    return;
  }
  l->failed = 0;
  l->pages++;
  for (int i = 0; i < r->npacks; i++) {
    int dup = 0;
    for (int k = 0; k < l->n && !dup; k++)
      dup = !strcmp(l->it[k].lid, r->packs[i].lid);
    if (dup || (l->max && l->n >= l->max))
      continue;
    if (l->n == l->cap) {
      int cap = l->cap ? l->cap * 2 : 32;
      LabNetPack *g = realloc(l->it, sizeof *g * (size_t)cap);
      if (!g)
        break;
      l->it = g;
      l->cap = cap;
    }
    l->it[l->n++] = r->packs[i];
  }
  l->more = r->more && (!l->max || l->n < l->max);
}

static void list_load(int dev, OnList *l) {
  if (l->loading || !lab_online_enabled())
    return;
  ListReq *q = malloc(sizeof *q);
  if (!q)
    return;
  q->dev = dev, q->l = l, q->gen = l->gen;
  l->loading = 1;
  l->failed = 0;
  lab_online_set_ipad(dev);
  lab_online_list(l->lt, l == &g_lists[dev][L_SEARCH] ? g_search_uid[dev] : NULL, l->pages + 1, list_done, q);
  lab_online_set_ipad(0);
}

/* ------------------------------------------------ a pack being fetched */
typedef struct {
  LabNetPack np;
  int dev, keep;
} Pending;
#define MAXP 24
static Pending *g_pend[MAXP];
static int g_npend;

/* the pack looked at: its zip here, not (yet) in the table */
static struct {
  int has, dev;
  LabPack p;
  int times[256], nt;
} P;

static struct {
  int ipad, tab, sub[3];
  char sel[24];
  int sel_dev;
  LabNetPack sel_np;
  int want_focus, want_in; /* a new list: the focus on it once it is here (in it, if it was) */
  HdList list;
} D = {.ipad = 1, .tab = 1};

static Pending *pending_find(const char *lid, int dev) {
  for (int i = 0; i < g_npend; i++)
    if (!strcmp(g_pend[i]->np.lid, lid) && g_pend[i]->dev == dev)
      return g_pend[i];
  return NULL;
}

static void pending_drop(Pending *p) {
  for (int i = 0; i < g_npend; i++)
    if (g_pend[i] == p) {
      g_pend[i] = g_pend[--g_npend];
      break;
    }
  free(p);
}

static void keep_preview(void) {
  LabPack *k = lab_levels_add(&P.p, P.times, P.nt);
  if (!k) {
    ui_confirm("Couldn't download", "There is no room for more level packs (1024). Delete some first.", "OK", NULL,
               NULL);
    return;
  }
  lab_levels_save();
  lab_audio_play(0, SND_MENU_DOWNLOAD_COMPLETE, 1.0f, 1.0f);
  ui_toast("Downloaded: A plays it (Single player > Downloaded too)");
  debugPrintf("[online] downloaded %s \"%s\"%s\n", k->id, k->name, k->ipad ? " (iPad)" : "");
  P.has = 0;
  /* the pane stays on it, its Download now its Play (hardware 2026-09-27:
   * the pane went blank and the pack had to be chosen again) */
  hd_info_show(k);
}

static void get_done(const LabOnResult *r, void *arg) {
  Pending *p = arg;
  if (r->status != LAB_ON_OK) {
    debugPrintf("[online] %s: not fetched (%s)\n", p->np.lid, r->msg);
    /* a pack only looked at (the ring resting on its row): no dialog in the
     * way of the list, a toast */
    if (p->keep)
      online_error(r);
    else if (on_screen())
      ui_toast("Couldn't get its levels from the server");
    pending_drop(p);
    return;
  }
  LabPack k;
  int times[256], nt = 0;
  int rc = lab_files_store_pack(p->np.lid, r->data, r->len, &k, times, 256, &nt);
  if (rc != 0) {
    if (on_screen())
      ui_confirm("Couldn't download", rc < 0 ? "The level pack could not be saved to the SD card (see debug.log)."
                                             : "What came back is not a level pack that can be read.",
                 "OK", NULL, NULL);
    pending_drop(p);
    return;
  }
  int ipad = k.ipad;
  from_list(&k, &p->np, ipad);
  if (ipad != p->dev)
    debugPrintf("[online] %s: from the %s server, but an %s board\n", k.id, p->dev ? "iPad" : "iPhone",
                ipad ? "iPad" : "iPhone");
  /* the one looked at: its info (and its levels) */
  if (!strcmp(D.sel, k.id) && D.sel_dev == p->dev) {
    P.has = 1, P.dev = p->dev;
    P.p = k;
    P.nt = nt;
    memcpy(P.times, times, sizeof(int) * (size_t)nt);
    for (int i = 0; i < nt; i++) {
      /* the designer's times shown before it is in the table */
      char key[128];
      snprintf(key, sizeof key, "DTIME_%s_%d", k.id, i);
      lab_reg_set_int(key, times[i]);
    }
    hd_info_show(&P.p); /* the same pack: its levels filled in, not shown anew */
  }
  if (p->keep && !lab_levels_find(k.id)) {
    if (P.has && !strcmp(P.p.id, k.id))
      keep_preview();
    else {
      LabPack *row = lab_levels_add(&k, times, nt);
      if (row) {
        lab_levels_save();
        lab_audio_play(0, SND_MENU_DOWNLOAD_COMPLETE, 1.0f, 1.0f);
        debugPrintf("[online] downloaded %s \"%s\"\n", k.id, k.name);
        if (!strcmp(D.sel, k.id) && D.sel_dev == p->dev) {
          ui_toast("Downloaded: A plays it (Single player > Downloaded too)");
          hd_info_show(row);
        }
      }
    }
  }
  pending_drop(p);
}

static void fetch(const LabNetPack *np, int dev, int keep) {
  Pending *p = pending_find(np->lid, dev);
  if (p) {
    p->keep |= keep;
    return;
  }
  if (g_npend >= MAXP || !lab_online_enabled())
    return;
  p = calloc(1, sizeof *p);
  if (!p)
    return;
  p->np = *np;
  p->dev = dev;
  p->keep = keep;
  g_pend[g_npend++] = p;
  lab_online_set_ipad(dev);
  lab_online_get(np->lid, get_done, p);
  lab_online_set_ipad(0);
}

/* ============================================================ download */
enum { TAB_NEW, TAB_ALL, TAB_TOP, TAB_ID };

static OnList *cur_list(void) {
  OnList *L = g_lists[D.ipad];
  switch (D.tab) {
  case TAB_NEW: return &L[L_NEW + D.sub[0]];
  case TAB_ALL: return &L[L_EASY + D.sub[1]];
  case TAB_TOP: return &L[L_RATED + D.sub[2]];
  default: return &L[L_SEARCH];
  }
}

static void lists_init(void) {
  static int done;
  if (done)
    return;
  done = 1;
  for (int d = 0; d < 2; d++)
    for (int i = 0; i < L_COUNT; i++) {
      g_lists[d][i].lt = k_lt[i];
      g_lists[d][i].max = i >= L_RATED && i <= L_PLAYED ? 100 : 0;
    }
}

static void create_list_reset(void);

void hd_online_reset(void) {
  lists_init();
  for (int d = 0; d < 2; d++)
    for (int i = 0; i < L_COUNT; i++)
      if (i != L_SEARCH)
        list_clear(&g_lists[d][i]);
  P.has = 0;
  D.sel[0] = 0;
  hd_info_show(NULL);
  scr_online_reset(); /* Create refreshes on its next visit */
  /* from the main menu: the lists as a whole focused, not in them yet */
  hd_list_reset(&D.list);
  create_list_reset();
}

void hd_download_enter(void) {
  lists_init();
  lab_ui_side_info(NULL, NULL, NULL);
  D.want_focus = -1;
}

static void run_search(const char *text) {
  char q[40];
  size_t n = 0;
  for (const char *s = text; *s && n < sizeof q - 1; s++)
    if (!isspace((unsigned char)*s))
      q[n++] = (char)toupper((unsigned char)*s);
  q[n] = 0;
  if (!q[0])
    return;
  snprintf(g_search_text[D.ipad], sizeof g_search_text[0], "%s", q);
  char *dot = strchr(q, '.');
  size_t ul = dot ? (size_t)(dot - q) : strlen(q);
  int ok = ul >= 4 && ul <= 16;
  for (size_t i = 0; i < ul && ok; i++)
    ok = isalnum((unsigned char)q[i]);
  if (!ok) {
    ui_confirm("Invalid ID",
               "Couldn't find ID. You can only search for user and level IDs on the form A70K3WA9 or A70K3WA9.01",
               "OK", NULL, NULL);
    return;
  }
  snprintf(g_search_uid[D.ipad], sizeof g_search_uid[0], "%.*s", (int)ul, q);
  OnList *l = &g_lists[D.ipad][L_SEARCH];
  list_clear(l);
  list_load(D.ipad, l);
}

static void kbd_done(const char *text, void *arg) {
  (void)arg;
  if (text)
    run_search(text);
}

static void sd_import(void) {
  int added = 0, failed = 0;
  lab_files_import_sd(&added, &failed);
  char s[128];
  snprintf(s, sizeof s, "%d new level pack%s from the SD card%s", added, added == 1 ? "" : "s",
           failed ? " (some files were not level packs)" : "");
  ui_toast(s);
}

/* a server pack's row: stars, download (or play, when it is here) */
static int net_row(int id, float y, const LabNetPack *np, int selected, int dev) {
  LabPack look;
  memset(&look, 0, sizeof look);
  snprintf(look.id, sizeof look.id, "%s", np->lid);
  snprintf(look.name, sizeof look.name, "%s", np->name);
  snprintf(look.author, sizeof look.author, "%s", np->author);
  look.difficulty = np->difficulty;
  look.theme = np->theme;
  const LabPack *have = lab_levels_find(np->lid);
  int busy = pending_find(np->lid, dev) != NULL;
  uint32_t bg;
  const char *icon, *play;
  hd_diff_style(np->difficulty, 0, &bg, &icon, &play);
  char dl[64];
  snprintf(dl, sizeof dl, "menugraphics/icon_download_%s",
           np->difficulty == 1 ? "orange" : np->difficulty == 2 ? "black" : "green");
  int r = hd_pack_row(id, y, &look, selected, have ? play : dl);
  if (busy) {
    /* over its button (centred where hd_pack_row puts it) */
    lab_draw_rrect(HD_LW - 44, y + 17, 36, 33, 8, bg);
    float t = ui_time() * 6.0f;
    for (int i = 0; i < 8; i++) {
      float a = t + (float)i * 0.785f;
      lab_draw_rrect(HD_LW - 26 + cosf(a) * 9 - 2, y + 33.2f + sinf(a) * 9 - 2, 4, 4, 2,
                     0x20405000u | (uint32_t)(40 + 26 * i));
    }
  }
  /* the stars */
  const LabTex *se = hd_tex("menugraphics/emptyStars"), *sf = hd_tex("menugraphics/stars");
  if (se && sf) {
    float sw = lab_tex_pt_w(se), sh = lab_tex_pt_h(se), f = clampf((float)np->rating * 0.2f, 0, 1);
    lab_draw_image(se, 76, y + 49, sw, sh, WHITE);
    if (f > 0.01f)
      lab_draw_image_uv(sf, 76, y + 49, sw * f, sh, 0, 0, f, 1, WHITE);
  }
  return r;
}

static void choose(const LabNetPack *np) {
  snprintf(D.sel, sizeof D.sel, "%s", np->lid);
  D.sel_dev = D.ipad;
  D.sel_np = *np;
  LabPack *have = lab_levels_find(np->lid);
  if (have) {
    hd_info_show(have);
    return;
  }
  P.has = 0;
  /* what the list says, until its zip is here */
  LabPack k;
  memset(&k, 0, sizeof k);
  from_list(&k, np, D.ipad);
  k.nlevels = np->nlevels;
  hd_info_show(NULL);
  hd_info_show(&k);
  fetch(np, D.ipad, 0);
}

void hd_download_frame(void) {
  float cw = hd_cw();
  lists_init();
  hd_split_bg();
  float y = 44;
  float tabs_y = y;
  y += 49;
  float seg_y = y;
  if (D.tab != TAB_ID)
    y += 48;
  const float top = y + (D.tab == TAB_ID ? 34 : 0), bottom = HD_CANVAS_H - 64;
  OnList *l = cur_list();
  if (D.tab != TAB_ID && !l->pages && !l->loading && !l->failed)
    list_load(D.ipad, l);
  /* ---- the list: its packs, then Get 25 more (a row of the list too) ---- */
  static float fy[1024 + 1], fh[1024 + 1];
  int nf = 0;
  for (int i = 0; i < l->n && nf < 1024; i++, nf++)
    fy[nf] = 70.0f * (float)i, fh[nf] = 70;
  int more_i = -1;
  if (l->more)
    more_i = nf, fy[nf] = 70.0f * (float)l->n, fh[nf] = 60, nf++;
  float content = 70.0f * (float)l->n + (l->more ? 60 : 0);
  float view = bottom - top;
  int chosen = -1;
  for (int i = 0; i < l->n && chosen < 0; i++)
    if (D.sel_dev == D.ipad && !strcmp(D.sel, l->it[i].lid))
      chosen = i;
  int la = hd_list(&D.list, &l->scroll, top, view, content, fy, fh, nf, chosen, 601);
  float ry = top - hd_scroll(&l->scroll, 0, top, HD_LW, view, content);
  lab_draw_scissor(0, top, HD_LW, view);
  ui_clip(0, top, HD_LW, view);
  int tap = 0, tap_i = -1;
  for (int i = 0; i < l->n; i++, ry += 70) {
    if (ry + 70 < top || ry > bottom)
      continue;
    int r = net_row(1000 + i, ry, &l->it[i], hd_list_lit(&D.list, i, chosen), D.ipad);
    if (r)
      tap = r, tap_i = i;
  }
  if (l->more && ry < bottom + 70) {
    int lit = D.list.in && D.list.cur == more_i;
    if (ui_button(90, 0, ry, HD_LW, 60, UI_NOFOCUS) && !l->loading)
      list_load(D.ipad, l);
    lab_draw_rect(0, ry, HD_LW, 60, lit ? 0x3f8fa4ffu : 0x2c5f6cffu);
    lab_draw_text(HD_LW * 0.5f, ry + 37, 17, 0xf4ecd8ffu, LAB_CENTER, 1, l->loading ? "Loading..." : "Get 25 more");
  }
  hd_scroll_bar(&l->scroll, HD_LW, top, view, content);
  lab_draw_scissor(0, 0, 0, 0);
  ui_clip(0, 0, 0, 0);
  if (!l->n) {
    const char *msg = l->loading ? "Loading..."
                    : l->failed  ? "Couldn't reach the Labyrinth server."
                    : D.tab == TAB_ID
                        ? (g_search_uid[D.ipad][0] ? "No level packs by that ID."
                                                   : "Search a player's level packs by their ID (A70K3WA9), or a "
                                                     "pack's (A70K3WA9.01).\n\nLevel packs on the SD card "
                                                     "(levelpacks/) are added with Y.")
                        : "No level packs.";
    lab_draw_text_box(24, top + 100, HD_LW - 48, 16, 0x1e3a3cffu, LAB_CENTER, 1, msg);
  }
  /* the row the controller rests on: its levels shown (a little longer than
   * in one's own lists: that fetches the pack's preview); at once when A or
   * right take it to the pane; a finger's too */
  int now_i = hd_list_dwell(&D.list, chosen, 0.45f);
  if (la != HD_LIST_NONE)
    now_i = D.list.cur;
  if (tap == 1)
    now_i = tap_i, D.list.cur = tap_i;
  if (now_i >= 0 && now_i < l->n) {
    LabNetPack np = l->it[now_i];
    if (now_i != chosen)
      choose(&np);
    chosen = now_i;
  }
  /* A in the list: that pack's Download (or Play) next; on Get 25 more, 25 more */
  if (la == HD_LIST_A) {
    if (D.list.cur == more_i) {
      if (!l->loading)
        list_load(D.ipad, l);
    } else if (chosen >= 0) {
      D.list.in = 0;
      ui_focus(601);
    }
  }
  /* ---- over the list: the tabs, the segments (or the search field) ---- */
  int switched = hd_device_tabs(tabs_y, &D.ipad);
  if (switched) {
    D.sel[0] = 0;
    P.has = 0;
    hd_info_show(NULL);
  }
  if (D.tab == TAB_ID) {
    hd_pic_wh("menugraphics/section_levelbyid", 0, seg_y, HD_LW, 15, WHITE);
    float fy2 = seg_y + 15;
    int hit = ui_button(40, 0, fy2, HD_LW, 34, 0);
    hd_pic_wh("menugraphics/bg_ID", 0, fy2, HD_LW, 34, WHITE);
    hd_pic("menugraphics/icon_ID", 6, fy2 + 1, WHITE);
    lab_draw_text(44, fy2 + 23, 15, g_search_text[D.ipad][0] ? 0x101010ffu : 0x808080ffu, LAB_LEFT, 1,
                  g_search_text[D.ipad][0] ? g_search_text[D.ipad] : "A player's or a pack's ID");
    if (hit || ui_pressed(HidNpadButton_Minus))
      lab_kbd_request("Search by ID", "A70K3WA9 or A70K3WA9.01", g_search_text[D.ipad], 16, kbd_done, NULL);
  } else {
    static const char *const segs[3][3] = {
        {"menugraphics/Tab_newcomers", "menugraphics/Tab_whatshot", "menugraphics/Tab_random"},
        {"menugraphics/Tab_easy", "menugraphics/Tab_medium", "menugraphics/Tab_hard"},
        {"menugraphics/Tab_rated", "menugraphics/Tab_downloaded", "menugraphics/Tab_played"}};
    if (hd_segments(20, seg_y, segs[D.tab], &D.sub[D.tab]))
      switched = 1;
  }
  /* ---- the bottom bar ---- */
  hd_pic_wh("menugraphics/bottom_bar", 0, bottom, HD_LW, 64, WHITE);
  static const char *const bars[4] = {"menugraphics/bar_new", "menugraphics/bar_alllevels", "menugraphics/bar_top25",
                                      "menugraphics/bar_search"};
  int nt = -1;
  for (int i = 0; i < 4; i++) {
    float bx = 80.0f * (float)i, by = bottom + 5;
    if (ui_button(30 + i, bx, by, 80, 55, UI_NORING))
      nt = i;
    char n[64];
    snprintf(n, sizeof n, "%s%s", bars[i], D.tab == i ? "_selected" : "");
    hd_pic(n, bx, by, WHITE);
    if (ui_focused(30 + i))
      lab_draw_glow(bx + 6, by + 4, 68, 47);
  }
  if (ui_pressed(HidNpadButton_Plus))
    nt = (D.tab + 1) % 4;
  if (nt >= 0 && nt != D.tab) {
    D.tab = nt;
    switched = 1;
  }
  if (switched) {
    D.want_focus = 1000;
    D.want_in = D.list.in;
    D.list.cur = 0; /* the other list from its top */
  }
  hd_split_line();
  /* ---- the info ---- */
  const LabPack *shown = hd_info_pack();
  LabPack *have = shown ? lab_levels_find(shown->id) : NULL;
  int iact = hd_info_frame(have ? HD_INFO_PLAY : HD_INFO_DOWNLOAD, !have);
  /* what it is waiting for: its levels (the preview), or its download (on
   * the button, which turns into Play when it is here) */
  Pending *pp = shown && !have ? pending_find(D.sel, D.sel_dev) : NULL;
  if (pp) {
    float mx = HD_LW + (cw - HD_LW) * 0.45f, by = HD_CANVAS_H - 51 - 46;
    if (pp->keep) {
      lab_draw_rrect(mx - 183, by, 366, 46, 10, 0x000000a8u);
      lab_draw_text(mx, by + 30, 19, WHITE, LAB_CENTER, 1, "Downloading...");
    } else if (!P.has) {
      lab_draw_text(mx, by - 42, 15, 0x40606affu, LAB_CENTER, 1, "Getting its levels...");
    }
  }
  /* ---- the bar ---- */
  static const char *const heads[4] = {"menugraphics/head_new", "menugraphics/head_alllevels", "menugraphics/head_25",
                                       "menugraphics/head_search"};
  static const char *const head_t[4] = {"New", "All levels", "Top 25", "Search by ID"};
  int back = hd_navibar(heads[D.tab], head_t[D.tab], "menugraphics/back_main_menu", "Main menu");
  /* ---- what was pressed ---- */
  if (tap == 2 && tap_i >= 0 && tap_i < l->n) { /* a finger on a row's button */
    LabNetPack np = l->it[tap_i];
    LabPack *k = lab_levels_find(np.lid);
    if (k)
      hd_open_game(k, k->current);
    else
      fetch(&np, D.ipad, 1);
    return;
  }
  if (iact == HD_ACT_PLAY && have) {
    hd_open_game(have, hd_info_level());
    return;
  }
  if (iact == HD_ACT_FAVE && have) {
    have->fave = !have->fave;
    lab_levels_save();
    ui_toast(have->fave ? "Added to your faves" : "Removed from your faves");
  }
  if (iact == HD_ACT_DOWNLOAD && shown && !have) {
    if (P.has && !strcmp(P.p.id, shown->id))
      keep_preview();
    else
      fetch(&D.sel_np, D.sel_dev, 1);
  }
  if (iact == HD_ACT_MOREINFO && shown) {
    /* the author's packs: By ID with the author's ID */
    char uid[64];
    snprintf(uid, sizeof uid, "%s", shown->id);
    char *dot = strchr(uid, '.');
    if (dot)
      *dot = 0;
    D.tab = TAB_ID;
    D.ipad = D.sel_dev;
    run_search(uid);
    D.want_focus = 1000;
    D.want_in = 0;
  }
  if (D.tab == TAB_ID && ui_pressed(HidNpadButton_Y))
    sd_import();
  if (D.want_focus >= 0 && (nf || !l->loading)) {
    /* a new list, here (it may take a moment from the server): the focus on
     * it, in it again if it was; none: the search field or the tab */
    if (nf) {
      ui_focus(HD_ID_LIST);
      D.list.in = D.want_in;
    } else {
      ui_focus(D.tab == TAB_ID ? 40 : 30 + D.tab);
    }
    D.want_focus = -1;
  }
  ui_default_focus(nf ? HD_ID_LIST : 30 + D.tab);
  int fid = ui_focus_id(), in_info = fid >= 600 && fid < 610;
  if (!back && in_info && nf && ui_back()) {
    ui_focus(HD_ID_LIST); /* B in the info: back in the list, at its row */
    lab_audio_click();
  } else if (back || ui_back()) {
    hd_info_show(NULL);
    ui_pop();
    return;
  }
  if (D.list.in) {
    ui_hint("A", D.list.cur == more_i ? "25 more" : "Choose");
    ui_hint("ZL", "iPad / iPhone");
    if (D.tab != TAB_ID)
      ui_hint("R", "Next list");
    ui_hint("+", "Next tab");
    ui_hint("B", "Leave list");
  } else {
    ui_hint("A", in_info ? "Press" : fid == HD_ID_LIST ? "Browse" : "Select");
    ui_hint("ZL", "iPad / iPhone");
    if (D.tab != TAB_ID)
      ui_hint("R", "Next list");
    else
      ui_hint("-", "Search");
    ui_hint("+", "Next tab");
    ui_hint("B", in_info ? "The list" : "Back");
  }
}

/* ============================================================ create */
static struct {
  char sel[64];
  HdScroll scroll;
  HdList list;
} C;

static void create_list_reset(void) { hd_list_reset(&C.list); }

void hd_create_enter(void) {
  scr_create_enter(); /* the refresh, when it is due */
}

void hd_create_frame(void) {
  hd_split_bg();
  const float top = 44 + 26, bottom = HD_CANVAS_H - 64;
  /* ---- the list: not published, then published ---- */
  static LabPack *rows[2][512];
  int n[2];
  n[0] = lab_levels_query_own(0, rows[0], 512);
  n[1] = lab_levels_query_own(1, rows[1], 512);
  /* the rows in order, where each is in the list (the groups' titles
   * between them) */
  static LabPack *flat[2 * 512];
  static float fy[2 * 512], fh[2 * 512];
  static int fg[2 * 512];
  int nf = 0;
  float content = 0;
  for (int g = 0; g < 2; g++) {
    if (!n[g])
      continue;
    content += 26;
    for (int i = 0; i < n[g]; i++, nf++, content += 70)
      flat[nf] = rows[g][i], fy[nf] = content, fh[nf] = 70, fg[nf] = g;
  }
  int chosen = -1;
  for (int i = 0; i < nf && chosen < 0; i++)
    if (!strcmp(C.sel, flat[i]->id))
      chosen = i;
  if (chosen < 0 && nf) {
    chosen = 0;
    snprintf(C.sel, sizeof C.sel, "%s", flat[0]->id);
  }
  float view = bottom - top;
  int la = hd_list(&C.list, &C.scroll, top, view, content, fy, fh, nf, chosen, 602);
  float at = hd_scroll(&C.scroll, 0, top, HD_LW, view, content);
  lab_draw_scissor(0, top, HD_LW, view);
  ui_clip(0, top, HD_LW, view);
  static const char *const gname[2] = {"Not published", "Published"};
  int tap = 0, tap_i = -1;
  for (int i = 0; i < nf; i++) {
    float ry = top - at + fy[i];
    if ((i == 0 || fg[i] != fg[i - 1]) && ry > top - 20 && ry - 26 < bottom) {
      lab_draw_rect(0, ry - 26, HD_LW, 26, 0x2b2a24ffu);
      lab_draw_text(14, ry - 8, 14, 0xf4ecd8ffu, LAB_LEFT, 1, gname[fg[i]]);
    }
    if (ry + 70 < top || ry > bottom)
      continue;
    int r = hd_pack_row(1000 + i, ry, flat[i], hd_list_lit(&C.list, i, chosen), NULL);
    if (r)
      tap = r, tap_i = i;
  }
  hd_scroll_bar(&C.scroll, HD_LW, top, view, content);
  lab_draw_scissor(0, 0, 0, 0);
  ui_clip(0, 0, 0, 0);
  if (!content)
    lab_draw_text_box(24, top + 140, HD_LW - 48, 16, 0x1e3a3cffu, LAB_CENTER, 1,
                      scr_create_refreshing() ? "Updating..."
                                              : "No level packs created yet.\n\nPress New (bottom right) to see how "
                                                "to create your own levels.");
  /* the row the controller rests on is chosen; at once when it is played or
   * taken to the pane; a finger's too */
  int now_i = hd_list_dwell(&C.list, chosen, 0.15f);
  if (la != HD_LIST_NONE)
    now_i = C.list.cur;
  if (tap)
    now_i = tap_i, C.list.cur = tap_i;
  if (now_i >= 0 && now_i < nf) {
    snprintf(C.sel, sizeof C.sel, "%s", flat[now_i]->id);
    chosen = now_i;
  }
  LabPack *sel = chosen >= 0 ? flat[chosen] : NULL;
  hd_info_show(sel);
  /* ---- the ID and PIN, over the list ---- */
  lab_draw_rect(0, 44, HD_LW, 26, 0xc8d8dcffu);
  const char *uid = lab_online_user_id(), *pin = lab_online_pin();
  char s[96];
  if (uid[0])
    snprintf(s, sizeof s, "Your ID: %s     Pin code: %s", uid, pin);
  else
    snprintf(s, sizeof s, "Your ID and pin code: not given yet");
  lab_draw_text(HD_LW * 0.5f, 44 + 18, 13, 0x3d7a7affu, LAB_CENTER, 1, s);
  /* ---- the bottom bar: Refresh, when it was, New ---- */
  hd_pic_wh("menugraphics/bottom_bar", 0, bottom, HD_LW, 64, WHITE);
  int refresh = hd_pic_button(30, "menugraphics/icon_refresh", "menugraphics/icon_refresh_down", 14, bottom + 14, 0);
  int newp = hd_pic_button(31, "menugraphics/icon_new", "menugraphics/icon_new_down", HD_LW - 14 - 37, bottom + 14, 0);
  lab_draw_text(HD_LW * 0.5f, bottom + 38, 13, 0xf4ecd8ffu, LAB_CENTER, 1, scr_create_updated());
  hd_split_line();
  /* ---- the info ---- */
  int iact = hd_info_frame(HD_INFO_OWN, 0);
  int back = hd_navibar("menugraphics/head_create", "Create", "menugraphics/back_main_menu", "Main menu");
  /* A in the list, or a finger on a row's play button: its next level */
  if ((la == HD_LIST_A || tap == 2) && sel) {
    hd_open_game(sel, sel->current);
    return;
  }
  LabPack *row = sel ? lab_levels_find(sel->id) : NULL;
  if (iact == HD_ACT_PLAY && row) {
    hd_open_game(row, hd_info_level());
    return;
  }
  if (iact == HD_ACT_PUBLISH && row)
    scr_online_publish(row);
  if (iact == HD_ACT_DELETE && row) {
    hd_info_show(NULL);
    lab_files_delete_pack(row->id);
    C.sel[0] = 0;
  }
  if (refresh || ui_pressed(HidNpadButton_Y))
    scr_create_refresh();
  if (newp || ui_pressed(HidNpadButton_Plus)) {
    ui_push(SCR_HOWTO);
    return;
  }
  ui_default_focus(nf ? HD_ID_LIST : 31);
  int fid = ui_focus_id(), in_info = fid >= 600 && fid < 610;
  if (!back && in_info && nf && ui_back()) {
    ui_focus(HD_ID_LIST); /* B in the info: back in the list, at its row */
    lab_audio_click();
  } else if (back || ui_back()) {
    hd_info_show(NULL);
    ui_pop();
    return;
  }
  ui_hint("A", C.list.in ? "Play" : in_info ? "Press" : fid == HD_ID_LIST ? "Browse" : "Select");
  ui_hint("Y", "Refresh");
  ui_hint("+", "New");
  ui_hint("B", C.list.in ? "Leave list" : in_info ? "The list" : "Back");
}

/* ============================================================ how-to */
static int g_qr;

void hd_howto_enter(void) {
  g_qr = 0;
  scr_enter_android(SCR_HOWTO); /* registers for the ID and PIN */
}

static void open_here_answer(int yes) {
  if (yes && lab_web_request(scr_editor_url()) != 0)
    ui_toast("The browser could not be opened");
}

void hd_howto_frame(void) {
  float cw = hd_cw();
  hd_split_bg();
  const LabTex *bg = hd_tex("menugraphics-ipad/bg-light-blue");
  if (bg)
    lab_draw_image(bg, 0, 44, cw, HD_CANVAS_H - 44, WHITE);
  static const char *const img[3] = {"menugraphics/01_icon_computer", "menugraphics/02_icon_id",
                                     "menugraphics/03_icon_create"};
  const char *uid = lab_online_user_id(), *pin = lab_online_pin();
  float colw = fminf(360, (cw - 120) / 3), x0 = (cw - colw * 3) * 0.5f, y0 = 80;
  for (int i = 0; i < 3; i++) {
    float cx = x0 + colw * ((float)i + 0.5f);
    hd_pic(img[i], cx - 67, y0, WHITE);
    char step[8];
    snprintf(step, sizeof step, "%d", i + 1);
    lab_draw_rrect(cx - 80, y0 - 6, 30, 30, 15, 0x3d7a7affu);
    lab_draw_text(cx - 65, y0 + 15, 17, WHITE, LAB_CENTER, 1, step);
    float ty = y0 + 134 + 26;
    if (i == 0) {
      ty += lab_draw_text_box(cx - colw * 0.45f, ty, colw * 0.9f, 19, 0x3d7a7affu, LAB_CENTER, 0, "Go to");
      ty += lab_draw_text_box(cx - colw * 0.45f, ty, colw * 0.9f, 20, 0x1e3a3cffu, LAB_CENTER, 1,
                              "labyrinth2.com/editor.html");
      lab_draw_text_box(cx - colw * 0.45f, ty, colw * 0.9f, 19, 0x3d7a7affu, LAB_CENTER, 0, "on your computer.");
    } else if (i == 1) {
      if (uid[0]) {
        ty += lab_draw_text_box(cx - colw * 0.45f, ty, colw * 0.9f, 19, 0x3d7a7affu, LAB_CENTER, 0, "Enter your ID:");
        ty += lab_draw_text_box(cx - colw * 0.45f, ty, colw * 0.9f, 26, 0x1e3a3cffu, LAB_CENTER, 1, uid) + 4;
        ty += lab_draw_text_box(cx - colw * 0.45f, ty, colw * 0.9f, 19, 0x3d7a7affu, LAB_CENTER, 0, "and your pin code:");
        lab_draw_text_box(cx - colw * 0.45f, ty, colw * 0.9f, 26, 0x1e3a3cffu, LAB_CENTER, 1, pin);
      } else {
        lab_draw_text_box(cx - colw * 0.45f, ty, colw * 0.9f, 16, 0x3d7a7affu, LAB_CENTER, 1,
                          lab_online_busy() ? "Getting your ID and pin code from the Labyrinth server..."
                                            : "Your ID and pin code come from the Labyrinth server: connect the "
                                              "console to the internet and open this page again.");
      }
    } else {
      ty += lab_draw_text_box(cx - colw * 0.45f, ty, colw * 0.9f, 19, 0x3d7a7affu, LAB_CENTER, 0,
                              "Create or edit your levels.");
      lab_draw_text_box(cx - colw * 0.45f, ty + 8, colw * 0.9f, 15, 0x1e3a3cffu, LAB_CENTER, 0,
                        "Then Refresh on the Create screen: your level packs come here to be played. When you have "
                        "played every level of one, publish it from its info.");
    }
  }
  /* the buttons: the QR code, the browser -- above the hints (hardware
   * 2026-09-27: at the iPad's 768-point place they were cut off) */
  float by = HD_CANVAS_H - 118, bw = 260;
  if (ui_button(51, cw * 0.5f - bw - 12, by, bw, 48, 0))
    g_qr = 2;
  ui_button_box(cw * 0.5f - bw - 12, by, bw, 48, "QR code", ui_focused(51), ui_down(51));
  if (ui_button(52, cw * 0.5f + 12, by, bw, 48, 0))
    ui_confirm("Open it here?",
               "The level editor was made for a computer's browser and mouse: the Switch's browser may not manage "
               "it. Log in with your ID and pin code.",
               "Open", "Cancel", open_here_answer);
  ui_button_box(cw * 0.5f + 12, by, bw, 48, "Open on Switch", ui_focused(52), ui_down(52));
  int back = hd_navibar("menugraphics/head_createlevel", "Create level", "menugraphics/back_create", "Create");
  if (g_qr) {
    lab_draw_rect(0, 0, cw, HD_CANVAS_H, 0x000000b0u);
    float w = 420, h = 470, x = (cw - w) * 0.5f, y = (HD_CANVAS_H - h) * 0.5f;
    lab_draw_rrect(x, y, w, h, 12, WHITE);
    lab_draw_text(cw * 0.5f, y + 36, 19, 0x1e3a3cffu, LAB_CENTER, 1, "Scan it with a phone or tablet");
    LabTex *t = scr_online_qr();
    float s = 300;
    if (t)
      lab_draw_image(t, cw * 0.5f - s * 0.5f, y + 56, s, s, WHITE);
    lab_draw_text(cw * 0.5f, y + 56 + s + 30, 18, 0x101010ffu, LAB_CENTER, 1, "labyrinth2.com/editor.html");
    lab_draw_text_box(x + 20, y + 56 + s + 44, w - 40, 14, 0x404040ffu, LAB_CENTER, 0,
                      "Log in there with the ID and pin code. A computer's screen suits the editor best.");
    if (g_qr == 2)
      g_qr = 1;
    else if (ui_back() || ui_pressed(HidNpadButton_A) || ui_pad->touch_ended) {
      lab_audio_click();
      g_qr = 0;
    }
    ui_hint("B", "Close");
    return;
  }
  ui_default_focus(51);
  if (back || ui_back()) {
    ui_pop();
    return;
  }
  ui_hint("A", "Select");
  ui_hint("B", "Back");
}
