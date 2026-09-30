/* lab_ui.h -- the screens' toolkit (lab_ui.c) and the screens (lab_screens.c).
 *
 * Immediate mode, in dp of the 320x480 portrait: a screen draws itself and
 * registers its buttons each frame; the toolkit moves the focus between
 * them with the D-pad / left stick (spatially: the nearest in the direction
 * pressed), presses the focused one with A, and turns taps on the
 * touchscreen into presses (a press is a release over the button the finger
 * went down on, as Android's click). MIT. */
#ifndef LAB_UI_H
#define LAB_UI_H
#include "lab.h"

enum {
  UI_ROUND = 1,     /* the ring is a circle */
  UI_NOFOCUS = 2,   /* touch only */
  UI_NORING = 4,    /* focusable, but the screen shows the focus itself */
  UI_NOTOUCH = 8,   /* the screen handles touches on it itself */
  UI_SILENT = 16,   /* no click sound when pressed */
};

/* the frame's input (lab_ui.c keeps it) */
extern const LabPad *ui_pad;
/* A button: rect in dp as drawn. Returns 1 the frame it is pressed. */
int ui_button(int id, float x, float y, float w, float h, int flags);
int ui_focused(int id);   /* has the focus (and the ring shows) */
int ui_down(int id);      /* held: a finger on it, or A on the focused one */
void ui_focus(int id);    /* move the focus there */
int ui_focus_id(void);
void ui_default_focus(int id); /* where it goes if it is on nothing */
/* The focused button's rect this frame (for scrolling it into view). */
int ui_focus_rect(float *x, float *y, float *w, float *h);
/* Touch clip for the buttons that follow (a list's viewport); w <= 0: none. */
void ui_clip(float x, float y, float w, float h);
/* The screen's buttons: pressed / back. */
int ui_back(void);        /* B, this frame */
void ui_hold_nav(int all); /* the screen uses the D-pad's left / right (all: every way) this frame */
int ui_pressed(uint64_t button);
void ui_eat(uint64_t buttons); /* this frame's press of these used up (later code does not see it) */
/* A drag that scrolls (a finger moving in the area): the offset changes,
 * and the finger's press is cancelled. Also the right stick. */
void ui_scroll(float x, float y, float w, float h, float *offset, float max);
/* Screen stack */
enum {
  SCR_SPLASH, SCR_MAIN, SCR_PACKS, SCR_INFO, SCR_SETTINGS, SCR_AWARDS, SCR_CREDITS, SCR_DOWNLOAD,
  SCR_CREATE, SCR_HOWTO, SCR_GAME, SCR_VERSUS, SCR_COUNT
};
void ui_push(int screen);
void ui_reset(int screen);  /* the stack is just this screen */
void ui_pop(void);
int ui_top(void);
/* A modal question over the screen: fn(1) on yes. */
void ui_confirm(const char *title, const char *text, const char *yes, const char *no, void (*fn)(int));
void ui_toast(const char *text); /* a line at the bottom for a few seconds */
int ui_dialog_open(void);

/* A dialog's button (drawn; ui_button for its presses). */
void ui_button_box(float x, float y, float w, float h, const char *label, int focus, int down);

/* the controls legend on the side panel */
void ui_hint(const char *button, const char *what);
typedef struct {
  char b[12], w[40];
} Hint;
void ui_glyph(float x, float y, const char *button, float size); /* a Switch button */
/* the canvas: 320 x 480 dp, or the iPad menus' points (lab_hd.c) */
float ui_canvas_w(void);
float ui_canvas_h(void);
float ui_px_per_unit(void);
/* lab_hd.c: the iPad menus (the player's .ipa is there). Their canvas is
 * 640 points tall (the iPad's was 768: on the Switch's smaller screen the
 * menus are drawn a fifth bigger; the main menu keeps the iPad's picture) */
#define HD_CANVAS_H 640.0f
int lab_hd_on(void);
void lab_hd_hints(const Hint *h, int n); /* the legend, bottom right */
void hd_enter(int screen);
void hd_frame(int screen);

/* common pieces */
void ui_navibar(const char *header_drawable, const char *fallback_text);
void lab_ui_side_info(const char *title, const char *sub, const char *sub2);
void ui_background(const char *drawable);
float ui_time(void);              /* seconds since start */

/* lab_screens.c */
void scr_enter(int screen);
void scr_frame(int screen);
void scr_enter_android(int screen); /* the phone's screens (the iPad menus use some) */
void scr_frame_android(int screen);
extern LabPack *g_ui_pack; /* the pack the info / game screens are about */
void scr_game_ended(void);
void scr_request_quit(void);
/* ... its pieces, for the online screens */
void scr_pic(const char *name, float x, float y, uint32_t rgba); /* hdpi size, top-left */
float scr_pic_w(const char *name);
float scr_pic_h(const char *name);
void scr_text_fit(float x, float y, float size, uint32_t rgba, int bold, float w, const char *s);
void scr_android_button(float x, float y, float w, float h, const char *label, int down, int toggle, int on);
void scr_diff_style(const LabPack *k, uint32_t *bg, const char **icon, const char **info);
void scr_level_dots(float x, float y, int n, int finished, int current, int selected, float pad);
void scr_open_game(LabPack *p);
void scr_spinner(float cx, float cy, float r); /* the ProgressBar */

/* the Awards screen's data (the engine's managers), for the iPad menus too */
#define MAX_AWARDS 64
typedef struct {
  int n;
  int idx[MAX_AWARDS];
  char name[MAX_AWARDS][64], desc[MAX_AWARDS][128], id[MAX_AWARDS][40];
  float prog[MAX_AWARDS];
  int got[MAX_AWARDS];
  int balls[3];
  int count, time_played;
  float distance;
  float scroll;
} ScrAwards;
const ScrAwards *scr_awards(void);
void scr_awards_load(void);
const char *scr_award_icon(const char *id); /* the APK's drawable */
extern const char *const k_credits[4][2];
int scr_settings_over_game(void);
void scr_settings_enter(void);
void scr_settings_back(void);

/* lab_screens_online.c: Download levels, Create, the editor's how-to */
void scr_download_enter(void);
void scr_download_frame(void);
void scr_create_enter(void);
void scr_create_frame(void);
void scr_howto_enter(void);
void scr_howto_frame(void);
void scr_online_reset(void); /* opened from the main menu: fresh lists */
/* the level info screen's online side */
int scr_online_is_preview(const LabPack *p);  /* a server pack not downloaded */
int scr_online_preview_time(int level);       /* its designer time */
LabPack *scr_online_keep_preview(void);       /* Download: into the table */
void scr_online_publish(LabPack *p);
int scr_online_publishing(void);
void scr_create_refresh(void);
int scr_create_refreshing(void);
const char *scr_create_updated(void);
const char *scr_editor_url(void);
LabTex *scr_online_qr(void);

#endif
