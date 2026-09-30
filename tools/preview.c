/* tools/preview.c -- the rebuilt screens on a PC, as pictures.
 *
 * The port's own UI code (lab_ui.c, lab_screens.c, lab_draw.c, lab_font.c,
 * lab_text.c, with the real level table and registry from the APK) drawn
 * with the Mac's legacy OpenGL (tools/host/host_gl.c) into the same portrait
 * surface and window as on the Switch, then each screen written out as a
 * PNG of the whole 1280x720 screen, side panels included. The engine, sound
 * and controllers are stand-ins; a scripted pad walks through the screens.
 * Run by tools/preview.sh. MIT. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "dcr_config.h"
#include "lab.h"
#include "lab_gl.h"
#include "lab_online.h"
#include "lab_ui.h"
#include "lab_hd.h"
#include "miniz/miniz.h"

LabGL lgl;
int host_gl_init(void);
int host_gl_count(void);
void host_gl_table(void **out);

/* ------------------------------------------------------------ stubs */
static char g_root[512], g_out[512];
const char *dcr_game_root(void) { return g_root; }
void debugPrintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}
void log_console_show_text(void) {}
void log_console_update(void) {}
void log_flush_ring(void) {}
ssize_t dcr_apkcache_read(uint64_t off, void *buf, size_t n) { return -1; }

static DcrConfig g_cfg = {.tilt = LAB_TILT_STICK, .stick_tilt = 0.5f, .motion_gain = 1, .pointer_speed = 5,
                          .rumble = 1, .supersample = 2, .side_panels = 1, .res_w = 1280, .res_h = 720};
const DcrConfig *dcr_config(void) { return &g_cfg; }
void dcr_config_set_tilt(int t) { g_cfg.tilt = t; }
void dcr_config_set_level_layout(int l) { g_cfg.level_layout = l; }
int lab_gfx_upright(void) { return 1; }
void lab_gfx_view_to_board(float *x, float *y) {}

void lab_audio_click(void) {}
void lab_audio_play(int source, int id, float gain, float pitch) {}
void lab_audio_pause(int paused) {}
void dcr_time_suspend(void) {}
void dcr_time_resume(void) {}

/* lab_applet.c: the keyboard answers at once with what the script wants */
static const char *g_kbd_answer = "chax3vkf.01";
void lab_kbd_request(const char *header, const char *guide, const char *initial, int maxlen, LabKbdDone cb,
                     void *arg) {
  printf("  [keyboard \"%s\" -> \"%s\"]\n", header, g_kbd_answer);
  if (cb)
    cb(g_kbd_answer, arg);
}
int lab_web_request(const char *url) {
  printf("  [browser: %s]\n", url);
  return 0;
}
int lab_applet_pending(void) { return 0; }
void lab_applets_pump(void) {}
void lab_local_time(char *out, size_t cap) {
  time_t now = time(NULL);
  strftime(out, cap, "%Y-%m-%d %H:%M", localtime(&now));
}
void lab_online_test_account(const char *uid, const char *pin);
int lab_audio_ready(void) { return 1; }
void lab_audio_enable(int on) {}
void lab_input_calibrate(void) {}
void lab_input_calibrate_player(int player) { (void)player; }
void lab_input_tilt(const LabPad *p, float out[3]) {
  out[0] = p->lx * 0.5f, out[1] = p->ly * 0.5f, out[2] = 1;
}
void lab_game_touch(int a, float x, float y) {}
int lab_game_popup_open(void) { return 0; }
int lab_game_overlay_buttons(LabBtn *out, int cap) { return 0; }
void lab_game_show_menu(void) {}
int lab_game_start(const LabPack *p) { return -1; }
static int g_fake_game; /* the script: "a level is up" (Settings over it) */
int lab_game_active(void) { return g_fake_game; }
void lab_game_set_tilt_view(int on) { (void)on; }
void dcr_dircache_forget(void) {}
void lab_game_resume_after_settings(void) {}
int lab_thumbs_setup(const LabPack *p, int w, int h) { return -1; }
LabTex *lab_thumbs_render(int level) { return NULL; }
LabTex *lab_thumbs_render_px(int level, uint8_t **px) { return NULL; }
int lab_gfx_level_turned(void) { return 0; }
void lab_thumbs_release(void) {}

/* the Switch's shared font: a TrueType file */
static uint8_t *g_font;
static size_t g_font_len;
Result plInitialize(PlServiceType t) { return 0; }
Result plGetSharedFontByType(PlFontData *out, PlSharedFontType t) {
  if (t != PlSharedFontType_Standard || !g_font)
    return 1;
  out->address = g_font;
  out->size = (u32)g_font_len;
  return 0;
}

/* JNI, as far as the awards screen goes */
void *g_jni_env;
static JClass g_cls;
static JObj g_cls_obj;
JClass *jni_class(const char *name) {
  g_cls.obj = &g_cls_obj;
  return &g_cls;
}
const char *jni_utf(const void *s) { return s ? ((const JObj *)s)->s.utf : ""; }
void jni_release(JObj *o) {}
void jni_set_field(JObj *o, const char *name, jvalue val, int is_object) {}
static const char *const k_aw[][3] = {
    {"tutorial", "Preschool", "Complete the tutorial"},
    {"distance-01", "Around the house", "Roll the ball 75 m (82 yards)"},
    {"completed-easy-01", "Easy peasy", "Complete an easy level pack"},
    {"milestone-01", "Milestone", "Complete 50 levels"},
    {"holes-01", "Hole in one", "Fall into 100 holes"},
};
static JObj g_str[16];
static JObj *mkstr(int i, const char *s) {
  g_str[i].kind = JK_STRING;
  g_str[i].s.utf = (char *)s;
  return &g_str[i];
}
static int32_t g_ids[5] = {0, 1, 2, 3, 4};
static JObj g_arr = {.kind = JK_ARRAY};
static void *f_arr(void *e, void *c) {
  g_arr.a.len = 5;
  g_arr.a.data = g_ids;
  return &g_arr;
}
static void *f_name(void *e, void *c, jint i) { return mkstr(i % 5, k_aw[i % 5][1]); }
static void *f_desc(void *e, void *c, jint i) { return mkstr(5 + i % 5, k_aw[i % 5][2]); }
static void *f_id(void *e, void *c, jint i) { return mkstr(10 + i % 5, k_aw[i % 5][0]); }
static jfloat f_prog(void *e, void *c, jint i) { return i < 2 ? 1.0f : 0.3f * (float)i; }
static jboolean f_ach(void *e, void *c, jint i) { return i < 2; }
static jboolean f_ball(void *e, void *c, jint i) { return i == 0; }
static jint f_cnt(void *e, void *c) { return 2; }
static jint f_time(void *e, void *c) { return 4000; }
static jfloat f_dist(void *e, void *c) { return 1234.5f; }
void *lab_native(const char *s) {
  static const struct { const char *n; void *f; } t[] = {
      {"AwardManager_getAwardArray", (void *)f_arr}, {"AwardManager_getAwardName", (void *)f_name},
      {"AwardManager_getAwardDesc", (void *)f_desc}, {"AwardManager_getAwardId", (void *)f_id},
      {"AwardManager_getAwardProgress", (void *)f_prog}, {"AwardManager_getAwardAchieved", (void *)f_ach},
      {"AwardManager_isBallUnlocked", (void *)f_ball}, {"StatisticsManager_getNbrAwards", (void *)f_cnt},
      {"StatisticsManager_getTimePlayed", (void *)f_time}, {"StatisticsManager_getBallDistance", (void *)f_dist}};
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
    if (strstr(s, t[i].n))
      return t[i].f;
  return NULL;
}

/* ------------------------------------------------------------ lab_gfx.c's part */
static GLuint g_fbo, g_tex, g_win_fbo, g_win_tex;
static const int SW = 960, SH = 1440, WW = 1280, WH = 720;
static float g_px = 400, g_py = 0, g_pw = 480, g_ph = 720;
void (*lab_gfx_side_hook)(int, int, float, float, float, float);
int lab_gfx_surface_w(void) { return SW; }
int lab_gfx_surface_h(void) { return SH; }
float lab_gfx_px_per_dp(void) { return (float)SW / 320.0f; }
void lab_gfx_begin_portrait(void) {
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_fbo);
  lgl.Viewport(0, 0, SW, SH);
}
void lab_gfx_picture_rect(float *x, float *y, float *w, float *h, int *rot) {
  *x = g_px, *y = g_py, *w = g_pw, *h = g_ph, *rot = 0;
}
int lab_gfx_touch_to_dp(float sx, float sy, float *dx, float *dy) { return 0; }
unsigned lab_gfx_offscreen_begin(int w, int h) { return 0; }
void lab_gfx_offscreen_end(void) {}

/* local play (lab_versus.c): the two boards' surfaces, the engines stand-ins
 * that draw a board with its ball where the tilt rolls it */
static GLuint g_rfbo[2], g_rtex[2];
static int g_race, g_stub_win = -1, g_race_made;
static float g_ball[2][2] = {{160, 300}, {160, 300}}, g_etilt[2][2];
int g_lab_race, g_lab_race_level, g_lab_eng, g_lab_race_p1;
static int g_race_ipad, g_rw = 480, g_rh = 720;
void (*lab_gfx_race_side_hook)(int, int);
void (*lab_gfx_race_board_hook)(int, float, float, float, float);
static void target(GLuint *fbo, GLuint *tex, int w, int h);
int lab_gfx_race_on(int on, int ipad) {
  if (on && g_race_made && g_race_ipad != ipad) {
    for (int i = 0; i < 2; i++) {
      lgl.DeleteFramebuffersOES(1, &g_rfbo[i]);
      lgl.DeleteTextures(1, &g_rtex[i]);
    }
    g_race_made = 0;
  }
  if (on && !g_race_made++) {
    g_race_ipad = ipad;
    g_rh = ipad ? 648 : 720, g_rw = ipad ? 486 : 480;
    for (int i = 0; i < 2; i++)
      target(&g_rfbo[i], &g_rtex[i], g_rw, g_rh);
  }
  g_race = on;
  lab_gfx_begin_portrait();
  return 0;
}
int lab_gfx_race_active(void) { return g_race; }
int lab_gfx_race_w(void) { return g_rw; }
int lab_gfx_race_h(void) { return g_rh; }
void lab_gfx_race_begin(int eng) {
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_rfbo[eng & 1]);
  lgl.Viewport(0, 0, g_rw, g_rh);
}
void lab_gfx_race_rect(int eng, float *x, float *y, float *w, float *h) {
  float gap = 20, x0 = (1280 - (2.0f * (float)g_rw + gap)) * 0.5f;
  *x = x0 + ((eng & 1) ? (float)g_rw + gap : 0), *y = (720 - (float)g_rh) * 0.5f, *w = (float)g_rw, *h = (float)g_rh;
}
int lab_load_second_engine(void) { return 0; }
int lab_load_ipad_race_engine(void) { return 0; }
int lab_game_eng_start(int eng, const LabPack *p) { return 0; }
void lab_game_eng_end(int eng) {}
void lab_game_eng_restart(int eng, int level) { g_ball[eng & 1][0] = 160, g_ball[eng & 1][1] = 300; }
void lab_game_eng_tilt(int eng, float x, float y, float z) { g_etilt[eng & 1][0] = x, g_etilt[eng & 1][1] = y; }
int lab_game_eng_popup(int eng) { return eng == g_stub_win ? 2 : 0; }
void lab_game_eng_frame(int eng, int render) {
  eng &= 1;
  g_ball[eng][0] += g_etilt[eng][0] * 4, g_ball[eng][1] -= g_etilt[eng][1] * 4;
  if (!render)
    return;
  lab_gfx_race_begin(eng);
  float uw = g_race_ipad ? 360 : 320; /* an iPad board: 3:4 */
  lab_draw_begin(uw, 480, g_rw, g_rh);
  lab_draw_rect(0, 0, uw, 480, 0x6b4a2effu);
  lab_draw_rect(12, 12, uw - 24, 456, 0xd8c09affu);
  for (int k = 0; k < 5; k++)
    lab_draw_rect(12 + 50.0f * (float)k, 90.0f + 70.0f * (float)k, 150, 10, 0x8a6a44ffu);
  lab_draw_rrect(240, 400, 36, 36, 18, 0x303030ffu);
  lab_draw_rrect(g_ball[eng][0] - 11, g_ball[eng][1] - 11, 22, 22, 11, 0xc8c8d0ffu);
  lab_draw_end();
}
void lab_input_local_play(int on) { printf("  [local play %s]\n", on ? "on" : "off"); }
void lab_input_poll_p2(LabPad *out) { memset(out, 0, sizeof *out); }
int lab_input_p2_connected(void) { return 0; }
const char *lab_input_player_name(int player) { return player ? NULL : "Pro Controller"; }
void lab_input_player_tilt(int player, const LabPad *p, float out[3]) {
  out[0] = p->lx * 0.5f, out[1] = p->ly * 0.5f, out[2] = 1;
}
void lab_input_stick_tilt(const LabPad *p, float out[3]) {
  out[0] = p->lx * 0.5f, out[1] = p->ly * 0.5f, out[2] = 1;
}
void lab_input_rumble_player(int player, float strength, int ms) {}
void lab_audio_stop_all(void) {}
int lab_controllers_request(void (*cb)(int ok)) {
  printf("  [the controller screen: 2 players]\n");
  return 0;
}

/* the iPad menus' surface (lab_gfx.c's) */
static GLuint g_hfbo, g_htex;
static int g_hshow;
int lab_gfx_hd_w(void) { return WW; }
int lab_gfx_hd_h(void) { return WH; }
static float g_hch = 640;
void lab_gfx_set_hd_canvas_h(float h) { g_hch = h; }
float lab_gfx_hd_canvas_h(void) { return g_hch; }
float lab_gfx_hd_canvas_w(void) { return g_hch * (float)WW / (float)WH; }
void lab_gfx_begin_hd(void) {
  if (!g_hfbo)
    target(&g_hfbo, &g_htex, WW, WH);
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_hfbo);
  lgl.Viewport(0, 0, WW, WH);
}
void lab_gfx_show_hd(int on) { g_hshow = on && g_hfbo; }
void lab_gfx_show_hd_over(int on) { (void)on; }
int lab_gfx_showing_hd(void) { return g_hshow; }
void lab_gfx_touch_to_canvas(float sx, float sy, float *x, float *y) {
  *x = sx / 1280.0f * lab_gfx_hd_canvas_w();
  *y = sy / 720.0f * g_hch;
}
void lab_gfx_begin_ipad(void) { lab_gfx_begin_portrait(); }
int lab_gfx_ipad_w(void) { return SW; }
int lab_gfx_ipad_h(void) { return SH; }
void lab_gfx_show_ipad(int on) {}
int lab_gfx_showing_ipad(void) { return 0; }
int lab_game_ipad(void) { return 0; }
int lab_load_ipad_engine(void) { return -1; }

static void target(GLuint *fbo, GLuint *tex, int w, int h) {
  lgl.GenTextures(1, tex);
  lgl.BindTexture(GL_TEXTURE_2D, *tex);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  lgl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  lgl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  lgl.GenFramebuffersOES(1, fbo);
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, *fbo);
  lgl.FramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, *tex, 0);
  if (lgl.CheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) != GL_FRAMEBUFFER_COMPLETE_OES)
    printf("framebuffer incomplete\n");
}

static void composite(void) {
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_win_fbo);
  lab_draw_begin(WW, WH, WW, WH);
  lgl.ClearColor(0, 0, 0, 1);
  lgl.Clear(GL_COLOR_BUFFER_BIT);
  if (g_hshow && !g_race) {
    const float xy[8] = {0, 0, WW, 0, 0, WH, WW, WH};
    const float uv[8] = {0, 1, 1, 1, 0, 0, 1, 0};
    LabTex t = {g_htex, WW, WH, 1, 1};
    lab_draw_quad_raw(&t, xy, uv, 0xffffffffu, 0);
    lab_draw_end();
    lab_gfx_begin_portrait();
    return;
  }
  if (g_race) {
    /* as lab_gfx.c's race_composite */
    if (lab_gfx_race_side_hook)
      lab_gfx_race_side_hook(WW, WH);
    for (int i = 0; i < 2; i++) {
      float x, y, w, h;
      lab_gfx_race_rect(i, &x, &y, &w, &h);
      const float xy[8] = {x, y, x + w, y, x, y + h, x + w, y + h};
      const float uv[8] = {0, 1, 1, 1, 0, 0, 1, 0};
      LabTex t = {g_rtex[i], 480, 720, 1, 1};
      lab_draw_quad_raw(&t, xy, uv, 0xffffffffu, 0);
      if (lab_gfx_race_board_hook)
        lab_gfx_race_board_hook(i, x, y, w, h);
    }
    if (g_hshow) {
      const float xy[8] = {0, 0, WW, 0, 0, WH, WW, WH};
      const float uv[8] = {0, 1, 1, 1, 0, 0, 1, 0};
      LabTex t = {g_htex, WW, WH, 1, 1};
      lab_draw_quad_raw(&t, xy, uv, 0xffffffffu, 1);
    } else {
      const float xy[8] = {400, 0, 880, 0, 400, 720, 880, 720};
      const float uv[8] = {0, 1, 1, 1, 0, 0, 1, 0};
      LabTex t = {g_tex, SW, SH, 1, 1};
      lab_draw_quad_raw(&t, xy, uv, 0xffffffffu, 1);
    }
    lab_draw_end();
    lab_gfx_begin_portrait();
    return;
  }
  if (lab_gfx_side_hook)
    lab_gfx_side_hook(WW, WH, g_px, g_py, g_pw, g_ph);
  const float xy[8] = {g_px, g_py, g_px + g_pw, g_py, g_px, g_py + g_ph, g_px + g_pw, g_py + g_ph};
  const float uv[8] = {0, 1, 1, 1, 0, 0, 1, 0};
  LabTex t = {g_tex, SW, SH, 1, 1};
  lab_draw_quad_raw(&t, xy, uv, 0xffffffffu, 0);
  lab_draw_end();
  lab_gfx_begin_portrait(); /* as lab_gfx_present does after the swap */
}

static void save(const char *name) {
  static uint8_t px[1280 * 720 * 4];
  lgl.BindFramebufferOES(GL_FRAMEBUFFER_OES, g_win_fbo);
  lgl.ReadPixels(0, 0, WW, WH, GL_RGBA, GL_UNSIGNED_BYTE, px);
  size_t len = 0;
  void *png = tdefl_write_image_to_png_file_in_memory_ex(px, WW, WH, 4, &len, 6, 1);
  char p[700];
  snprintf(p, sizeof p, "%s/%s.png", g_out, name);
  FILE *f = fopen(p, "wb");
  if (f) {
    fwrite(png, 1, len, f);
    fclose(f);
  }
  free(png);
  printf("wrote %s\n", p);
}

/* ------------------------------------------------------------ the script */
static LabPad pad;
static void frame(uint64_t down) {
  memset(&pad, 0, sizeof pad);
  pad.down = down;
  pad.held = down;
  lab_online_poll();
  if (lab_versus_active() && ui_top() == SCR_VERSUS)
    lab_versus_frame(&pad);
  lab_ui_frame(&pad);
  lab_gfx_show_hd(lab_hd_on() && ui_top() != SCR_GAME);
  if (down)
    printf("  [pad %llx] screen %d focus %d\n", (unsigned long long)down, ui_top(), ui_focus_id());
  composite();
  usleep(16000);
}
static void frames(int n) {
  for (int i = 0; i < n; i++)
    frame(0);
}
/* until the level server has answered (10 s at most) */
static void wait_online(void) {
  frames(2); /* the screen asks in its first frames */
  for (int i = 0; i < 600 && lab_online_busy(); i++)
    frame(0);
  frames(3);
  if (lab_hd_on() && ui_dialog_open()) {
    /* (the preview has no account on the iPad server: its /get fails) */
    save("hd_dialog");
    frame(HidNpadButton_A);
    frames(2);
  }
}

/* ------------------------------------------------------------ the iPad menus */
static void tap(float x, float y) {
  /* a touch at canvas points (x, y): down, then up */
  memset(&pad, 0, sizeof pad);
  float sx = x / lab_gfx_hd_canvas_w() * 1280.0f, sy = y / g_hch * 720.0f;
  pad.touch = 1, pad.touch_in = 1, pad.touch_began = 1, pad.tx = x, pad.ty = y;
  (void)sx, (void)sy;
  lab_online_poll();
  lab_ui_frame(&pad);
  composite();
  memset(&pad, 0, sizeof pad);
  pad.touch_ended = 1, pad.tx = x, pad.ty = y, pad.touch_in = 1;
  lab_online_poll();
  lab_ui_frame(&pad);
  composite();
}

static int hd_script(void) {
  frame(0);
  save("hd01_splash");
  for (int i = 0; i < 200 && ui_top() == SCR_SPLASH; i++)
    frame(0);
  frames(8);
  save("hd02_main_sliding");
  frames(40);
  save("hd03_main");
  frame(HidNpadButton_Down);
  frames(3);
  save("hd04_main_focus");
  frame(HidNpadButton_Plus); /* Settings */
  frames(3);
  save("hd05_settings");
  frame(HidNpadButton_B);
  frames(2);
  ui_focus(3); /* the ball */
  frame(HidNpadButton_A);
  frames(3);
  save("hd06_awards");
  frame(HidNpadButton_B);
  frames(2);
  ui_focus(100); /* Single player */
  frame(0);
  frame(HidNpadButton_A);
  frames(4);
  save("hd07_single_ipad");
  frame(HidNpadButton_Down); /* the ring rests on a row: it is chosen */
  frames(20);
  frame(HidNpadButton_Right); /* its Play */
  frames(3);
  printf("  right from a pack: focus %d (601: Play)\n", ui_focus_id());
  frame(HidNpadButton_Up); /* its pictures */
  frames(20);
  printf("  up from Play: focus %d (600: the pictures)\n", ui_focus_id());
  save("hd08_single_chosen");
  frame(HidNpadButton_B); /* back to the row */
  frames(3);
  frame(HidNpadButton_ZR); /* iPhone levels */
  frames(3);
  save("hd09_single_iphone");
  { /* a long name: on two lines, clear of the times */
    frames(20); /* the ring's pack chosen */
    const LabPack *ip = hd_info_pack();
    LabPack *row = ip ? lab_levels_find(ip->id) : NULL;
    printf("  long name on %s (%s)\n", ip ? ip->id : "-", row ? row->name : "no row");
    if (row) {
      char was[sizeof row->name];
      memcpy(was, row->name, sizeof was);
      snprintf(row->name, sizeof row->name, "%s", "Clean and Classic: the Incredibly Long and Winding Journey Through the Enchanted Marble Forest of Doom");
      frames(2);
      save("hd09b_long_name");
      /* emoji (the iPhone's SoftBank codes and Unicode) in the list and the info */
      snprintf(row->name, sizeof row->name, "%s", "Clean \xee\x80\x95 and \xf0\x9f\x98\x80 Classic \xf0\x9f\x8e\xb1");
      frames(2);
      save("hd09c_emoji_name");
      memcpy(row->name, was, sizeof was);
    }
  }
  frame(HidNpadButton_R); /* Downloaded */
  frames(3);
  save("hd10_single_downloaded");
  frame(HidNpadButton_B);
  frames(10);
  ui_focus(101); /* Multi player */
  frame(0);
  frame(HidNpadButton_A);
  frames(3);
  save("hd11_multi");
  frame(HidNpadButton_Y); /* play: the popup */
  frames(3);
  save("hd12_multi_popup");
  frame(HidNpadButton_L);
  frames(2);
  save("hd13_multi_popup_bt");
  frame(HidNpadButton_B);
  frames(2);
  frame(HidNpadButton_B);
  frames(10);
  ui_focus(102); /* Download levels */
  frame(0);
  frame(HidNpadButton_A);
  wait_online();
  save("hd14_download_ipad");
  frame(HidNpadButton_Up); /* from the tabs under the list: its last row that shows */
  frames(3);
  frame(HidNpadButton_Up);
  frames(35); /* the ring rests on a pack: its info */
  frame(HidNpadButton_A); /* its Download */
  wait_online();
  frames(5);
  save("hd15_download_chosen");
  frame(HidNpadButton_ZR); /* iPhone levels */
  wait_online();
  save("hd16_download_iphone");
  frame(HidNpadButton_Plus); /* Top 25 */
  wait_online();
  save("hd17_download_top");
  frame(HidNpadButton_B);
  frames(10);
  ui_focus(2); /* Create */
  frame(0);
  frame(HidNpadButton_A);
  wait_online();
  save("hd18_create");
  frame(HidNpadButton_Plus); /* New: the how-to */
  frames(3);
  save("hd19_howto");
  frame(HidNpadButton_B);
  frames(2);
  frame(HidNpadButton_B);
  frames(10);
  frame(HidNpadButton_B); /* quit? */
  frames(3);
  save("hd20_quit");
  frame(HidNpadButton_B);
  frames(3);
  /* Settings over a level: the panel alone (the level's picture under it on
   * a Switch), no Credits */
  g_fake_game = 1;
  ui_push(SCR_SETTINGS);
  frames(3);
  save("hd21_settings_over_level");
  frame(HidNpadButton_B);
  frames(2);
  printf("  B in Settings over a level: screen %d (the game's is %d)\n", ui_top(), SCR_GAME);
  g_fake_game = 0;
  /* local play on iPad boards */
  for (int i = 0; i < lab_levels_count(); i++) {
    LabPack *k = lab_levels_at(i);
    if (!k->ipad || k->tutorial)
      continue;
    if (lab_versus_start(k) == 0) {
      ui_push(SCR_VERSUS);
      frames(60);
      save("hd22_race_ipad_countdown");
      for (int f = 0; f < 200; f++)
        frame(0);
      g_stub_win = 3;
      frames(20);
      save("hd23_race_ipad_result");
      g_stub_win = -1;
      lab_versus_quit();
      frames(4);
    }
    break;
  }
  (void)tap;
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: preview game.apk root outdir [font.ttf]\n");
    return 2;
  }
  snprintf(g_root, sizeof g_root, "%s", argv[2]);
  snprintf(g_out, sizeof g_out, "%s", argv[3]);
  mkdir(g_out, 0777);
  const char *fontp = argc > 4 ? argv[4] : "/System/Library/Fonts/Supplemental/Arial.ttf";
  FILE *f = fopen(fontp, "rb");
  if (f) {
    fseek(f, 0, SEEK_END);
    g_font_len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    g_font = malloc(g_font_len);
    if (fread(g_font, 1, g_font_len, f) != g_font_len)
      g_font_len = 0;
    fclose(f);
  }
  if (host_gl_init() != 0) {
    fprintf(stderr, "no OpenGL\n");
    return 1;
  }
  if (host_gl_count() != (int)(sizeof(LabGL) / sizeof(void *))) {
    fprintf(stderr, "host_gl.c's table (%d) does not match lab_gl.h (%d)\n", host_gl_count(),
            (int)(sizeof(LabGL) / sizeof(void *)));
    return 1;
  }
  host_gl_table((void **)&lgl);
  lab_apk_init(argv[1]);
  const char *ipa = getenv("IPA");
  if (ipa && ipa[0])
    lab_ipa_init(ipa);
  lab_reg_load();
  lab_levels_load();
  lab_files_setup();
  /* some progress, so the list has every group */
  LabPack *a = lab_levels_find("Z0000000.01");
  if (a)
    a->nfinished = 4, a->current = 4;
  LabPack *b = lab_levels_find("Z0000000.19");
  if (b)
    b->nfinished = b->nlevels;
  /* a downloaded pack, and two of "your own" (made-up: copies of official
   * ones), for the Downloaded tab, Create and the info screen's buttons */
  int seeded = 0;
  for (int i = 0; i < lab_levels_count() && seeded < 3; i++) {
    LabPack *k = lab_levels_at(i);
    if (k->tutorial || k == a || k == b)
      continue;
    k->preloaded = 0;
    if (seeded == 0) {
      snprintf(k->author, sizeof k->author, "Some player");
      k->rating = 3.6;
    } else {
      k->ownlevel = 1;
      k->published = seeded == 2;
      k->nfinished = k->nlevels;
      snprintf(k->name, sizeof k->name, seeded == 1 ? "My first pack" : "Marble madness");
      snprintf(k->author, sizeof k->author, "PREVIEW0");
    }
    seeded++;
  }
  lab_online_test_account("PREVIEW0", "1234");
  target(&g_fbo, &g_tex, SW, SH);
  target(&g_win_fbo, &g_win_tex, WW, WH);
  lab_gfx_begin_portrait();
  lab_ui_init();
  if (lab_hd_on())
    return hd_script();

  frame(0);
  save("01_splash");
  for (int i = 0; i < 200 && ui_top() == SCR_SPLASH; i++)
    frame(0);
  frames(4);
  save("02_main_sliding");
  frames(45);
  save("03_main");
  frame(HidNpadButton_Down); /* the ring comes back / moves */
  frames(2);
  save("04_main_focus");
  frame(HidNpadButton_Up);
  frames(2);
  frame(HidNpadButton_A); /* Play game */
  frames(3);
  save("05_packs");
  frame(HidNpadButton_Down);
  frame(0);
  frame(HidNpadButton_Down);
  frames(2);
  save("06_packs_focus");
  frame(HidNpadButton_R);
  frames(2);
  save("07_packs_downloaded");
  frame(HidNpadButton_L);
  frames(2);
  frame(HidNpadButton_X); /* the focused pack's info */
  frames(3);
  save("08_info");
  frame(HidNpadButton_Right);
  frames(2);
  save("09_info_next");
  frame(HidNpadButton_B);
  frames(2);
  frame(HidNpadButton_B);
  frames(40);
  frame(HidNpadButton_Up);
  frame(0);
  frame(HidNpadButton_Up);
  frames(2);
  save("10_main_settings_focus");
  frame(HidNpadButton_A);
  frames(3);
  save("11_settings");
  frame(HidNpadButton_Down);
  frame(0);
  frame(HidNpadButton_Down);
  frame(0);
  frame(HidNpadButton_Down);
  frame(0);
  frame(HidNpadButton_Down);
  frame(0);
  frame(HidNpadButton_Down);
  frames(2);
  frame(HidNpadButton_A); /* Credits */
  frames(3);
  save("12_credits");
  frame(HidNpadButton_B);
  frames(2);
  frame(HidNpadButton_B);
  frames(40);
  /* to the ball (Awards): down from the last bar */
  for (int i = 0; i < 8 && ui_focus_id() != 2; i++) {
    frame(HidNpadButton_Down);
    frame(0);
  }
  frames(2);
  save("13_main_awards_focus");
  frame(HidNpadButton_A);
  frames(3);
  save("14_awards");
  frame(HidNpadButton_B);
  frames(40);
  frame(HidNpadButton_Up); /* the ball -> Download levels */
  frame(0);
  frame(HidNpadButton_A);
  wait_online();
  save("15_download_all");
  frame(HidNpadButton_ZR); /* All levels: medium */
  wait_online();
  save("16_download_medium");
  frame(HidNpadButton_Down);
  frame(0);
  frame(HidNpadButton_Down);
  frames(2);
  save("17_download_focus");
  /* down to "Get 25 more", pressed: the focus goes on to the first new row */
  for (int i = 0; i < 30 && ui_focus_id() != 90; i++) {
    frame(HidNpadButton_Down);
    frame(0);
  }
  frame(HidNpadButton_A);
  wait_online();
  printf("  after Get 25 more: focus %d (want 1025)\n", ui_focus_id());
  save("17b_download_more");
  /* the tabs along the bottom, sideways with the controller (list rows
   * scrolled out of sight below must not take the focus) */
  ui_focus(31);
  frame(0);
  frame(HidNpadButton_Right);
  printf("  tabs: All levels -> right -> %d (want 32)\n", ui_focus_id());
  frame(0);
  frame(HidNpadButton_Left);
  printf("  tabs: Top 25 -> left -> %d (want 31)\n", ui_focus_id());
  frame(0);
  frame(HidNpadButton_L); /* New */
  wait_online();
  save("18_download_new");
  frame(HidNpadButton_R);
  frame(0);
  frame(HidNpadButton_R); /* Top 25 */
  wait_online();
  save("19_download_top");
  frame(HidNpadButton_R); /* By ID */
  frames(3);
  save("20_download_id");
  frame(HidNpadButton_Y); /* the keyboard (answers chax3vkf.01) */
  wait_online();
  save("21_download_search");
  frame(HidNpadButton_B);
  frames(40);
  frame(HidNpadButton_Up); /* -> Create */
  frame(0);
  frame(HidNpadButton_A);
  wait_online();
  save("22_create");
  frame(HidNpadButton_X); /* the unpublished pack's info: Publish + Play */
  frames(3);
  save("23_info_own");
  frame(HidNpadButton_B);
  frames(2);
  frame(HidNpadButton_Plus); /* New: the how-to */
  frames(3);
  save("24_howto_1");
  frame(HidNpadButton_Up);
  frame(0);
  frame(HidNpadButton_A); /* QR code */
  frames(3);
  save("25_howto_qr");
  frame(HidNpadButton_B);
  frames(2);
  frame(HidNpadButton_R);
  frames(3);
  save("26_howto_2");
  frame(HidNpadButton_R);
  frames(3);
  save("27_howto_3");
  frame(HidNpadButton_B);
  frames(2);
  frame(HidNpadButton_B);
  frames(40);
  /* the downloaded pack's info: Delete + Play */
  ui_focus(100); /* Play game */
  frame(0);
  frame(HidNpadButton_A);
  frames(3);
  frame(HidNpadButton_R);
  frames(2);
  frame(HidNpadButton_X);
  frames(3);
  save("28_info_downloaded");
  frame(HidNpadButton_B);
  frames(2);
  frame(HidNpadButton_B);
  frames(40);
  /* emoji in the menus' text (a font as EMOJI_FONT): the iPhone's old
   * SoftBank code points, Unicode ones, flags and keycaps */
  {
    lab_gfx_begin_portrait();
    lab_draw_begin(320, 480, SW, SH);
    ui_background("bg_light_blue");
    static const char *const lines[] = {
        "\xee\x84\xb2r@<e w!Th fr!en<ls\xee\x85\x83",
        "Sun \xee\x81\x8a smile \xee\x90\x95 Japan \xee\x94\x8b",
        "Keycap \xee\x88\x90 and 1\xef\xb8\x8f\xe2\x83\xa3, flag \xf0\x9f\x87\xba\xf0\x9f\x87\xb8",
        "Unicode \xf0\x9f\x98\x80 \xe2\x9d\xa4\xef\xb8\x8f \xe2\x98\x80 plain \xe2\x86\x92 A&B",
    };
    float y = 40;
    const float sizes[3] = {10, 16, 24};
    for (int s = 0; s < 3; s++)
      for (unsigned i = 0; i < sizeof lines / sizeof lines[0]; i++, y += sizes[s] * 1.6f)
        lab_draw_text(12, y + sizes[s], sizes[s], 0x000000ffu, LAB_LEFT, 1, lines[i]);
    lab_draw_text(12, y + 30, 16, 0x000000ffu, LAB_LEFT, 1, lab_emoji_ready() ? "(an emoji font)" : "(no emoji font)");
    lab_draw_end();
    composite();
    save("30_emoji");
  }
  /* the glow on a button of the game's own screens (a mock: a slanted band
   * and white icons, as the pause screen), at its dimmest and brightest */
  for (int k = 0; k < 2; k++) {
    lab_gfx_begin_portrait();
    lab_draw_begin(320, 480, SW, SH);
    ui_background("bg_light_blue");
    const float m[6] = {0.8192f, 0.5736f, -40, -0.5736f, 0.8192f, 330};
    const LabTex *band = lab_tex("bar2");
    if (band)
      lab_draw_image_xform(band, m, 0xc8e650ffu);
    const char *ic[3] = {"icon_refresh", "settings", "icon_new"};
    for (int i = 0; i < 3; i++)
      scr_pic(ic[i], 60.0f + 80.0f * (float)i, 250.0f - 55.0f * (float)i, 0xffffffffu);
    if (k)
      lab_draw_glow(140, 195, 45, 44);
    lab_draw_end();
    composite();
    save(k ? "32_glow_bright" : "31_glow_dim");
  }
  /* Multi player: the list, the popup (Local, then Bluetooth), a race */
  ui_focus(101);
  frame(0);
  frames(2);
  save("33_main_multiplayer_focus");
  frame(HidNpadButton_A);
  frames(3);
  save("34_mp_packs");
  frame(HidNpadButton_A); /* the first pack */
  frames(2);
  save("35_mp_popup_local");
  frame(HidNpadButton_L);
  frames(2);
  save("36_mp_popup_bluetooth");
  frame(HidNpadButton_R);
  frames(2);
  frame(HidNpadButton_B); /* the popup closes */
  frames(2);
  {
    LabPack *k = lab_levels_count() ? lab_levels_at(1) : NULL;
    if (k && lab_versus_start(k) == 0) {
      ui_push(SCR_VERSUS);
      frames(60);
      save("37_race_countdown");
      for (int i = 0; i < 200; i++)
        frame(0);
      save("38_race_play");
      g_stub_win = 0;
      frames(20);
      save("39_race_result");
      g_stub_win = -1;
      frame(HidNpadButton_Plus);
      frames(2);
      save("40_race_paused");
      for (int d = 0; d < 4; d++) { /* past Calibrate player 1 / 2, Restart level */
        frame(HidNpadButton_Down);
        frame(0);
      }
      frame(HidNpadButton_A); /* Quit */
      frames(3);
      printf("  after Quit: screen %d, race %d\n", ui_top(), lab_versus_active());
    }
  }
  frame(HidNpadButton_B);
  frames(40);
  frame(HidNpadButton_B); /* quit? */
  frames(3);
  save("29_quit");
  return 0;
}
