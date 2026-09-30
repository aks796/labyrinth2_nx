/* lab_screens_online.c -- the game's online screens, rebuilt, on the
 * Labyrinth 2 server that is still up (lab_online.c):
 *
 *   download   DownloadActivity(Full): the players' level packs. Tabs along
 *              the bottom -- New (newcomers / hot / random), All levels
 *              (easy / medium / hard), Top 25 (rated / downloaded / played,
 *              at most 100) and By ID (a player's or a pack's ID) -- 25 a
 *              page, "Get 25 more". A row's right side downloads the pack
 *              (then plays it); the row opens its info, which for a pack
 *              not downloaded yet fetches it first to show its levels
 *              (LevelPackInfoActivityBase's t). The By ID tab also takes
 *              level packs from the SD card, as the port did offline.
 *   create     CreateActivity: your own packs, made in the web editor with
 *              this console's ID and PIN: "Not published" / "Published",
 *              brought from the server on Refresh (dn.java: the list of
 *              yours, then each pack), played here, published from their
 *              info once every level is played (your times are the
 *              designer times).
 *   how-to     CreateInstructionsActivity: the editor's address, your ID
 *              and PIN, and the pictures. The editor is a web page for a
 *              computer; the page also has a QR code of its address (for a
 *              phone or a tablet) and can open it in the Switch's browser.
 *
 * The requests run on lab_online.c's thread; their answers arrive here on
 * the main thread (lab_online_poll), where the level table and the files
 * are changed. MIT.
 */
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "lab.h"
#include "lab_online.h"
#include "lab_qr.h"
#include "lab_ui.h"
#include "util.h"

#define EDITOR_URL "http://www.labyrinth2.com/editor.html"
#define EDITOR_SHOWN "labyrinth2.com/editor.html"

#define BLACK 0x000000ffu
#define WHITE 0xffffffffu
#define TEAL 0x3d7a7affu      /* the how-to's text */
#define DARK 0x1e3a3cffu
#define UNPUBLISHED 0xbad6cfffu
#define GROUP_BG 0x0f0d0affu  /* finished_bg */
#define GROUP_FG 0xdcd1a8ffu
#define EASY_BG 0xd9e394ffu

static const float RH = 64;   /* ?listPreferredItemHeight */
static const float GH = 33;   /* a group's row */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static const LabTex *tex(const char *n) { return lab_tex(n); }

static int on_online_screen(void) {
  int s = ui_top();
  return s == SCR_DOWNLOAD || s == SCR_CREATE || s == SCR_HOWTO || s == SCR_INFO;
}

/* dw.a: "Connection Failed!" (or what the server said) */
static void online_error(const LabOnResult *r) {
  if (!on_online_screen())
    return;
  if (r->status == LAB_ON_OFFLINE)
    ui_confirm("Not connected", r->msg, "OK", NULL, NULL);
  else if (r->status == LAB_ON_REFUSED)
    ui_confirm("Connection Failed!", r->msg, "OK", NULL, NULL);
  else
    ui_confirm("Connection Failed!", "Sorry! Couldn't reach the Labyrinth server. Please try later.", "OK", NULL,
               NULL);
}

static void download_sound(void) { lab_audio_play(0, SND_MENU_DOWNLOAD_COMPLETE, 1.0f, 1.0f); }

/* dh.a(zip, false, levelPack): the list's name, author, difficulty, rating
 * and theme over the pack's info.xml */
static void from_list(LabPack *k, const LabNetPack *np) {
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
  k->tutorial = !strncmp(k->id, "ZTUT", 4);
  k->preloaded = 0;
  k->ownlevel = 0;
  k->published = 1;
  k->myrating = k->mydifficulty = -1;
}

/* ID / PIN on list_id_pin (instructions1.xml; also on Create here) */
static void id_pin_strip(float y) {
  const LabTex *bg = tex("list_id_pin");
  if (bg)
    lab_draw_image(bg, 0, y, 320, 26, WHITE);
  else
    lab_draw_rect(0, y, 320, 26, 0xc8d8dcffu);
  const char *id = lab_online_user_id(), *pin = lab_online_pin();
  char s[96];
  if (id[0])
    snprintf(s, sizeof s, "Your ID: %s      Pin code: %s", id, pin);
  else
    snprintf(s, sizeof s, "Your ID and pin code: not given yet");
  lab_draw_text(160, y + 18, 13, TEAL, LAB_CENTER, 1, s);
}

static void group_row(float y, const char *name) {
  lab_draw_rect(0, y, 320, GH, GROUP_BG);
  lab_draw_text(40, y + 7 + 14, 14, GROUP_FG, LAB_LEFT, 1, name);
}

/* ============================================================ the lists */
typedef struct {
  const char *lt;
  int max;            /* at most (the top lists: 100); 0 none */
  LabNetPack *it;
  int n, cap;
  int pages;          /* loaded */
  int more;           /* another page */
  int loading, failed;
  unsigned gen;
  float scroll;
  char msg[200];
} OnList;

enum { L_NEW, L_HOT, L_RND, L_EASY, L_MED, L_HARD, L_RATED, L_DLS, L_PLAYED, L_SEARCH, L_COUNT };
static OnList g_lists[L_COUNT] = {
    {.lt = "new"},          {.lt = "hot"},         {.lt = "rnd"},         {.lt = "all_esy"},
    {.lt = "all_med"},      {.lt = "all_hrd"},     {.lt = "rtg", .max = 100},
    {.lt = "dls", .max = 100}, {.lt = "mpl", .max = 100}, {.lt = "ath"},
};

static void list_clear(OnList *l) {
  free(l->it);
  l->it = NULL;
  l->n = l->cap = l->pages = l->more = l->loading = l->failed = 0;
  l->scroll = 0;
  l->msg[0] = 0;
  l->gen++;
}

typedef struct {
  OnList *l;
  unsigned gen;
} ListReq;

static OnList *cur_list(void);

static void list_done(const LabOnResult *r, void *arg) {
  ListReq q = *(ListReq *)arg;
  free(arg);
  OnList *l = q.l;
  if (q.gen != l->gen)
    return; /* cleared since */
  l->loading = 0;
  if (r->status != LAB_ON_OK) {
    l->failed = 1;
    snprintf(l->msg, sizeof l->msg, "%s", r->msg);
    online_error(r);
    return;
  }
  l->failed = 0;
  l->pages++;
  int old_n = l->n;
  /* de.c(): appended; a pack already listed (the lists move) is not twice */
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
  /* "Get 25 more" pressed: the focus on to the first of them */
  if (old_n && l->n > old_n && l == cur_list() && ui_top() == SCR_DOWNLOAD && ui_focus_id() == 90)
    ui_focus(1000 + old_n);
}

static char g_search_uid[24], g_search_lid[24], g_search_text[40];
static int g_searched;

static void list_load(OnList *l) {
  if (l->loading || !lab_online_enabled())
    return;
  ListReq *q = malloc(sizeof *q);
  if (!q)
    return;
  q->l = l;
  q->gen = l->gen;
  l->loading = 1;
  l->failed = 0;
  lab_online_list(l->lt, l == &g_lists[L_SEARCH] ? g_search_uid : NULL, l->pages + 1, list_done, q);
}

/* ------------------------------------------------ packs being downloaded */
typedef struct {
  LabNetPack np;
  int keep;  /* into the table (the row's download button) */
  int open;  /* its info when it is here (the row) ... */
  unsigned visit; /* ... on this visit of the Download screen */
} Pending;
static unsigned g_dl_visit;
#define MAXP 24
static Pending *g_pend[MAXP];
static int g_npend;

/* the pack looked at before downloading (the info screen's) */
static LabPack g_prev;
static int g_prev_times[256], g_prev_nt;

static Pending *pending_find(const char *lid) {
  for (int i = 0; i < g_npend; i++)
    if (!strcmp(g_pend[i]->np.lid, lid))
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

static void get_done(const LabOnResult *r, void *arg) {
  Pending *p = arg;
  if (r->status != LAB_ON_OK) {
    debugPrintf("[online] %s: not downloaded (%s)\n", p->np.lid, r->msg);
    online_error(r);
    pending_drop(p);
    return;
  }
  LabPack k;
  int times[256], nt = 0;
  int rc = lab_files_store_pack(p->np.lid, r->data, r->len, &k, times, 256, &nt);
  if (rc != 0) {
    if (on_online_screen())
      ui_confirm("Couldn't download", rc < 0 ? "The level pack could not be saved to the SD card (see debug.log)."
                                             : "What came back is not a level pack that can be read.",
                 "OK", NULL, NULL);
    pending_drop(p);
    return;
  }
  from_list(&k, &p->np);
  LabPack *row = NULL;
  if (p->keep) {
    row = lab_levels_add(&k, times, nt);
    if (!row) {
      if (on_online_screen())
        ui_confirm("Couldn't download", "There is no room for more level packs (1024). Delete some from their info screens first.", "OK", NULL, NULL);
      pending_drop(p);
      return;
    }
    lab_levels_save();
    download_sound();
    debugPrintf("[online] downloaded %s \"%s\"\n", k.id, k.name);
  }
  if (p->open && p->visit == g_dl_visit && ui_top() == SCR_DOWNLOAD) {
    if (!row)
      row = lab_levels_find(k.id);
    if (row) {
      g_ui_pack = row;
    } else {
      g_prev = k;
      g_prev_nt = nt;
      memcpy(g_prev_times, times, sizeof(int) * (size_t)nt);
      g_ui_pack = &g_prev;
    }
    ui_push(SCR_INFO);
  }
  pending_drop(p);
}

static void fetch_pack(const LabNetPack *np, int keep, int open) {
  Pending *p = pending_find(np->lid);
  if (p) {
    p->keep |= keep;
    if (open) {
      p->open = 1;
      p->visit = g_dl_visit;
    }
    return;
  }
  if (g_npend >= MAXP || !lab_online_enabled())
    return;
  p = calloc(1, sizeof *p);
  if (!p)
    return;
  p->np = *np;
  p->keep = keep;
  p->open = open;
  p->visit = g_dl_visit;
  g_pend[g_npend++] = p;
  lab_online_get(np->lid, get_done, p);
}

int scr_online_is_preview(const LabPack *p) { return p == &g_prev; }

int scr_online_preview_time(int level) {
  return level >= 0 && level < g_prev_nt ? g_prev_times[level] : 0;
}

/* LevelPackInfoActivityFull.onDownload: the zip is here already */
LabPack *scr_online_keep_preview(void) {
  LabPack *k = lab_levels_add(&g_prev, g_prev_times, g_prev_nt);
  if (!k)
    ui_confirm("Couldn't download", "There is no room for more level packs (1024). Delete some from their info screens first.", "OK", NULL, NULL);
  if (k) {
    lab_levels_save();
    download_sound();
    ui_toast("Downloaded: it is under Downloaded now");
    debugPrintf("[online] downloaded %s \"%s\" (from its info)\n", k->id, k->name);
  }
  return k;
}

/* ------------------------------------------------------ a server pack's row */
enum { ROW_NONE, ROW_INFO, ROW_MAIN };

static int net_row(int id, float y, const LabNetPack *np) {
  LabPack *have = lab_levels_find(np->lid);
  int busy = pending_find(np->lid) != NULL;
  LabPack look;
  memset(&look, 0, sizeof look);
  look.difficulty = np->difficulty;
  uint32_t bg;
  const char *icon, *info;
  scr_diff_style(&look, &bg, &icon, &info);
  lab_draw_rect(0, y, 320, RH, bg);
  lab_draw_rect(0, y + RH - 1, 320, 1, 0x00000038u);
  const LabTex *it = tex(icon);
  if (it)
    lab_draw_image(it, 8, y + 4, 56, 56, WHITE);
  float tx = 8 + 56 + 4 + 16;
  scr_text_fit(tx, y + 17, 10, BLACK, 1, 320 - 50 - tx, np->author);
  scr_text_fit(tx, y + 35, 16, BLACK, 1, 320 - 50 - tx - 4, np->name);
  /* the rating: stars over empty ones, rating * 0.2 of their width */
  const LabTex *se = tex("stars_empty"), *sf = tex("stars");
  if (se && sf) {
    float w = lab_tex_dp_w(se), h = lab_tex_dp_h(se), f = (float)clampf((float)np->rating * 0.2f, 0, 1);
    lab_draw_image(se, tx, y + 40, w, h, WHITE);
    if (f * w * lab_gfx_px_per_dp() > 1.0f)
      lab_draw_image_uv(sf, tx, y + 40, w * f, h, 0, 0, f, 1, WHITE);
  }
  /* the right: download, play, or the progress */
  int r = ROW_NONE;
  if (ui_button(id + 100000, 270, y, 50, RH, UI_NOFOCUS))
    r = ROW_MAIN;
  if (busy) {
    scr_spinner(295, y + RH * 0.5f, 10);
  } else {
    const char *b = have ? "icon_play_button" : "icon_download_button";
    float bw = scr_pic_w(b), bh = scr_pic_h(b);
    scr_pic(b, 270 + (50 - bw) * 0.5f, y + (RH - bh) * 0.5f, ui_down(id + 100000) ? 0xc0c0c0ffu : WHITE);
  }
  if (ui_button(id + 50000, 0, y, 270, RH, UI_NOFOCUS))
    r = ROW_INFO;
  if (ui_button(id, 0, y, 320, RH, UI_NORING | UI_NOTOUCH))
    r = ROW_MAIN;
  if (ui_focused(id) || ui_down(id + 50000))
    lab_draw_image(tex("item_focused"), 0, y, 320, RH, WHITE);
  if (ui_focused(id) && ui_pressed(HidNpadButton_X)) {
    lab_audio_click();
    r = ROW_INFO;
  }
  return r;
}

/* onPlayClicked / onDownloadClicked; onItemClicked */
static void row_action(const LabNetPack *np, int what) {
  LabPack *k = lab_levels_find(np->lid);
  if (what == ROW_MAIN) {
    if (k)
      scr_open_game(k);
    else
      fetch_pack(np, 1, 0);
  } else if (what == ROW_INFO) {
    if (k) {
      g_ui_pack = k;
      ui_push(SCR_INFO);
    } else {
      fetch_pack(np, 0, 1);
    }
  }
}

/* ========================================================== download */
enum { TAB_NEW, TAB_ALL, TAB_TOP, TAB_ID };
static int g_tab = TAB_ALL, g_sub[3];
static int g_dl_focus_want = -1;

static OnList *cur_list(void) {
  switch (g_tab) {
  case TAB_NEW: return &g_lists[L_NEW + g_sub[0]];
  case TAB_ALL: return &g_lists[L_EASY + g_sub[1]];
  case TAB_TOP: return &g_lists[L_RATED + g_sub[2]];
  default: return &g_lists[L_SEARCH];
  }
}

void scr_download_enter(void) { lab_ui_side_info(NULL, NULL, NULL); }

/* a new list on the screen: the focus to its first row next frame */
static void dl_switched(void) {
  OnList *l = cur_list();
  g_dl_focus_want = l->n ? 1000 : 30 + g_tab;
}

/* ---- by ID */
static void search_syntax(void) {
  ui_confirm("Invalid ID",
             "Couldn't find ID. You can only search for user and level IDs on the form A70K3WA9 or A70K3WA9.01",
             "OK", NULL, NULL);
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
  snprintf(g_search_text, sizeof g_search_text, "%s", q);
  /* A70K3WA9 or A70K3WA9.01 (cs.java: the part before the dot is the
   * player's ID, whose packs are listed) */
  char *dot = strchr(q, '.');
  size_t ul = dot ? (size_t)(dot - q) : strlen(q);
  int ok = ul >= 4 && ul <= 16;
  for (size_t i = 0; i < ul && ok; i++)
    ok = isalnum((unsigned char)q[i]);
  if (dot) {
    ok = ok && dot[1] && strlen(dot + 1) <= 4;
    for (const char *s = dot + 1; *s && ok; s++)
      ok = isdigit((unsigned char)*s);
  }
  if (!ok) {
    search_syntax();
    return;
  }
  snprintf(g_search_uid, sizeof g_search_uid, "%.*s", (int)ul, q);
  snprintf(g_search_lid, sizeof g_search_lid, "%s", dot ? q : "");
  OnList *l = &g_lists[L_SEARCH];
  list_clear(l);
  g_searched = 1;
  list_load(l);
}

static void kbd_done(const char *text, void *arg) {
  (void)arg;
  if (text)
    run_search(text);
}

static void open_keyboard(void) {
  lab_kbd_request("Search by ID", "A70K3WA9 or A70K3WA9.01", g_search_text, 16, kbd_done, NULL);
}

static void sd_import(void) {
  int added = 0, failed = 0;
  lab_files_import_sd(&added, &failed);
  char s[128];
  snprintf(s, sizeof s, "%d new level pack%s from the SD card%s", added, added == 1 ? "" : "s",
           failed ? " (some files were not level packs)" : "");
  ui_toast(s);
}

/* ---- the list of the tab */
static void keep_focus_in_view(float top, float bottom, float *scroll, float content) {
  float fx, fy, fw, fh;
  if (ui_focus_rect(&fx, &fy, &fw, &fh) && ui_focus_id() >= 90 && fy + fh > top - 400 && fy < bottom + 400) {
    if (fy < top)
      *scroll -= top - fy;
    else if (fy + fh > bottom)
      *scroll += fy + fh - bottom;
    *scroll = clampf(*scroll, 0, fmaxf(0, content - (bottom - top)));
  }
}

/* rows of a list from y; returns the height used. Rows far off the screen
 * are not registered (the toolkit holds so many); the ones near it are, so
 * the controller can go on to them. */
static float list_rows(OnList *l, int id0, float y, float top, float bottom, int *acted) {
  for (int i = 0; i < l->n; i++, y += RH) {
    if (y + RH < top - 3 * RH || y > bottom + 3 * RH)
      continue;
    int r = net_row(id0 + i, y, &l->it[i]);
    if (r && !*acted) {
      *acted = 1;
      row_action(&l->it[i], r);
    }
  }
  return RH * (float)l->n;
}

/* "Get 25 more", its progress, or "Try again" */
static float more_row(OnList *l, float y, float top, float bottom) {
  if (!(l->more || (l->loading && l->n) || (l->failed && l->n)))
    return 0;
  const float h = 60;
  lab_draw_rect(0, y, 320, h, EASY_BG);
  if (l->loading) {
    ui_button(90, 0, y, 320, h, UI_NORING | UI_SILENT); /* inert: the focus stays */
    scr_spinner(160, y + h * 0.5f, 9);
  } else if (y + h > top - 3 * RH && y < bottom + 3 * RH) {
    if (ui_button(90, 0, y, 320, h, UI_NORING))
      list_load(l);
    lab_draw_text(160, y + h * 0.5f + 6, 16, BLACK, LAB_CENTER, 1, l->failed ? "Try again" : "Get 25 more");
    if (ui_focused(90) || ui_down(90))
      lab_draw_image(tex("item_focused"), 0, y, 320, h, WHITE);
  }
  return h;
}

static float empty_block(OnList *l, float top, float bottom) {
  float cy = (top + bottom) * 0.5f;
  if (!lab_online_enabled()) {
    lab_draw_text_box(24, cy - 30, 272, 14, DARK, LAB_CENTER, 1,
                      "Online is off: [online] enabled = false in config.ini.");
  } else if (l->loading) {
    scr_spinner(160, cy, 16);
  } else if (l->failed) {
    float h = lab_draw_text_box(24, cy - 60, 272, 14, DARK, LAB_CENTER, 1, l->msg[0] ? l->msg : "Couldn't reach the Labyrinth server.");
    float by = cy - 60 + h + 16;
    if (ui_button(90, 60, by, 200, 40, 0))
      list_load(l);
    scr_android_button(60, by, 200, 40, "Try again", ui_down(90), 0, 0);
  } else if (l->pages) {
    lab_draw_text_box(24, cy - 10, 272, 15, DARK, LAB_CENTER, 1, "No level packs here.");
  }
  return 0;
}

/* the By ID tab's page before a search: what to type, and the SD card */
static void search_help(float top, float bottom) {
  float y = top + 18;
  y += lab_draw_text_box(20, y, 280, 14, DARK, LAB_CENTER, 1,
                         "Type a player's ID to see their level packs, or a level pack's ID to find it "
                         "(A70K3WA9 or A70K3WA9.01).");
  const char *me = lab_online_user_id();
  if (me[0]) {
    char s[64];
    snprintf(s, sizeof s, "Your own ID: %s", me);
    y += 10;
    y += lab_draw_text_box(20, y, 280, 14, BLACK, LAB_CENTER, 1, s);
  }
  float sy = bottom - 128;
  if (sy < y + 16)
    sy = y + 16;
  lab_draw_rect(16, sy - 10, 288, 1, 0x00000030u);
  float h = lab_draw_text_box(20, sy, 280, 13, DARK, LAB_CENTER, 1, "Level packs on the SD card");
  h += lab_draw_text_box(20, sy + h + 2, 280, 11, 0x303030ffu, LAB_CENTER, 0,
                         "Zip files in sd:/switch/labyrinth2_nx/levelpacks/ are added when the game starts, "
                         "or now:");
  float by = sy + h + 8;
  if (ui_button(22, 60, by, 200, 36, 0))
    sd_import();
  scr_android_button(60, by, 200, 36, "Look for new packs", ui_down(22), 0, 0);
}

static void search_results(OnList *l, float top, float bottom, float *content_out, int *acted) {
  float y = top - l->scroll, y0 = y;
  if (g_search_lid[0]) {
    /* the pack searched for, then the author's others (cr.java) */
    group_row(y, "Search result");
    y += GH;
    int hit = -1;
    for (int i = 0; i < l->n && hit < 0; i++)
      if (!strcmp(l->it[i].lid, g_search_lid))
        hit = i;
    if (hit >= 0) {
      int r = net_row(900, y, &l->it[hit]);
      if (r && !*acted) {
        *acted = 1;
        row_action(&l->it[hit], r);
      }
    } else {
      lab_draw_rect(0, y, 320, RH, UNPUBLISHED);
      const LabTex *it = tex("icon_unpublished_transp");
      if (it)
        lab_draw_image(it, 8, y + 4, 56, 56, WHITE);
      lab_draw_text(84, y + 38, 16, BLACK, LAB_LEFT, 1, "No levelpack found");
    }
    y += RH;
    if (l->n) {
      group_row(y, "Other levelpacks by this author");
      y += GH;
    }
  } else if (l->n) {
    group_row(y, "Search results");
    y += GH;
  } else {
    lab_draw_text_box(24, (top + bottom) * 0.5f - 10, 272, 15, DARK, LAB_CENTER, 1, "No levelpack found");
  }
  y += list_rows(l, 1000, y, top, bottom, acted);
  y += more_row(l, y, top, bottom);
  *content_out = y - y0;
}

void scr_download_frame(void) {
  if (g_dl_focus_want >= 0) {
    ui_focus(g_dl_focus_want);
    g_dl_focus_want = -1;
  }
  ui_background("bg_light_blue");
  scr_pic("il_logo", (320 - scr_pic_w("il_logo")) * 0.5f, 480 - 85 - scr_pic_h("il_logo"), WHITE);
  const float sub_h = 67.0f / 1.5f, top = 45 + sub_h, bar_h = 94.0f / 1.5f, bottom = 480 - bar_h;
  OnList *l = cur_list();
  if (l != &g_lists[L_SEARCH] && !l->pages && !l->loading && !l->failed && lab_online_enabled())
    list_load(l);
  /* its first rows have come: the focus from the tabs to them */
  static OnList *seen;
  static int seen_n;
  if (seen == l && !seen_n && l->n && (ui_focus_id() < 90 && ui_focus_id() != 20))
    ui_focus(1000);
  seen = l;
  seen_n = l->n;
  /* ---- the list ---- */
  float view_h = bottom - top, content = 0;
  int acted = 0;
  ui_scroll(0, top, 320, view_h, &l->scroll, 1e9f); /* clamped below */
  lab_draw_scissor(0, top, 320, view_h);
  ui_clip(0, top, 320, view_h);
  if (l == &g_lists[L_SEARCH]) {
    if (!g_searched)
      search_help(top, bottom);
    else if (!l->pages)
      empty_block(l, top, bottom);
    else
      search_results(l, top, bottom, &content, &acted);
  } else if (!l->n) {
    empty_block(l, top, bottom);
  } else {
    float y = top - l->scroll;
    y += list_rows(l, 1000, y, top, bottom, &acted);
    y += more_row(l, y, top, bottom);
    content = y - (top - l->scroll);
  }
  lab_draw_scissor(0, 0, 0, 0);
  ui_clip(0, 0, 0, 0);
  l->scroll = clampf(l->scroll, 0, fmaxf(0, content - view_h));
  keep_focus_in_view(top, bottom, &l->scroll, content);
  if (acted && ui_top() != SCR_DOWNLOAD)
    return;
  /* ---- the bar of sub-lists (or the search field) ---- */
  static const char *const subs[3][3] = {{"btn_newcomers", "btn_hot", "btn_random"},
                                          {"btn_easy", "btn_medium", "btn_hard"},
                                          {"btn_rated", "btn_downloaded", "btn_played"}};
  int sub_switch = -1;
  if (g_tab != TAB_ID) {
    const LabTex *bg = tex("bg_light_blue");
    if (bg)
      lab_draw_image_uv(bg, 0, 45, 320, sub_h, 0, 45.0f / 480.0f, 1, top / 480.0f, WHITE);
    float cw = 320.0f / 3.0f;
    for (int i = 0; i < 3; i++) {
      char n[48];
      snprintf(n, sizeof n, "%s%s", subs[g_tab][i], g_sub[g_tab] == i ? "_selected" : "");
      const LabTex *t = tex(n);
      float w = cw, h = t ? cw * (float)t->h / (float)t->w : sub_h;
      if (ui_button(10 + i, cw * (float)i, 45, cw, sub_h, 0))
        sub_switch = i;
      if (t)
        lab_draw_image(t, cw * (float)i, 45 + (sub_h - h) * 0.5f, w, h, WHITE);
    }
    if (ui_pressed(HidNpadButton_ZL))
      sub_switch = (g_sub[g_tab] + 2) % 3;
    if (ui_pressed(HidNpadButton_ZR))
      sub_switch = (g_sub[g_tab] + 1) % 3;
  } else {
    const LabTex *bg = tex("bg_light_blue");
    if (bg)
      lab_draw_image_uv(bg, 0, 45, 320, sub_h, 0, 45.0f / 480.0f, 1, top / 480.0f, WHITE);
    float fx = 8, fy = 45 + 5, fw = 320 - 8 - 8 - 44, fh = sub_h - 10;
    if (ui_button(20, fx, fy, fw, fh, 0))
      open_keyboard();
    lab_draw_rrect(fx, fy, fw, fh, 5, 0x8a9aa0ffu);
    lab_draw_rrect(fx + 1.5f, fy + 1.5f, fw - 3, fh - 3, 4, WHITE);
    if (g_search_text[0])
      scr_text_fit(fx + 10, fy + fh * 0.5f + 6, 16, BLACK, 0, fw - 20, g_search_text);
    else
      lab_draw_text(fx + 10, fy + fh * 0.5f + 6, 16, 0x9a9a9affu, LAB_LEFT, 0, "Enter ID");
    float sw = scr_pic_w("icon_search"), sh = scr_pic_h("icon_search"), sx = 320 - 8 - 40, sy = 45 + (sub_h - sh) * 0.5f;
    if (ui_button(21, sx, sy, 40, sh, 0)) {
      if (g_search_text[0])
        run_search(g_search_text);
      else
        open_keyboard();
    }
    scr_pic(ui_down(21) ? "icon_search_pressed" : "icon_search", sx + (40 - sw) * 0.5f, sy, WHITE);
    if (ui_pressed(HidNpadButton_Y))
      open_keyboard();
  }
  if (sub_switch >= 0 && g_tab != TAB_ID && sub_switch != g_sub[g_tab]) {
    g_sub[g_tab] = sub_switch;
    dl_switched();
  }
  /* ---- the tabs along the bottom (downlist) ---- */
  const LabTex *bar = tex("downlist");
  if (bar)
    lab_draw_image(bar, 0, bottom, 320, bar_h, WHITE);
  else
    lab_draw_rect(0, bottom, 320, bar_h, 0x3a7078ffu);
  static const char *const tabs[4] = {"btn_new", "btn_alllevels", "btn_top25", "btn_id"};
  int tab_switch = -1;
  for (int i = 0; i < 4; i++) {
    float x = 80.0f * (float)i;
    if (g_tab == i) {
      const LabTex *sel = tex("selected_baritem");
      if (sel)
        lab_draw_image(sel, x, bottom, 80, bar_h, WHITE);
    }
    char n[48];
    snprintf(n, sizeof n, "%s%s", tabs[i], g_tab == i ? "_selected" : "");
    float w = scr_pic_w(n), h = scr_pic_h(n);
    if (ui_button(30 + i, x, bottom, 80, bar_h, 0))
      tab_switch = i;
    scr_pic(n, x + (80 - w) * 0.5f, bottom + (bar_h - h) * 0.5f, WHITE);
  }
  if (ui_pressed(HidNpadButton_L))
    tab_switch = (g_tab + 3) % 4;
  if (ui_pressed(HidNpadButton_R))
    tab_switch = (g_tab + 1) % 4;
  if (tab_switch >= 0 && tab_switch != g_tab) {
    g_tab = tab_switch;
    dl_switched();
  }
  /* ---- the navi bar: the tab's title ---- */
  static const char *const heads[4] = {"head_new", "head_alllevels", "head_25", "head_search"};
  static const char *const names[4] = {"New", "All levels", "Top 25", "Search"};
  ui_navibar(heads[g_tab], names[g_tab]);
  ui_default_focus(l->n ? 1000 : g_tab == TAB_ID ? 20 : 30 + g_tab);
  if (ui_back()) {
    ui_pop();
    return;
  }
  ui_hint("A", g_tab == TAB_ID && ui_focus_id() == 20 ? "Type an ID" : "Download / Play");
  ui_hint("X", "Level info");
  ui_hint("L", "Tab to the left");
  ui_hint("R", "Tab to the right");
  if (g_tab == TAB_ID)
    ui_hint("Y", "Search");
  else
    ui_hint("ZR", "Next list");
  ui_hint("B", "Back");
}

/* ============================================================ create */
static struct {
  int want_refresh;   /* on the next visit */
  int refreshing;
  unsigned gen;
  LabNetPack *own;
  int n, cap, page;
  int gets, failed_gets;
  char updated[80];
  float scroll;
  int open[2];
} C = {.open = {1, 1}};

static void create_finish(void) {
  C.refreshing = 0;
  lab_levels_save();
  char when[40];
  lab_local_time(when, sizeof when);
  snprintf(C.updated, sizeof C.updated, "Updated %s", when);
  if (C.failed_gets && ui_top() == SCR_CREATE)
    ui_confirm("Connection Failed!", "Some of your level packs could not be brought from the server. Please try later.",
               "OK", NULL, NULL);
  debugPrintf("[online] your packs: %d on the server, %d not brought\n", C.n, C.failed_gets);
}

/* dn.c() for one of yours: the zip, then its row (a published one's from
 * its info.xml; an unpublished one's from the list, with times of 0) */
static void store_own(const LabNetPack *np, const LabOnResult *r) {
  LabPack k;
  int times[256], nt = 0;
  int rc = lab_files_store_pack(np->lid, r->data, r->len, &k, times, 256, &nt);
  if (rc < 0) {
    C.failed_gets++;
    return;
  }
  if (rc == 1 || !np->published) {
    int parsed_n = rc == 0 ? k.nlevels : 0;
    char aid[64];
    snprintf(aid, sizeof aid, "%s", rc == 0 ? k.author_id : lab_online_user_id());
    memset(&k, 0, sizeof k);
    snprintf(k.author_id, sizeof k.author_id, "%s", aid);
    k.nlevels = np->nlevels > 0 ? np->nlevels : parsed_n;
    nt = k.nlevels < 256 ? k.nlevels : 256;
    memset(times, 0, sizeof times);
  }
  from_list(&k, np);
  k.difficulty = np->difficulty;
  k.ownlevel = 1;
  k.published = np->published;
  if (k.nlevels <= 0) {
    C.failed_gets++;
    return;
  }
  LabPack *old = lab_levels_find(np->lid);
  int old_rev = old ? old->revision : 0, had = old != NULL;
  LabPack *row = lab_levels_add(&k, times, nt);
  if (!row) {
    C.failed_gets++;
    return;
  }
  lab_files_complete_pack(row); /* not published yet: no info.xml in its zip */
  if (had && old_rev < np->rev) {
    /* a new revision (edited on the web): played again from the start, the
     * old best times gone -- they would be sent as the designer times */
    row->nfinished = 0;
    row->current = 0;
    for (int i = 0; i < row->nlevels; i++) {
      char key[128];
      snprintf(key, sizeof key, "TIME_%s_%d", row->id, i);
      lab_reg_remove(key, 2);
    }
    debugPrintf("[online] %s: revision %d (was %d): its progress starts again\n", row->id, np->rev, old_rev);
  }
}

typedef struct {
  unsigned gen;
  LabNetPack np;
} OwnReq;

static void own_get_done(const LabOnResult *r, void *arg) {
  OwnReq *q = arg;
  if (q->gen == C.gen) {
    if (r->status == LAB_ON_OK)
      store_own(&q->np, r);
    else
      C.failed_gets++;
    if (--C.gets == 0)
      create_finish();
  }
  free(q);
}

static void own_list_done(const LabOnResult *r, void *arg) {
  unsigned gen = (unsigned)(uintptr_t)arg;
  if (gen != C.gen)
    return;
  if (r->status != LAB_ON_OK) {
    C.refreshing = 0;
    snprintf(C.updated, sizeof C.updated, "Not updated");
    if (ui_top() == SCR_CREATE)
      online_error(r);
    return;
  }
  for (int i = 0; i < r->npacks; i++) {
    if (C.n == C.cap) {
      int cap = C.cap ? C.cap * 2 : 32;
      LabNetPack *g = realloc(C.own, sizeof *g * (size_t)cap);
      if (!g)
        break;
      C.own = g;
      C.cap = cap;
    }
    C.own[C.n++] = r->packs[i];
  }
  if (r->more && C.page < 8) {
    C.page++;
    lab_online_list("own", NULL, C.page, own_list_done, arg);
    return;
  }
  /* each of them, every time (the Java did): a pack edited on the web
   * may keep its revision until it is published */
  C.gets = 0;
  for (int i = 0; i < C.n; i++) {
    OwnReq *q = malloc(sizeof *q);
    if (!q)
      continue;
    q->gen = gen;
    q->np = C.own[i];
    C.gets++;
    lab_online_get(C.own[i].lid, own_get_done, q);
  }
  if (!C.gets)
    create_finish();
}

static void create_refresh(void) {
  if (C.refreshing || !lab_online_enabled())
    return;
  C.refreshing = 1;
  C.gen++;
  C.n = 0;
  C.page = 1;
  C.gets = C.failed_gets = 0;
  snprintf(C.updated, sizeof C.updated, "Updating...");
  lab_online_list("own", NULL, 1, own_list_done, (void *)(uintptr_t)C.gen);
}

/* for the iPad menus' Create (lab_hd_online.c): the same refresh */
void scr_create_refresh(void) { create_refresh(); }
int scr_create_refreshing(void) { return C.refreshing; }
const char *scr_create_updated(void) { return C.updated; }
const char *scr_editor_url(void) { return EDITOR_URL; }

void scr_online_reset(void) {
  g_dl_visit++;
  for (int i = 0; i < L_COUNT; i++)
    if (i != L_SEARCH)
      list_clear(&g_lists[i]);
  C.want_refresh = 1;
}

void scr_create_enter(void) {
  lab_ui_side_info(NULL, NULL, NULL);
  if (C.want_refresh) {
    C.want_refresh = 0;
    create_refresh();
  }
}

/* cm.a(): the line where a row's author goes */
static const char *own_status(const LabPack *k) {
  if (k->published)
    return k->author[0] ? k->author : "Published";
  if (k->nlevels < 5)
    return "Minimum 5 levels to publish";
  if (k->nfinished < k->nlevels)
    return "Play to publish";
  return "Tap info (X) to publish";
}

void scr_create_frame(void) {
  ui_background("bg_light_blue");
  scr_pic("il_logo", (320 - scr_pic_w("il_logo")) * 0.5f, 480 - 85 - scr_pic_h("il_logo"), WHITE);
  const float top = 45 + 26, bottom = 480 - 60;
  static LabPack *rows[2][1024];
  int nrows[2];
  nrows[0] = lab_levels_query_own(0, rows[0], 1024);
  nrows[1] = lab_levels_query_own(1, rows[1], 1024);
  float content = 0;
  for (int g = 0; g < 2; g++)
    content += GH + (C.open[g] ? RH * (float)nrows[g] : 0);
  float view_h = bottom - top;
  ui_scroll(0, top, 320, view_h, &C.scroll, content - view_h);
  lab_draw_scissor(0, top, 320, view_h);
  ui_clip(0, top, 320, view_h);
  float y = top - C.scroll;
  static const char *const gname[2] = {"Not published", "Published"};
  for (int g = 0; g < 2; g++) {
    if (ui_button(200 + g, 0, y, 320, GH, UI_NORING))
      C.open[g] = !C.open[g];
    group_row(y, gname[g]);
    if (ui_focused(200 + g))
      lab_draw_image(tex("item_focused"), 0, y, 320, GH, WHITE);
    y += GH;
    if (!C.open[g])
      continue;
    for (int i = 0; i < nrows[g]; i++, y += RH) {
      LabPack *k = rows[g][i];
      int id = 1000 + g * 300 + i;
      if (y + RH < top - 3 * RH || y > bottom + 3 * RH)
        continue;
      uint32_t bg = UNPUBLISHED;
      const char *icon = "icon_unpublished_transp", *info = "i_icon_blue_trans";
      if (k->published && k->difficulty >= 0)
        scr_diff_style(k, &bg, &icon, &info);
      lab_draw_rect(0, y, 320, RH, bg);
      lab_draw_rect(0, y + RH - 1, 320, 1, 0x00000038u);
      const LabTex *it = tex(icon);
      if (it)
        lab_draw_image(it, 8, y + 4, 56, 56, WHITE);
      float tx = 8 + 56 + 4 + 16;
      scr_text_fit(tx, y + 18, 10, BLACK, 1, 320 - 50 - tx, own_status(k));
      scr_text_fit(tx, y + 37, 16, BLACK, 1, 320 - 50 - tx - 4, k->name);
      scr_level_dots(tx - 1, y + 40, k->nlevels, k->nfinished, k->current, -1, 1.33f);
      int info_hit = ui_button(id + 100000, 270, y, 50, RH, UI_NOFOCUS);
      float iw = scr_pic_w(info), ih = scr_pic_h(info);
      scr_pic(info, 270 + (50 - iw) * 0.5f, y + (RH - ih) * 0.5f, ui_down(id + 100000) ? 0xc0c0c0ffu : WHITE);
      int play = ui_button(id, 0, y, 270, RH, UI_NORING);
      if (ui_focused(id) || ui_down(id))
        lab_draw_image(tex("item_focused"), 0, y, 320, RH, WHITE);
      if (ui_focused(id) && ui_pressed(HidNpadButton_X)) {
        lab_audio_click();
        info_hit = 1;
      }
      if ((info_hit || play) && C.refreshing) {
        /* the refresh may be replacing this very pack (its zip, a new
         * revision's progress): not while it is played or looked at */
        ui_toast("Your level packs are being updated: a moment");
      } else if (info_hit || play) {
        lab_draw_scissor(0, 0, 0, 0);
        ui_clip(0, 0, 0, 0);
        if (info_hit) {
          g_ui_pack = k;
          ui_push(SCR_INFO);
        } else {
          scr_open_game(k);
        }
        return;
      }
    }
  }
  lab_draw_scissor(0, 0, 0, 0);
  ui_clip(0, 0, 0, 0);
  C.scroll = clampf(C.scroll, 0, fmaxf(0, content - view_h));
  keep_focus_in_view(top, bottom, &C.scroll, content);
  if (!nrows[0] && !nrows[1] && !C.refreshing) {
    const char *msg = !lab_online_enabled()
                          ? "Online is off: [online] enabled = false in config.ini."
                          : "No level packs of yours yet.\n\nThey are made in the level editor, a web page: "
                            "choose New (+) to see how, then Refresh (Y) here.";
    lab_draw_text_box(24, top + 2 * GH + 30, 272, 14, DARK, LAB_CENTER, 1, msg);
  }
  if (C.refreshing)
    scr_spinner(160, (top + bottom) * 0.5f, 16);
  /* the ID and PIN (the how-to's strip: always in sight here) */
  id_pin_strip(45);
  ui_navibar("head_createlevel", "Create levels");
  /* the bar: Refresh, when, New */
  const LabTex *bar = tex("downlist");
  if (bar)
    lab_draw_image(bar, 0, bottom, 320, 60, WHITE);
  else
    lab_draw_rect(0, bottom, 320, 60, 0x3a7078ffu);
  float rw = scr_pic_w("icon_refresh"), rh = scr_pic_h("icon_refresh");
  int refresh = ui_button(40, 4, bottom + (60 - rh) * 0.5f, rw, rh, UI_ROUND) || ui_pressed(HidNpadButton_Y);
  scr_pic(C.refreshing || ui_down(40) ? "icon_refresh_down" : "icon_refresh", 4, bottom + (60 - rh) * 0.5f, WHITE);
  lab_draw_text(160, bottom + 35, 12, WHITE, LAB_CENTER, 1, C.updated[0] ? C.updated : " ");
  float nw = scr_pic_w("icon_new"), nh = scr_pic_h("icon_new"), nx = 320 - 4 - nw;
  int howto = ui_button(41, nx, bottom + (60 - nh) * 0.5f, nw, nh, UI_ROUND) || ui_pressed(HidNpadButton_Plus);
  scr_pic(ui_down(41) ? "icon_new_down" : "icon_new", nx, bottom + (60 - nh) * 0.5f, WHITE);
  if (refresh)
    create_refresh();
  ui_default_focus(nrows[0] ? 1000 : nrows[1] ? 1300 : 41);
  if (howto) {
    ui_push(SCR_HOWTO);
    return;
  }
  if (ui_back()) {
    ui_pop();
    return;
  }
  ui_hint("A", "Play");
  ui_hint("X", "Level info");
  ui_hint("Y", "Refresh");
  ui_hint("+", "New: how to");
  ui_hint("B", "Back");
}

/* ============================================================ how-to */
static struct {
  int page;
  int qr; /* the QR code is up: 2 = since this frame (the press that opened it is not a close) */
} H;

void scr_howto_enter(void) {
  H.page = 0;
  H.qr = 0;
  lab_ui_side_info(NULL, NULL, NULL);
  /* df.a(): the ID and PIN come from registering */
  if (!lab_online_user_id()[0] && lab_online_enabled())
    lab_online_register(NULL, NULL);
}

/* the editor's address as a QR code: 8 pixels a module, the quiet zone
 * around (made once) */
static LabTex *qr_texture(void);
LabTex *scr_online_qr(void) { return qr_texture(); }
static LabTex *qr_texture(void) {
  static LabTex *t;
  static int tried;
  if (t || tried)
    return t;
  tried = 1;
  static uint8_t m[LAB_QR_MAX * LAB_QR_MAX];
  int n = 0;
  if (lab_qr_encode(EDITOR_URL, m, &n))
    return NULL;
  const int q = 4, s = 8, w = (n + 2 * q) * s;
  uint8_t *px = malloc((size_t)w * (size_t)w * 4);
  if (!px)
    return NULL;
  for (int y = 0; y < w; y++)
    for (int x = 0; x < w; x++) {
      int mx = x / s - q, my = y / s - q;
      int dark = mx >= 0 && my >= 0 && mx < n && my < n && m[my * n + mx];
      uint8_t *o = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
      o[0] = o[1] = o[2] = dark ? 0 : 255;
      o[3] = 255;
    }
  t = lab_tex_from_rgba(px, w, w);
  free(px);
  return t;
}

static void open_here_answer(int yes) {
  if (yes && lab_web_request(EDITOR_URL) != 0)
    ui_toast("The browser could not be opened");
}

static void howto_qr(void) {
  lab_draw_rect(0, 0, 320, 480, 0x000000b0u);
  float x = 20, y = 64, w = 280, h = 352;
  lab_draw_rrect(x, y, w, h, 10, WHITE);
  lab_draw_text(160, y + 30, 16, DARK, LAB_CENTER, 1, "Scan it with a phone or tablet");
  LabTex *t = qr_texture();
  float s = 220;
  if (t)
    lab_draw_image(t, 160 - s * 0.5f, y + 42, s, s, WHITE);
  lab_draw_text(160, y + 42 + s + 22, 15, BLACK, LAB_CENTER, 1, EDITOR_SHOWN);
  lab_draw_text_box(x + 16, y + 42 + s + 32, w - 32, 12, 0x404040ffu, LAB_CENTER, 0,
                    "Log in there with the ID and pin code above. A computer's screen suits the editor best.");
  if (H.qr == 2) {
    H.qr = 1;
  } else if (ui_back() || ui_pressed(HidNpadButton_A) || ui_pad->touch_ended) {
    lab_audio_click();
    H.qr = 0;
  }
}

void scr_howto_frame(void) {
  ui_background("bg_light_blue");
  ui_navibar("head_new", "New level pack");
  id_pin_strip(45);
  static const char *const img[3] = {"create01_computer", "create02_id", "create03_edit"};
  const LabTex *im = tex(img[H.page]);
  float iy = 45 + 26 + 20, is = 148;
  if (H.page == 0)
    is = 120; /* room for the QR code and the browser */
  if (im)
    lab_draw_image(im, (320 - is) * 0.5f, iy, is, is, WHITE);
  float ty = iy + is + 20;
  const char *id = lab_online_user_id(), *pin = lab_online_pin();
  if (H.page == 0) {
    ty += lab_draw_text_box(16, ty, 288, 19, TEAL, LAB_CENTER, 0, "Go to");
    ty += lab_draw_text_box(16, ty, 288, 19, DARK, LAB_CENTER, 1, EDITOR_SHOWN);
    ty += lab_draw_text_box(16, ty, 288, 19, TEAL, LAB_CENTER, 0, "on your computer.");
  } else if (H.page == 1) {
    if (id[0]) {
      ty += lab_draw_text_box(16, ty, 288, 19, TEAL, LAB_CENTER, 0, "Enter your ID:");
      ty += lab_draw_text_box(16, ty, 288, 24, DARK, LAB_CENTER, 1, id) + 4;
      ty += lab_draw_text_box(16, ty, 288, 19, TEAL, LAB_CENTER, 0, "and your pin code:");
      ty += lab_draw_text_box(16, ty, 288, 24, DARK, LAB_CENTER, 1, pin);
    } else {
      ty += lab_draw_text_box(16, ty, 288, 15, TEAL, LAB_CENTER, 1,
                              lab_online_busy() ? "Getting your ID and pin code from the Labyrinth server..."
                                                : "Your ID and pin code come from the Labyrinth server: connect "
                                                  "the console to the internet and open this page again.");
    }
  } else {
    ty += lab_draw_text_box(16, ty, 288, 19, TEAL, LAB_CENTER, 0, "Create or edit your levels.");
    ty += 10;
    lab_draw_text_box(16, ty, 288, 13, DARK, LAB_CENTER, 0,
                      "Then choose Refresh on the Create screen: your level packs come here to be played. "
                      "When you have played every level of one, publish it from its info.");
  }
  /* the buttons: Next; Previous + Next; Previous + View levels */
  float lw = scr_pic_w("btn_next_large_up"), lh = scr_pic_h("btn_next_large_up");
  float sw = scr_pic_w("btn_previous_up"), sh = scr_pic_h("btn_previous_up");
  float by = 480 - 10 - lh, sx0 = (320 - (2 * sw + 5)) * 0.5f, sx1 = sx0 + sw + 5;
  int live = !H.qr;
  int go = 0; /* -1 back a page, 1 on, 2 view levels */
  if (H.page == 0) {
    if (live && ui_button(50, (320 - lw) * 0.5f, by, lw, lh, 0))
      go = 1;
    scr_pic(live && ui_down(50) ? "btn_next_large_down" : "btn_next_large_up", (320 - lw) * 0.5f, by, WHITE);
    /* the port's: the address for a phone, the Switch's own browser */
    float bw = 136, bh = 34, b_y = by - 14 - bh;
    if (live && ui_button(51, 20, b_y, bw, bh, 0))
      H.qr = 2;
    scr_android_button(20, b_y, bw, bh, "QR code", live && ui_down(51), 0, 0);
    if (live && ui_button(52, 164, b_y, bw, bh, 0))
      ui_confirm("Open it here?",
                 "The level editor was made for a computer's browser and mouse: the Switch's browser may not "
                 "manage it. Log in with your ID and pin code.",
                 "Open", "Cancel", open_here_answer);
    scr_android_button(164, b_y, bw, bh, "Open on Switch", live && ui_down(52), 0, 0);
  } else {
    if (live && ui_button(53, sx0, by, sw, sh, 0))
      go = -1;
    scr_pic(live && ui_down(53) ? "btn_previous_down" : "btn_previous_up", sx0, by, WHITE);
    const char *nb = H.page == 1 ? "btn_next_small" : "btn_viewlevel";
    char up[40], dn[40];
    snprintf(up, sizeof up, "%s_up", nb);
    snprintf(dn, sizeof dn, "%s_down", nb);
    if (live && ui_button(54, sx1, by, sw, sh, 0))
      go = H.page == 1 ? 1 : 2;
    scr_pic(live && ui_down(54) ? dn : up, sx1, by, WHITE);
  }
  if (live && ui_pressed(HidNpadButton_ZL | HidNpadButton_L) && H.page > 0)
    go = -1, lab_audio_click();
  if (live && ui_pressed(HidNpadButton_ZR | HidNpadButton_R) && H.page < 2)
    go = 1, lab_audio_click();
  if (H.qr)
    howto_qr();
  if (go == -1 || go == 1) {
    H.page += go;
    ui_focus(H.page == 0 ? 50 : 54);
  } else if (go == 2) {
    C.want_refresh = 1; /* onViewLvls: back to Create, which refreshes */
    ui_pop();
    return;
  }
  ui_default_focus(H.page == 0 ? 50 : 54);
  if (live && ui_back()) {
    ui_pop();
    return;
  }
  if (H.qr) {
    ui_hint("B", "Close");
  } else {
    ui_hint("A", "Select");
    ui_hint("L", "Previous page");
    ui_hint("R", "Next page");
    ui_hint("B", "Back");
  }
}

/* ============================================================ publish */
static int g_publishing;
int scr_online_publishing(void) { return g_publishing; }

typedef struct {
  char lid[64];
} PubReq;

/* a dialog on the online screens; over anything else (a game), a toast */
static void tell(const char *title, const char *text, const char *toast) {
  if (on_online_screen())
    ui_confirm(title, text, "OK", NULL, NULL);
  else
    ui_toast(toast);
}

/* bx.java: published -> the row from the server's answer; by / bz / ca */
static void publish_done(const LabOnResult *r, void *arg) {
  PubReq *q = arg;
  g_publishing = 0;
  LabPack *k = lab_levels_find(q->lid);
  if (r->status == LAB_ON_OK && k) {
    k->published = 1;
    if (r->pack.name[0])
      snprintf(k->name, sizeof k->name, "%s", r->pack.name);
    if (r->pack.author[0])
      snprintf(k->author, sizeof k->author, "%s", r->pack.author);
    k->revision = r->pack.rev;
    if (r->pack.nlevels > 0)
      k->nlevels = r->pack.nlevels;
    k->rating = r->pack.rating;
    if (r->pack.difficulty >= 0)
      k->difficulty = r->pack.difficulty;
    k->theme = r->pack.theme;
    k->reqver = r->pack.rqv;
    lab_levels_save();
    debugPrintf("[online] published %s\n", q->lid);
    tell("Published successfully", "Your level pack can now be enjoyed by players around the world!",
         "Your level pack is published");
  } else if (r->http) {
    /* the server answered, but not with the pack (ClientProtocolException) */
    char msg[256];
    snprintf(msg, sizeof msg, "Level pack not accepted by server. You might need to add more elements.%s%s",
             r->status == LAB_ON_REFUSED ? "\n\n" : "", r->status == LAB_ON_REFUSED ? r->msg : "");
    debugPrintf("[online] %s not published: %s\n", q->lid, r->msg);
    tell("Couldn't publish", msg, "Your level pack could not be published");
  } else {
    online_error(r);
    if (!on_online_screen())
      ui_toast("Couldn't reach the Labyrinth server to publish");
  }
  free(q);
}

void scr_online_publish(LabPack *p) {
  if (g_publishing || !p)
    return;
  if (!lab_online_enabled()) {
    ui_confirm("Couldn't publish", "Online is off: [online] enabled = false in config.ini.", "OK", NULL, NULL);
    return;
  }
  /* what the server wants (the Create list's hints; the FAQ's rules) */
  if (p->nlevels < 5) {
    ui_confirm("Couldn't publish", "Minimum 5 levels to publish.", "OK", NULL, NULL);
    return;
  }
  if (p->nfinished < p->nlevels) {
    ui_confirm("Play to publish",
               "Play every level of the pack first: your times are its designer times.", "OK", NULL, NULL);
    return;
  }
  /* e(): your best time on each level, comma separated */
  char times[1024];
  size_t o = 0;
  times[0] = 0;
  for (int i = 0; i < p->nlevels && o < sizeof times - 16; i++)
    o += (size_t)snprintf(times + o, sizeof times - o, "%s%d", i ? "," : "", lab_levels_best_time(p->id, i));
  PubReq *q = calloc(1, sizeof *q);
  if (!q)
    return;
  snprintf(q->lid, sizeof q->lid, "%s", p->id);
  g_publishing = 1;
  lab_online_publish(p->id, p->revision, times, publish_done, q);
}
