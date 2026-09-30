/* lab_hd.h -- the iPad menus' pieces (lab_hd.c) and screens (lab_hd_packs.c,
 * lab_hd_online.c). The canvas: iPad points, 768 tall, hd_cw() wide. MIT. */
#ifndef LAB_HD_H
#define LAB_HD_H
#include "lab.h"
#include "lab_ui.h"

#define HD_LW 320.0f   /* the split view's list */
#define HD_ID_BACK 9   /* the navigation bar's back button */
#define HD_ID_TABS 10  /* the iPad levels / iPhone levels tabs */

float hd_cw(void);
float hd_ch(void); /* the canvas: HD_CANVAS_H points tall */
const LabTex *hd_tex(const char *name); /* the .ipa's "menugraphics/tab_preload" */
float hd_w(const char *name);           /* its size in points */
float hd_h(const char *name);
void hd_pic(const char *name, float x, float y, uint32_t rgba);
void hd_pic_wh(const char *name, float x, float y, float w, float h, uint32_t rgba);
void hd_text_fit(float x, float y, float size, uint32_t c, int bold, float w, const char *s);
int hd_pic_button(int id, const char *up, const char *down, float x, float y, int flags);
int hd_navibar(const char *title_pic, const char *title_text, const char *back_pic, const char *back_text);
void hd_split_bg(void);
void hd_split_line(void);
/* a list's scroll: where it is going, where it is drawn (easing there; a
 * finger or the right stick move both), its bounce at an end, when it last
 * moved (its indicator) */
typedef struct {
  float to, at, bump, moved, last_at;
} HdScroll;
float hd_scroll(HdScroll *s, float x, float top, float w, float view, float content); /* where it is drawn */
void hd_scroll_bar(const HdScroll *s, float right, float top, float view, float content); /* after its rows */

/* A list the controller works in (left of the split): one thing to focus
 * (HD_ID_LIST, the ring round it) until A goes in; then up / down move the
 * lit row (cur), right goes to the pane's main button (pane_main), B out.
 * Its n rows' tops in the list (ry, the groups' titles between them) and
 * heights (rh); chosen: the row whose info shows (-1: none). */
#define HD_ID_LIST 8
typedef struct {
  int in, cur;
  int dir, stick_right, prev;
  float held, next, moved, to_was;
} HdList;
enum { HD_LIST_NONE, HD_LIST_A, HD_LIST_RIGHT }; /* A on row cur; right to the pane (row cur) */
int hd_list(HdList *L, HdScroll *s, float top, float view, float content, const float *ry, const float *rh, int n,
            int chosen, int pane_main); /* before the rows are drawn (then hd_scroll) */
int hd_list_dwell(const HdList *L, int chosen, float dwell); /* the row rested on: to choose, else -1 */
int hd_list_lit(const HdList *L, int i, int chosen);          /* row i lit (blue) */
void hd_list_reset(HdList *L);                                /* out of it, at the top (a new visit) */
int hd_device_tabs(float y, int *ipad);
int hd_segments(int id0, float y, const char *const names[3], int *sel);
void hd_popover(float x, float y, float w, float h, float ax, int up);
void hd_switch(float x, float y, int on, int focus, int down);
void hd_rbutton(float x, float y, float w, float h, const char *label, int focus, int down);

/* lab_hd_packs.c: Single player / Multi player, and the level info pane */
void hd_packs_set_multi(int on);
void hd_packs_enter(void);
void hd_packs_frame(void);
/* a pack's row in a list (320 x 70), lit (blue) or not; what a finger
 * pressed: 1 the row, 2 its button (play / download) */
int hd_pack_row(int id, float y, const LabPack *k, int lit, const char *button_icon);
float hd_group_header(float y, const char *pic);
enum { HD_INFO_PLAY, HD_INFO_DOWNLOAD, HD_INFO_OWN };
enum { HD_ACT_NONE, HD_ACT_PLAY, HD_ACT_FAVE, HD_ACT_DOWNLOAD, HD_ACT_MOREINFO, HD_ACT_PUBLISH, HD_ACT_DELETE };
/* The info pane (right of the list) for pack p (a row of the table, or a
 * server pack being looked at); its level chosen in *level. */
void hd_info_show(const LabPack *p);   /* NULL: nothing */
const LabPack *hd_info_pack(void);
int hd_info_level(void);
int hd_info_frame(int mode, int designer_only);
void hd_info_release(void);            /* the thumbnails: before a game */
void hd_open_game(LabPack *p, int level);
void hd_diff_style(int difficulty, int tutorial, uint32_t *bg, const char **icon, const char **play);

/* lab_hd_online.c */
void hd_online_reset(void);
void hd_download_enter(void);
void hd_download_frame(void);
void hd_create_enter(void);
void hd_create_frame(void);
void hd_howto_enter(void);
void hd_howto_frame(void);

#endif
