/* lab.h -- the Labyrinth 2 side of the port: what stands in for the game's
 * Java app (its activities, views, databases and managers), and how the
 * pieces talk to each other. MIT. */
#ifndef LAB_H
#define LAB_H
#include <stddef.h>
#include <stdint.h>
#include "jni.h"
#include "so_util.h"

/* ---------------------------------------------------------- lab_loader.c */
extern so_module g_mod_game;
int lab_load_module(void);
void lab_run_constructors(void);
/* A JNI export of the engine ("Java_se_illusionlabs_..."), NULL if absent. */
void *lab_native(const char *symbol);
/* Local play: the engine loaded a second time (its own globals), and a
 * native of copy eng (0, 1). */
int lab_load_second_engine(void);
int lab_load_ipad_engine(void); /* copy 2: 576 x 768 boards (iPad levels) */
void *lab_native_in(int eng, const char *symbol);
so_module *lab_module(int eng);
/* an iPad board's copy of the engine (2, and local play's second: 3) */
static inline int lab_eng_ipad(int eng) { return eng >= 2; }
int lab_load_ipad_race_engine(void); /* copies 2 and 3 */
/* local play: the copy player 1's board is (0, or 2 on iPad boards) */
extern int g_lab_race_p1;

/* ------------------------------------------------------------- lab_apk.c */
/* The APK's entries (assets/, res/...), read through the block cache. */
int lab_apk_init(const char *apk_path);
/* Whole entry into a new malloc()ed buffer (NULL if absent). +1 byte, 0. */
void *lab_apk_read(const char *name, size_t *len);
int lab_apk_exists(const char *name);
/* Every entry name starting with prefix, in APK order: fn(name, arg). */
void lab_apk_list(const char *prefix, void (*fn)(const char *name, uint32_t crc, void *arg), void *arg);
void lab_apk_report(void);
/* The iPad game's .ipa (Labyrinth 2 HD, the player's own): names are under
 * its Payload/<x>.app/ ("menugraphics-ipad/tab_ipad.png"). */
int lab_ipa_init(const char *path);   /* 0: opened */
int lab_ipa_present(void);
const char *lab_ipa_path(void);
int lab_ipa_exists(const char *name);
void *lab_ipa_read(const char *name, size_t *len);
void lab_ipa_list(const char *prefix, void (*fn)(const char *name, uint32_t crc, void *arg), void *arg);
uint32_t lab_ipa_crc(const char *name);

/* ----------------------------------------------------------- lab_files.c */
/* The engine's files dir: <root>/data/files (FileSystemUtil.setResourcePath). */
const char *lab_files_dir(void);
const char *lab_files_ipad_dir(void); /* the iPad engine's: iPad packs, floors; else files/ */
/* First launch / a new game.apk: the assets and the official level packs
 * into the files dir, the packs into the level table. Then the SD card's
 * level packs (<root>/levelpacks/<name>.zip). */
void lab_files_setup(void);
/* The level pack zips on the SD card: imports new ones (the Download screen). */
int lab_files_import_sd(int *added, int *failed);
const char *lab_sd_packs_dir(void);

/* -------------------------------------------------------- lab_registry.c */
/* ZRegistry: the four tables (string, binary, integer, float). */
void lab_reg_load(void);
void lab_reg_save(void);                  /* if changed; also every few seconds */
void lab_reg_tick(void);                  /* once a frame: debounced save */
int lab_reg_get_int(const char *key, int def);
float lab_reg_get_float(const char *key, float def);
const char *lab_reg_get_string(const char *key); /* NULL: no value */
void lab_reg_set_int(const char *key, int v);
void lab_reg_set_float(const char *key, float v);
void lab_reg_set_string(const char *key, const char *v);
void lab_reg_remove(const char *key, int table); /* 0 string 1 binary 2 int 3 float */

/* ---------------------------------------------------------- lab_levels.c */
/* LevelsDB: the level packs (the Java's `levels` table). */
typedef struct {
  char id[64];          /* _id: the pack's level id ("Z0000000.01") = its file in zipfiles/ */
  char name[128];       /* levelname */
  char author_id[64];
  char author[96];      /* authorname */
  int difficulty;       /* 0 easy 1 medium 2 hard */
  int nlevels;          /* nbrlevels */
  int nfinished;        /* nbrlevelsfinished */
  int current;          /* currentlevel */
  int tutorial;         /* istutorial (id starts ZTUT) */
  int preloaded;        /* an official pack */
  int published, ownlevel, revision, theme, reqver;
  int myrating, mydifficulty, playcount;
  double rating;
  int ipad;             /* an iPad pack (576 x 768 board): the iPad engine plays it */
  int fave;             /* the iPad menus' Faves */
} LabPack;
void lab_levels_load(void);
void lab_levels_save(void);
int lab_levels_count(void);
LabPack *lab_levels_at(int i);
LabPack *lab_levels_find(const char *id);
/* Insert or update from a pack's info.xml (dh.a + LevelsDB.a(LevelPack)),
 * designer times into the registry (DTIME_<id>_<n>). */
LabPack *lab_levels_add(const LabPack *p, const int *times, int ntimes);
int lab_levels_remove(const char *id);
/* The list screen's groups: 0 Ongoing, 1 New, 2 Finished; official or not.
 * Fills ids (sorted as the Java: tutorials first, then by name). */
int lab_levels_query(int group, int official, LabPack **out, int cap); /* iPhone packs */
/* The iPad menus' lists: kind 0 downloaded, 1 official, 2 faves; ipad: its
 * board (the iPad levels / iPhone levels tabs); group as above, -1 all. */
int lab_levels_query_dev(int group, int kind, int ipad, LabPack **out, int cap);
int lab_levels_query_own(int published, LabPack **out, int cap);
int lab_levels_imported(const char *zip);  /* imported_zips */
void lab_levels_mark_imported(const char *zip);
/* Best (TIME_) and designer (DTIME_) times, ms. */
int lab_levels_best_time(const char *id, int level);
int lab_levels_designer_time(const char *id, int level);
/* Parse a pack's info.xml into p/times (returns 0 on success). */
int lab_levels_parse_info(const char *xml, LabPack *p, int *times, int cap, int *ntimes);
/* (lab_files.c) A level pack's zip from the server: into zipfiles/<id>, its
 * info.xml read into p / times. 0 = stored and read, 1 = stored but no
 * info.xml that could be read, -1 = not stored (not a zip, a bad id, the
 * card). */
int lab_files_store_pack(const char *id, const void *zip, size_t len, LabPack *p, int *times, int cap,
                         int *ntimes);
/* (lab_files.c) LevelPack.a(): the zip and the table's row (the table saved). */
int lab_files_delete_pack(const char *id);
int lab_files_have_pack(const char *id);  /* zipfiles/<id> is there */
int lab_files_have_pack_dev(const char *id, int ipad); /* ... for a pack maybe not in the table */
/* how many of its levels (level0.xml up, at most want) its zip has: 0 none, or no zip */
int lab_files_pack_levels(const char *id, int ipad, int want);
int lab_files_write_png(const char *path, const uint8_t *rgba, int w, int h); /* 0 ok */
/* (lab_files.c) A pack whose zip has no info.xml (yours, not published
 * yet): one made from its row, as every other pack has. */
int lab_files_complete_pack(const LabPack *p);
/* (lab_files.c) Before a game: its zip is there and has level0.xml ..
 * level<n-1>.xml (what it holds goes to debug.log); else -1 and why. */
int lab_files_check_pack(const LabPack *p, char *why, size_t cap);

/* ------------------------------------------------------------ lab_java.c */
/* The Java objects the engine is handed (created by lab_java_init). */
extern JObj *g_zregistry, *g_levelsdb, *g_soundmgr, *g_accel, *g_ztouch;
JObj *lab_java_accel(int eng); /* each copy's Accelerometer */
/* Local play: a race is on (its level); the copy calling in (lab_game.c). */
extern int g_lab_race, g_lab_race_level, g_lab_eng;
void lab_java_init(void);
/* ea.a(): the managers' native init()s, in the Java's order. */
void lab_java_boot_natives(void);
void lab_java_boot_natives_eng(int eng);
/* A LevelPack object for the engine, from a table row (refs = 1). */
JObj *lab_java_levelpack(const LabPack *p);
/* The activity the engine's GameActivity.init was given. */
extern JObj *g_game_activity;

/* ----------------------------------------------------------- lab_audio.c */
/* SoundManager (SoundPool): the sounds decoded from res/raw, mixed to audout. */
void lab_audio_init(void);                  /* decode + start the output */
int lab_audio_ready(void);
void lab_audio_play(int source, int id, float gain, float pitch);
void lab_audio_stop(int source, int id);
void lab_audio_update(int source, int id, float gain, float pitch);
void lab_audio_stop_all(void);
void lab_audio_enable(int on);              /* enable/disableSounds */
void lab_audio_mute_game(int on);           /* SoundManager.c()/d(): the "m" flag */
void lab_audio_pause(int paused);           /* HOME: silence, no mixing */
void lab_audio_click(void);                 /* SoundManager.b(): the menu click */
uint32_t lab_audio_mixes(void);
void lab_audio_selftest(void);
/* Sound ids (dp.java's order). */
enum {
  SND_AWARD, SND_MENU_BUTTON_CLICK, SND_MENU_DOWNLOAD_COMPLETE, SND_BALLENLARGER_BOUNCE,
  SND_BALL_BALL_COLLISION, SND_BALL_CANNONBALL_COLLISION, SND_BALL_CLOWNBALL_COLLISION,
  SND_BALL_ROLL, SND_BALL_ROLL_METAL, SND_BALL_ROLL_WOOD, SND_BALL_ROLL_PLASTIC, SND_BUMPER_ROUND,
  SND_BUMPER_TRI, SND_BUTTON, SND_CANNON_BALL_EXPLOSION, SND_CANNON_BOUNCE, SND_CANNON_FIRE,
  SND_CHECKPOINT, SND_CLONING_STARTED, SND_CLONING_ENABLED, SND_DOOR_HIT, SND_DOOR_MOVING,
  SND_DOOR_STOP, SND_FAN_HIT_BASE, SND_FAN_HIT_BLADE, SND_FAN_SPIN, SND_GOAL, SND_HOLE,
  SND_LASER_BEAM_BUZZ, SND_LASER_BEAM_ON, SND_LASER_BEAM_OFF, SND_LASER_TOWER_BOUNCE,
  SND_MAGNET_BOUNCE, SND_MERRYGOROUND_BOUNCE, SND_MERRYGOROUND_SPIN, SND_MOVINGWALL_BOUNCE,
  SND_POPUP_HIDE, SND_POPUP_SHOW, SND_WARNINGLIGHT_ALARM, SND_WARNINGLIGHT_BOUNCE, SND_WALL,
  SND_CANNONBALL_START, SND_BUMPER_ROUND_CANNONBALL, SND_CANNON_BOUNCE_CANNONBALL,
  SND_WALL_CANNONBALL, SND_MOVINGWALL_BOUNCE_CANNONBALL, SND_BUMPER_TRI_CANNONBALL,
  SND_MERRYGOROUND_BOUNCE_CANNONBALL, SND_MAGNET_BOUNCE_CANNONBALL,
  SND_WARNINGLIGHT_BOUNCE_CANNONBALL, SND_LASER_TOWER_BOUNCE_CANNONBALL,
  SND_FAN_HIT_BASE_CANNONBALL, SND_DOOR_HIT_CANNONBALL, SND_BALLENLARGER_BOUNCE_CANNONBALL,
  SND_COUNT
};

/* ----------------------------------------------------------- lab_font.c */
/* The Switch's shared fonts (pl), rasterised. */
int lab_font_init(void);
int lab_font_ready(void);
/* One line's advance at px; coverage of a string into an 8-bit buffer
 * (baseline at y, x from 0), clipped to w x h; bold: a wider stroke. */
float lab_font_width(float px, const char *utf8, int bold);
float lab_font_ascent(float px);
float lab_font_descent(float px);
void lab_font_draw(uint8_t *dst, int w, int h, int stride, float x, float y, float px,
                   const char *utf8, int bold);

/* Emoji in a line drawn from x = 0: glyphs and x (pixels); the count. */
int lab_font_emoji(float px, const char *utf8, float *xs, int *glyphs, int cap);
int lab_font_emoji_bold(float px, const char *utf8, int bold, float *xs, int *glyphs, int cap);

/* ----------------------------------------------------------- lab_emoji.c */
/* Colour emoji from an emoji font on the SD card (sbix or CBDT). */
int lab_emoji_init(void);  /* 0: a font is there */
int lab_emoji_ready(void);
/* At s: an emoji (or a character to leave out)? Its bytes (0: no), its
 * glyph (0: nothing drawn) and advance in em. */
int lab_emoji_match(const char *s, int *glyph, float *adv_em);
/* A glyph's picture (RGBA, malloc) and where it sits: left and top (above
 * the baseline) in em, and one pixel's size in em. */
uint8_t *lab_emoji_rgba(int glyph, int *w, int *h, float *left_em, float *top_em, float *px_em);

/* ------------------------------------------------------------ lab_text.c */
/* ZFont.renderTextToTexture (GL thread, the engine's context current). */
int lab_text_render(JObj *zfont, const char *text, const char *font, int size, int align,
                    int max_w, int unknown, int tex, int tex_w, int tex_h);
/* Word-wrapped lines of text at px within max_w (the UI uses this too):
 * returns the line count, fills starts/lens. */
int lab_text_wrap(const char *text, float px, int bold, float max_w, int *starts, int *lens, int cap);

/* ------------------------------------------------------------- lab_gfx.c */
/* EGL (an ES 1.1 context on the default window), the portrait surface and
 * the composite. All on the main thread. */
int lab_gfx_init(void);
/* The portrait surface: the size in pixels, and in dp (320 x 480). */
int lab_gfx_surface_w(void);
int lab_gfx_surface_h(void);
float lab_gfx_px_per_dp(void);
/* Start a frame: the portrait surface bound, viewport = all of it. */
void lab_gfx_begin_portrait(void);
/* The iPad board's surface (3:4: the iPad engine draws a 576 x 768 board
 * into it), shown turned as 4:3 in the middle of the screen. */
void lab_gfx_begin_ipad(void);
int lab_gfx_ipad_w(void);
int lab_gfx_ipad_h(void);
void lab_gfx_show_ipad(int on); /* the picture shown is the iPad board's */
int lab_gfx_showing_ipad(void);
/* The iPad menus (lab_hd*.c): a landscape surface the window's size, drawn
 * in points (768 tall, lab_gfx_hd_canvas_w wide: 1365 at 16:9), shown
 * instead of the portrait picture (lab_gfx_show_hd, each frame). */
void lab_gfx_begin_hd(void);
int lab_gfx_hd_w(void);
int lab_gfx_hd_h(void);
float lab_gfx_hd_canvas_w(void);
float lab_gfx_hd_canvas_h(void);
void lab_gfx_set_hd_canvas_h(float h);
void lab_gfx_show_hd(int on);
void lab_gfx_show_hd_over(int on); /* ... drawn over the game's picture (Settings over a level) */
int lab_gfx_showing_hd(void);
void lab_gfx_touch_to_canvas(float sx, float sy, float *x, float *y);
/* Draw the side panels + the portrait picture to the window, then the
 * overlays (drawn in dp over the picture by lab_ui), and present. */
void lab_gfx_present(void);
/* The window's pixels of the portrait picture (for touch): x, y, w, h and
 * its rotation (0, 90, 270). */
void lab_gfx_picture_rect(float *x, float *y, float *w, float *h, int *rot);
/* A level turned to fill the screen (portrait layout): 0, 90 (the board's
 * top to the left) or 270 (to the right); animated to. The layout's own
 * rotation (a console held upright) always wins. */
void lab_gfx_set_rotation(int rot);
void lab_gfx_set_rotation_now(int rot); /* no animation */
int lab_gfx_rotation(void);
int lab_gfx_upright(void); /* 1 upright, 0 turned, -1 turning */
int lab_gfx_level_turned(void); /* a level is shown turned (level_layout) */
/* the ghost ball (lab_ghost.c) */
void lab_ghost_frame(int eng);                         /* single player: after its frame */
void lab_ghost_mirror(int board_eng, int other_eng, int shown); /* local play: the other's ball */
int lab_game_eng_run(int eng, float *secs, int *level, char *id, size_t idcap); /* secs: the run's time */
int lab_game_eng_ball(int eng, float *x, float *y, int *live); /* live: in play (after the 3-2-1) */
/* the engine's ghost ball on that board: shown at x, y (the board's units) */
int lab_game_eng_ghost(int eng, int shown, float x, float y);
/* A direction as the player sees it (x right, y up) -> the board's (x to
 * its right, y to its top), for the turned level. */
void lab_gfx_view_to_board(float *x, float *y);
/* A screen touch (window pixels) -> dp in the portrait (0..320, 0..480, y
 * down); 0 if outside the picture. */
int lab_gfx_touch_to_dp(float sx, float sy, float *dx, float *dy);
/* Local play: two surfaces side by side (w x h each), bound one at a time
 * for their engines; the composite shows both, then the portrait surface
 * (the menus' overlay, cleared see-through) over them. */
int lab_gfx_race_on(int on, int ipad); /* 0 ok; ipad: 3:4 boards */
int lab_gfx_race_active(void);
void lab_gfx_race_begin(int eng);
int lab_gfx_race_w(void);
int lab_gfx_race_h(void);
/* where each board is on the window (pixels), for the side panels */
void lab_gfx_race_rect(int eng, float *x, float *y, float *w, float *h);
extern void (*lab_gfx_race_side_hook)(int win_w, int win_h);
/* ... and over each board, once it is drawn (window pixels) */
extern void (*lab_gfx_race_board_hook)(int eng, float x, float y, float w, float h);
/* A texture render target for thumbnails: bind (w x h px), unbind. */
unsigned lab_gfx_offscreen_begin(int w, int h);
void lab_gfx_offscreen_end(void);
/* The engine's framebuffer binds go through this (0 = the portrait surface). */
void lab_gl_bind_framebuffer(unsigned target, unsigned fb);
/* GL entry points the engine imports that the port redirects. */
extern DynLibFunction lab_gl_overrides[];
extern int lab_gl_overrides_count;
/* Side panel drawer (lab_ui.c): called during present, window pixels. */
extern void (*lab_gfx_side_hook)(int win_w, int win_h, float px, float py, float pw, float ph);

/* ------------------------------------------------------------ lab_draw.c */
/* 2D drawing in dp over the current target (the portrait surface: dp scale;
 * or the window: pixels, scale 1). GLES 1 fixed function, state saved. */
typedef struct {
  unsigned tex;
  int w, h;             /* pixels */
  float tw, th;         /* texture coordinates of the picture's corner (POT padding) */
  float scale;          /* pixels per unit (an iPad picture: 1, @2x 2); 0: an hdpi drawable (1.5) */
} LabTex;
void lab_draw_begin(float units_w, float units_h, int target_w, int target_h);
void lab_draw_begin_at(float x0, float y0, float units_w, float units_h, int target_w, int target_h);
void lab_draw_end(void);
/* Around a call into the engine from inside a frame: its GL state its own. */
int lab_draw_suspend(void);
void lab_draw_resume(int depth);
/* A picture from the APK (res/drawable-hdpi-v4/<name>.png|.jpg, or a path
 * with a '/'), cached; NULL if it can't be read. "ipa:<path without
 * extension>": the iPad game's (<path>@2x or <path>, .png or .jpg; @2x
 * first when lab_tex_prefer_2x). */
const LabTex *lab_tex(const char *name);
void lab_tex_prefer_2x(int on);
/* Its size in the units it was made for: an hdpi drawable in dp, an iPad
 * picture in points. */
float lab_tex_pt_w(const LabTex *t);
float lab_tex_pt_h(const LabTex *t);
/* A picture from RGBA pixels (not cached; lab_tex_free). */
LabTex *lab_tex_from_rgba(const uint8_t *rgba, int w, int h);
void lab_tex_free(LabTex *t);
/* Size in dp of a drawable (hdpi: px / 1.5). */
float lab_tex_dp_w(const LabTex *t);
float lab_tex_dp_h(const LabTex *t);
void lab_draw_image(const LabTex *t, float x, float y, float w, float h, uint32_t rgba);
/* Part of the texture: u0..u1, v0..v1 in 0..1 of the picture. */
/* four corners (tl, tr, bl, br) with their texture coordinates */
void lab_draw_quad_raw(const LabTex *t, const float *xy, const float *uv, uint32_t rgba, int blend);
void lab_draw_image_uv(const LabTex *t, float x, float y, float w, float h, float u0, float v0,
                       float u1, float v1, uint32_t rgba);
/* Rotated about (cx, cy) by deg, scaled: the MainMenuButtonView bars. */
void lab_draw_image_xform(const LabTex *t, const float m[6], uint32_t rgba);
void lab_draw_rect(float x, float y, float w, float h, uint32_t rgba);
void lab_draw_line(float x0, float y0, float x1, float y1, float width, uint32_t rgba);
void lab_draw_frame(float x, float y, float w, float h, float t, uint32_t rgba);
void lab_draw_rrect(float x, float y, float w, float h, float r, uint32_t rgba);
void lab_draw_ring(float x, float y, float w, float h, float r, float t, uint32_t rgba);
void lab_draw_opaque(float x, float y, float w, float h, float r); /* alpha 1 there, colours kept */
/* A focus that glows (softly, steadily), added to the picture: round about
 * the rect; or an ellipse about (cx, cy), its radii, turned deg. */
void lab_draw_glow(float x, float y, float w, float h);
void lab_draw_glow_at(float cx, float cy, float rx, float ry, float deg, float k); /* k: 1 soft */
void lab_draw_glow_behind(float x, float y, float w, float h); /* under a picture drawn next */
/* Text (dp size), left/centre/right of x; returns the width drawn. */
enum { LAB_LEFT, LAB_CENTER, LAB_RIGHT };
float lab_draw_text(float x, float y_baseline, float size, uint32_t rgba, int align, int bold,
                    const char *utf8);
float lab_text_width(float size, int bold, const char *utf8);
/* Wrapped into w; returns the height used (line = 1.3 x size). */
float lab_draw_text_box(float x, float y_top, float w, float size, uint32_t rgba, int align, int bold,
                        const char *utf8);
void lab_draw_scissor(float x, float y, float w, float h); /* w <= 0: off */
/* A PNG/JPEG in memory -> RGBA8 (malloc; stb_image). */
uint8_t *lab_image_decode(const uint8_t *data, size_t len, int *w, int *h);

/* ------------------------------------------------------------ lab_game.c */
/* A game session: GameActivity + its SurfaceView for one level pack. */
int lab_game_start(const LabPack *p);       /* at its current level */
int lab_game_active(void);
int lab_game_loading(void); /* its Loading... frame is up (the level loads next) */
int lab_game_ipad(void); /* the game is an iPad pack's (the iPad engine, its 3:4 surface) */
/* One frame: queued touches + tilt, render() into the portrait surface. */
void lab_game_frame(void);
void lab_game_show_menu(void);              /* Back / Menu: the engine's pause overlay */
/* One of the engine's popups is up (pause, level end...): 1/0, -1 unknown. */
int lab_game_popup_open(void); /* 1: the pause screen, 2: another (a level's end) */
/* The buttons on it the controller can press (dp, y down), best first. */
typedef struct {
  float x, y, w, h; /* where it is drawn */
  uintptr_t ptr;
  int visible;
  float m[6];       /* its hit test's frame: a touch at (x, y) (y up) is looked for at
                     * (m0 x + m2 y + m4, m1 x + m3 y + m5) */
} LabBtn;
int lab_game_overlay_buttons(LabBtn *out, int cap);
void lab_game_resume_after_settings(void);
void lab_game_set_tilt_view(int on); /* Settings > Tilt view, live */
void lab_game_end(void);                    /* destroy (finish or leaving) */
/* A touch in dp (y down), queued as ZTouch does: 0 pressed 1 released 2 moved 3 cancelled. */
void lab_game_touch(int action, float x_dp, float y_dp);
/* Tilt, in g (x right, y up, z out of the screen), calibrated. */
void lab_game_tilt(float x, float y, float z);
/* GameActivity's Java methods the engine calls. */
void lab_game_on_finish(void);
void lab_game_on_show_settings(void);
void lab_game_on_finished_pack(void);
/* The level pack the thumbnails are for (level info screen). */
int lab_thumbs_setup(const LabPack *p, int w, int h);
LabTex *lab_thumbs_render(int level);       /* a new texture (lab_tex_free) */
LabTex *lab_thumbs_render_px(int level, uint8_t **rgba); /* ... and its pixels (malloc, rows bottom-up) */
void lab_thumbs_release(void);
/* The engine's sizes: resize() after anything that changed them. */
void lab_game_resize(void);
/* Local play (lab_versus.c): a game in engine copy eng, drawn into its own
 * surface (lab_gfx_race_begin); render 0 keeps its last picture. */
int lab_game_eng_start(int eng, const LabPack *p);
int lab_game_eng_active(int eng);
void lab_game_eng_frame(int eng, int render);
void lab_game_eng_tilt(int eng, float x, float y, float z);
void lab_game_eng_touch(int eng, int action, float x_dp, float y_dp);
void lab_game_eng_restart(int eng, int level);
void lab_game_eng_show_menu(int eng);
int lab_game_eng_popup(int eng);   /* as lab_game_popup_open */
int lab_game_eng_finished(int eng); /* it called finish() */
void lab_game_eng_end(int eng);

/* ----------------------------------------------------------- lab_input.c */
typedef struct {
  uint64_t down, held, up;  /* HidNpadButton_* (A/B swapped when asked) */
  float lx, ly, rx, ry;     /* sticks, -1..1 (up positive) */
  int touch;                /* a finger is on the screen */
  float tx, ty;             /* ... where, in dp of the portrait (y down) */
  int touch_in;             /* ... inside the picture */
  int touch_began, touch_ended;
  float accel[3];           /* the controller's gravity (motion), g */
  int accel_ok;
} LabPad;
void lab_input_init(void);
void lab_input_poll(LabPad *out);           /* once a frame */
/* The board's tilt from the pad (stick / motion), calibrated. */
void lab_input_tilt(const LabPad *p, float out[3]);
/* Settings > Calibrate: the controller's position now is level. */
void lab_input_calibrate(void);
void lab_input_calibrate_player(int player); /* local play: that player's controller */
void lab_input_rumble(float strength, int ms);
/* Local play: player 2's controller (No. 2) and each player's rumble (-1:
 * the usual controller). */
void lab_input_poll_p2(LabPad *out);
int lab_input_p2_connected(void);
const char *lab_input_player_name(int player); /* "Pro Controller", "left Joy-Con"... */
void lab_input_stick_tilt(const LabPad *p, float out[3]); /* the stick alone */
void lab_input_player_tilt(int player, const LabPad *p, float out[3]); /* local play: stick + that player's motion */
void lab_input_rumble_player(int player, float strength, int ms);
void lab_input_local_play(int on); /* Joy-Cons held sideways, one each */

/* -------------------------------------------------------------- lab_ui.c */
/* The rebuilt Android screens. */
void lab_ui_init(void);
/* One frame of the menus (the portrait surface bound), with the pad. */
void lab_ui_frame(const LabPad *pad);
/* In a level: the pointer over the engine's overlays, drawn after it. */
void lab_ui_game_frame(const LabPad *pad);
/* Screens the game opens: Settings over a level (GameActivity.showSettings). */
void lab_ui_open_settings_over_game(void);
/* One of the game's own screens is up over the level: 1 the pause screen,
 * 2 another (a level's end...), 0 none. */
int lab_ui_game_paused(void);
/* The game session ended (finish): back to the screen it came from. */
void lab_ui_game_ended(void);
int lab_ui_quit_requested(void);

/* ---------------------------------------------------------- lab_applet.c */
/* The Switch's keyboard and browser, shown after the frame (they block). */
typedef void (*LabKbdDone)(const char *text, void *arg); /* NULL: cancelled */
void lab_kbd_request(const char *header, const char *guide, const char *initial, int maxlen, LabKbdDone cb,
                     void *arg);
int lab_web_request(const char *url);       /* 0: it will open */
int lab_controllers_request(void (*cb)(int ok)); /* the console's controller screen, 2 players */
int lab_applet_pending(void);
void lab_applets_pump(void);                /* the frame loop, after the present */
void lab_local_time(char *out, size_t cap); /* "2026-09-26 15:40" */

/* ---------------------------------------------------------- lab_versus.c */
/* Local play: two players race a level pack on two boards (engine copies 0
 * and 1), the first ball in the goal wins the level. */
int lab_versus_start(const LabPack *p); /* 0 ok (then the SCR_VERSUS screen) */
int lab_versus_active(void);
void lab_versus_end(void);
void lab_versus_frame(const LabPad *p1); /* both engines, before the menus' frame */
void lab_versus_set_paused(int on);
int lab_versus_paused(void);
/* from the race's screen: done at the start of the next race frame */
void lab_versus_restart_level(void);
void lab_versus_restart_pack(void);     /* from its first level, the wins 0 */
void lab_versus_quit(void);             /* then lab_versus_active() is 0 */
int lab_versus_pause_pressed(const LabPad *p1); /* + or - from either player */
int lab_versus_phase_over(void);        /* the pack's last level is won */
int lab_versus_zl_pressed(int player);  /* a player's ZL this frame */
void lab_versus_score(int *p1, int *p2);
int lab_versus_pack_level(const LabPack *p); /* where its next race starts */

/* ------------------------------------------------------------ lab_boot.c */
int lab_boot_run(void);
void lab_request_exit(void);
uint64_t lab_frame_count(void);

#endif
