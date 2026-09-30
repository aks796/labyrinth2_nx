/* lab_game.c -- a game: GameActivity and its SurfaceView, for one level pack.
 *
 * GameActivity.onCreate: init(activity) (the engine caches finish,
 * showSettings, showAdPopup, finishedLevelPack), a SurfaceView (a new ZFont
 * -> ZFont.init; its ZTouch), setLevelPack (view state 2). Its GL thread:
 *   onSurfaceCreated   SurfaceView.init(levelPack, 2, 0)  (loads the pack at
 *                      its current level)
 *   onSurfaceChanged   resize(w, h)
 *   onDrawFrame        FileSystemUtil.initDumpBufferForThisThread, the
 *                      queued touches (ZTouch.Pressed/Released/Moved/
 *                      Cancelled, 320x480 with y up), render()
 * The accelerometer's events reach Accelerometer.onEvent(float[3]) from the
 * sensor thread; here once a frame, before the touches.
 * Back / Menu = showMenu() (the engine's pause overlay; again = resume).
 * The engine calls finish() (from inside render) to leave: stopAllSounds,
 * then onPause (SurfaceView.destroy, statistics saved) and onDestroy
 * (GameActivity.destroy); the port does that after render returns.
 * showSettings(): Android opened the Settings activity, which paused the
 * game activity -- its surface was destroyed, and made again on the way
 * back, so the level started over with the new settings. The same here.
 *
 * LOCAL PLAY runs a second copy of the engine (lab_loader.c: the library
 * mapped twice, each with its own globals) beside the first, each its own
 * GameActivity and SurfaceView drawn into its own surface (lab_gfx.c), each
 * fed its own player's tilt (lab_versus.c). Every call into a copy is made
 * with g_lab_eng set to it, so the Java it calls back (lab_java.c) knows
 * which player it is: an Eng below per copy; the single-player game is
 * engine 0.
 *
 * Thumbnails (the level info screen): ThumbnailManager.setupThumbnails(pack,
 * w, h) loads the pack for drawing, renderThumbnail(i) draws level i into
 * the current framebuffer (Android: a pbuffer; here an FBO's texture),
 * release() frees it. setupThumbnails changes the engine's screen size, so a
 * game always gets its resize again. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "lab.h"
#include "lab_online.h"
#include "lab_gl.h"
#include "nx_init.h"
#include "util.h"
#include <math.h>

#define NS "Java_se_illusionlabs_"

typedef void (*fn_v)(void *env, void *thiz);
typedef void (*fn_o)(void *env, void *thiz, void *o);
typedef void (*fn_vo)(void *env, void *thiz, void *o);
typedef void (*fn_oii)(void *env, void *thiz, void *o, jint a, jint b);
typedef void (*fn_i)(void *env, void *thiz, jint a);
typedef void (*fn_ii)(void *env, void *thiz, jint a, jint b);
typedef void (*fn_ff)(void *env, void *thiz, jfloat x, jfloat y);

typedef struct {
  fn_vo act_init;         /* GameActivity.init(Activity) */
  fn_v act_destroy, act_show_menu;
  fn_oii sv_init;         /* SurfaceView.init(LevelPack, int, int) */
  fn_v sv_destroy, sv_render;
  fn_ii sv_resize;
  fn_v font_init, font_release;
  fn_ff touch[4];         /* Pressed, Released, Moved, Cancelled */
  fn_o accel_event;
  fn_v dump_init;
  fn_v stat_save, stat_enable, stat_disable;
  fn_oii th_setup;
  fn_i th_render;
  fn_v th_release;
  int ok;
} Natives;

#define MAXQ 64
typedef struct {
  int idx;                      /* 0, 1 (local play's second), 2 (iPad boards): the copy */
  Natives nat;
  JObj *sv, *font, *pack_obj, *accel_arr, *activity;
  LabPack pack;                 /* the row it was started from (a copy) */
  int active;                   /* the activity exists */
  int surface;                  /* SurfaceView.init done (ea.a) */
  int pending_init;             /* the surface to make on the next frame (Loading... first) */
  int race;                     /* local play: its own surface, its level given */
  volatile int finish, settings, finished_pack;
  int orient, last_popup;       /* the overlays' way up as set (0: not yet), the popup it was set for */
  int orient_set;               /* setOrientation was called (its matrix is the manager's) */
  float tm[6];                  /* the frame the touch now down is sent in (lab_game_eng_touch) */
  float tilt[3];
  struct {
    int action;
    float x, y;
  } q[MAXQ];
  int nq;
} Eng;

static Eng E[4] = {{.idx = 0, .tilt = {0, 0, 1}, .tm = {1, 0, 0, 1, 0, 0}},
                   {.idx = 1, .tilt = {0, 0, 1}, .tm = {1, 0, 0, 1, 0, 0}},
                   {.idx = 2, .tilt = {0, 0, 1}, .tm = {1, 0, 0, 1, 0, 0}},
                   {.idx = 3, .tilt = {0, 0, 1}, .tm = {1, 0, 0, 1, 0, 0}}};
static int g_sp;  /* the single-player game's copy: 0, or 2 for an iPad pack */
static int g_th;  /* the thumbnails' copy (the same) */
static JObj *g_fsu_cls, *g_stat_cls;
int g_lab_eng; /* the copy being called into (lab_java.c reads it) */

static void *need(int eng, const char *sym) {
  void *p = lab_native_in(eng, sym);
  if (!p)
    debugPrintf("[game] MISSING native %s (engine %d)\n", sym, eng);
  return p;
}

static void resolve_eng(Eng *e) {
  Natives *N = &e->nat;
  if (N->ok)
    return;
  int i = e->idx;
  N->act_init = need(i, NS "labyrinth2_activities_GameActivity_init");
  N->act_destroy = need(i, NS "labyrinth2_activities_GameActivity_destroy");
  N->act_show_menu = need(i, NS "labyrinth2_activities_GameActivity_showMenu");
  N->sv_init = need(i, NS "labyrinth2_views_SurfaceView_init");
  N->sv_destroy = need(i, NS "labyrinth2_views_SurfaceView_destroy");
  N->sv_render = need(i, NS "labyrinth2_views_SurfaceView_render");
  N->sv_resize = need(i, NS "labyrinth2_views_SurfaceView_resize");
  N->font_init = need(i, NS "common_ZFont_init");
  N->font_release = need(i, NS "common_ZFont_release");
  N->touch[0] = need(i, NS "common_ZTouch_Pressed");
  N->touch[1] = need(i, NS "common_ZTouch_Released");
  N->touch[2] = need(i, NS "common_ZTouch_Moved");
  N->touch[3] = need(i, NS "common_ZTouch_Cancelled");
  N->accel_event = need(i, NS "labyrinth2_managers_Accelerometer_onEvent");
  N->dump_init = need(i, NS "labyrinth2_util_FileSystemUtil_initDumpBufferForThisThread");
  N->stat_save = need(i, NS "labyrinth2_managers_StatisticsManager_saveStatistics");
  N->stat_enable = need(i, NS "labyrinth2_managers_StatisticsManager_enableStatistics");
  N->stat_disable = need(i, NS "labyrinth2_managers_StatisticsManager_disableStatistics");
  N->th_setup = need(i, NS "labyrinth2_managers_ThumbnailManager_setupThumbnails");
  N->th_render = need(i, NS "labyrinth2_managers_ThumbnailManager_renderThumbnail");
  N->th_release = need(i, NS "labyrinth2_managers_ThumbnailManager_release");
  N->ok = N->act_init && N->act_destroy && N->act_show_menu && N->sv_init && N->sv_destroy && N->sv_render &&
          N->sv_resize && N->font_init && N->touch[0] && N->touch[1] && N->touch[2];
  if (e->active && !e->surface)
    e->pending_init = 1;
}

/* the thumbnails' copy's natives */
#define N (E[g_th].nat)

int lab_game_active(void) { return E[g_sp].active; }
int lab_game_ipad(void) { return E[g_sp].active && g_sp == 2; }
int lab_game_eng_active(int eng) { return eng >= 0 && eng < 4 && E[eng].active; }

static void touch_frame(Eng *e, float x_dp, float y_dp, float m[6]);

void lab_game_eng_touch(int eng, int action, float x_dp, float y_dp) {
  Eng *e = &E[eng];
  if (!e->active || e->nq >= MAXQ)
    return;
  /* SurfaceView.a()/b() with g = true: 320 x 480, y up -- through the frame
   * the engine looks for it in (a button's, its overlays turned), chosen
   * where the finger goes down */
  if (action == 0)
    touch_frame(e, x_dp, y_dp, e->tm);
  float x = x_dp, y = 480.0f - y_dp;
  const float *m = e->tm;
  e->q[e->nq].action = action;
  e->q[e->nq].x = m[0] * x + m[2] * y + m[4];
  e->q[e->nq].y = m[1] * x + m[3] * y + m[5];
  e->nq++;
}

void lab_game_touch(int action, float x_dp, float y_dp) { lab_game_eng_touch(g_sp, action, x_dp, y_dp); }

void lab_game_eng_tilt(int eng, float x, float y, float z) {
  E[eng].tilt[0] = x, E[eng].tilt[1] = y, E[eng].tilt[2] = z;
}

void lab_game_tilt(float x, float y, float z) { lab_game_eng_tilt(g_sp, x, y, z); }

void lab_game_on_finish(void) {
  debugPrintf("[game] the game finished (GameActivity.finish, engine %d)\n", g_lab_eng);
  if (!E[g_lab_eng].race)
    lab_audio_stop_all();
  E[g_lab_eng].finish = 1;
}

void lab_game_on_show_settings(void) {
  debugPrintf("[game] showSettings: the Settings screen over the game\n");
  E[g_lab_eng].settings = 1;
}

void lab_game_on_finished_pack(void) {
  debugPrintf("[game] finishedLevelPack (engine %d)\n", g_lab_eng);
  E[g_lab_eng].finished_pack = 1;
}

int lab_game_finished_pack(void) { return E[g_sp].finished_pack; }

/* the surface it draws into: local play's own, the iPad board's (3:4), or
 * the portrait one */
static void eng_size(const Eng *e, int *w, int *h) {
  *w = e->race ? lab_gfx_race_w() : lab_eng_ipad(e->idx) ? lab_gfx_ipad_w() : lab_gfx_surface_w();
  *h = e->race ? lab_gfx_race_h() : lab_eng_ipad(e->idx) ? lab_gfx_ipad_h() : lab_gfx_surface_h();
}

static void eng_resize(Eng *e) {
  int w, h;
  eng_size(e, &w, &h);
  g_lab_eng = e->idx;
  e->nat.sv_resize(g_jni_env, e->sv, w, h);
  g_lab_eng = 0;
}

void lab_game_resize(void) {
  if (E[g_sp].surface)
    eng_resize(&E[g_sp]);
}

static void bind_target(Eng *e) {
  if (e->race)
    lab_gfx_race_begin(e->idx - g_lab_race_p1); /* its board: 0 or 1 */
  else if (lab_eng_ipad(e->idx))
    lab_gfx_begin_ipad();
  else
    lab_gfx_begin_portrait();
}

/* onSurfaceCreated + onSurfaceChanged */
static void surface_create(Eng *e) {
  u64 t0 = armGetSystemTick();
  g_lab_eng = e->idx;
  if (e->surface)
    e->nat.sv_destroy(g_jni_env, e->sv);
  /* the row may have moved on (the info screen's level choice): as the
   * Bundle's LevelPack, made from the table now (a race: its level kept) */
  LabPack *row = lab_levels_find(e->pack.id);
  if (row && !e->race)
    e->pack = *row;
  jni_release(e->pack_obj);
  e->pack_obj = lab_java_levelpack(&e->pack);
  bind_target(e);
  e->nat.sv_init(g_jni_env, e->sv, e->pack_obj, 2, 0);
  jni_exception_report("SurfaceView.init");
  e->surface = 1;
  e->orient = 0; /* its overlays are new: their way up again */
  e->orient_set = 0;
  g_lab_eng = 0;
  eng_resize(e);
  debugPrintf("[game] %s \"%s\" loaded at level %d of %d in %llu ms (engine %d)\n", e->pack.id, e->pack.name,
              e->pack.current + 1, e->pack.nlevels,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull), e->idx);
}

/* onPause: the surface goes, the statistics are saved */
static void surface_destroy(Eng *e) {
  if (!e->surface)
    return;
  g_lab_eng = e->idx;
  e->nat.sv_destroy(g_jni_env, e->sv);
  e->surface = 0;
  if (!e->race)
    lab_audio_stop_all();
  if (e->nat.stat_save)
    e->nat.stat_save(g_jni_env, g_stat_cls);
  if (e->nat.stat_enable)
    e->nat.stat_enable(g_jni_env, g_stat_cls);
  g_lab_eng = 0;
  lab_reg_save();
  lab_levels_save();
}

/* Can engine copy eng open pack id's zip the way its level loader will
 * (its resource dir + zipfiles/<id>, through the same shims)? The loader
 * does not look (+0x2cab8: a zip it cannot open, or a level not in it, is
 * a read through a null handle). When the card has it but the shims'
 * directory listings (dcr_dircache.c) say it is not there, they are
 * forgotten and it is asked again. */
void *b_fopen(const char *path, const char *mode);
int b_fclose(void *fp);
void dcr_dircache_forget(void);

static int engine_sees_zip(int eng, const char *id) {
  char path[256];
  snprintf(path, sizeof path, "/data/data/se.illusionlabs.labyrinth2/%s/zipfiles/%s", lab_eng_ipad(eng) ? "files-ipad" : "files",
           id);
  for (int attempt = 0; attempt < 2; attempt++) {
    void *f = b_fopen(path, "rb");
    if (f) {
      b_fclose(f);
      if (attempt)
        debugPrintf("[game] %s: the file listings had missed it (refreshed)\n", id);
      return 1;
    }
    if (attempt || !lab_files_have_pack_dev(id, lab_eng_ipad(eng)))
      break;
    dcr_dircache_forget();
  }
  debugPrintf("[game] %s: engine %d cannot open its zip (%s): not used\n", id, eng, path);
  return 0;
}

static int eng_start(Eng *e, const LabPack *p, int race) {
  resolve_eng(e);
  if (!engine_sees_zip(e->idx, p->id))
    return -1;
  if (!e->nat.ok) {
    debugPrintf("[game] engine %d lacks the game's natives: cannot play\n", e->idx);
    return -1;
  }
  if (e->active)
    lab_game_eng_end(e->idx);
  if (!e->sv) {
    e->sv = jni_new("se/illusionlabs/labyrinth2/views/SurfaceView");
    e->accel_arr = jni_array('F', 3);
    e->activity = e->idx ? jni_new("se/illusionlabs/labyrinth2/activities/GameActivity") : g_game_activity;
  }
  if (!g_fsu_cls) {
    g_fsu_cls = jni_class("se/illusionlabs/labyrinth2/util/FileSystemUtil")->obj;
    g_stat_cls = jni_class("se/illusionlabs/labyrinth2/managers/StatisticsManager")->obj;
  }
  e->pack = *p;
  e->race = race;
  e->finish = e->settings = e->finished_pack = 0;
  e->nq = 0;
  debugPrintf("[game] starting %s \"%s\" (%s, level %d of %d)%s\n", p->id, p->name,
              p->preloaded ? "official" : "downloaded", p->current + 1, p->nlevels,
              race ? (e->idx != g_lab_race_p1 ? ", local play: player 2" : ", local play: player 1") : "");
  int dd = lab_draw_suspend();
  g_lab_eng = e->idx;
  /* GameActivity.onCreate: init(this) */
  e->nat.act_init(g_jni_env, e->activity, e->activity);
  jni_exception_report("GameActivity.init");
  /* new SurfaceView: new ZFont() -> init() */
  e->font = jni_new("se/illusionlabs/common/ZFont");
  e->nat.font_init(g_jni_env, e->font);
  jni_exception_report("ZFont.init");
  /* onResume: statistics off for one's own levels (and for a race) */
  if ((p->ownlevel || race) && e->nat.stat_disable)
    e->nat.stat_disable(g_jni_env, g_stat_cls);
  g_lab_eng = 0;
  lab_draw_resume(dd);
  e->active = 1;
  e->pending_init = 1; /* a "Loading..." frame first (the Java's ProgressDialog) */
  return 0;
}

int lab_game_start(const LabPack *p) {
  if (E[g_sp].active)
    lab_game_end();
  g_sp = p->ipad ? 2 : 0;
  if (p->ipad && lab_load_ipad_engine() != 0) {
    g_sp = 0;
    return -1;
  }
  return eng_start(&E[g_sp], p, 0);
}
int lab_game_eng_start(int eng, const LabPack *p) { return eng_start(&E[eng], p, 1); }

void lab_game_eng_show_menu(int eng) {
  Eng *e = &E[eng];
  if (e->active && e->surface) {
    g_lab_eng = eng;
    e->nat.act_show_menu(g_jni_env, e->activity);
    jni_exception_report("GameActivity.showMenu");
    g_lab_eng = 0;
  }
}

void lab_game_show_menu(void) { lab_game_eng_show_menu(g_sp); }

void lab_game_eng_end(int eng) {
  Eng *e = &E[eng];
  if (!e->active)
    return;
  surface_destroy(e);
  g_lab_eng = eng;
  /* onDestroy: a downloaded pack's play count, your rating and your
   * difficulty to the server (br.java), then the play count is 0 */
  LabPack *row = lab_levels_find(e->pack.id);
  if (!e->race && row && !row->preloaded && !row->ownlevel && lab_online_enabled()) {
    lab_online_set_ipad(row->ipad); /* the pack's own server */
    lab_online_update(row->id, row->playcount, row->myrating, row->mydifficulty);
    lab_online_set_ipad(0);
    row->playcount = 0;
  }
  e->nat.act_destroy(g_jni_env, e->activity);
  jni_exception_report("GameActivity.destroy");
  if (e->font) {
    if (e->nat.font_release)
      e->nat.font_release(g_jni_env, e->font);
    jni_release(e->font);
    e->font = NULL;
  }
  g_lab_eng = 0;
  jni_release(e->pack_obj);
  e->pack_obj = NULL;
  e->active = 0;
  e->race = 0;
  debugPrintf("[game] left %s (engine %d)\n", e->pack.id, eng);
}

void lab_game_end(void) { lab_game_eng_end(g_sp); }

/* a race's next level: the surface made again at it */
void lab_game_eng_restart(int eng, int level) {
  Eng *e = &E[eng];
  if (!e->active)
    return;
  e->pack.current = level;
  e->nq = 0;
  e->finish = e->finished_pack = 0;
  surface_create(e);
}

static void draw_loading(Eng *e) {
  bind_target(e);
  lgl.ClearColor(0, 0, 0, 1);
  lgl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  int w, h;
  eng_size(e, &w, &h);
  float uw = lab_eng_ipad(e->idx) ? 360 : 320; /* the iPad board's surface is 3:4 */
  lab_draw_begin(uw, 480, w, h);
  const LabTex *bg = lab_tex("bg_light_blue");
  if (bg)
    lab_draw_image(bg, 0, 0, uw, 480, 0xffffffffu);
  /* a level shown turned (its top to the left, or right): the words turned
   * back on its surface, so they read across the screen (hardware
   * 2026-09-28: they read up it) */
  float ang = 0;
  if (!e->race && lab_gfx_level_turned())
    ang = dcr_config()->level_layout == LAB_LAYOUT_ROTATED_RIGHT ? -90.0f : 90.0f;
  if (ang != 0) {
    float a = ang * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a), cx = uw * 0.5f, cy = 240;
    const float m[16] = {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, cx - c * cx + s * cy, cy - s * cx - c * cy, 0, 1};
    lgl.MatrixMode(GL_MODELVIEW);
    lgl.LoadMatrixf(m);
  }
  lab_draw_rrect(uw * 0.5f - 70, 212, 140, 56, 8, 0x000000c0u);
  lab_draw_text(uw * 0.5f, 246, 16, 0xffffffffu, LAB_CENTER, 1, "Loading...");
  if (ang != 0) {
    lgl.MatrixMode(GL_MODELVIEW);
    lgl.LoadIdentity();
  }
  lab_draw_end();
}

static void eng_orient(Eng *e);

/* One frame of an engine: its "Loading..." first, then its surface, its
 * tilt, touches, render(). render = 0: its last picture stays (frozen). */
static void eng_frame(Eng *e, int render) {
  if (!e->active)
    return;
  if (e->pending_init) {
    if (e->pending_init == 1) {
      draw_loading(e); /* shown this frame; the (blocking) load on the next */
      e->pending_init = 2;
      return;
    }
    e->pending_init = 0;
    surface_create(e);
  }
  if (!e->surface || !render)
    return;
  eng_orient(e);
  bind_target(e);
  g_lab_eng = e->idx;
  /* the accelerometer, then the touches, then the frame */
  if (e->nat.accel_event) {
    float *d = e->accel_arr->a.data;
    d[0] = e->tilt[0], d[1] = e->tilt[1], d[2] = e->tilt[2];
    e->nat.accel_event(g_jni_env, lab_java_accel(e->idx), e->accel_arr);
  }
  if (e->nat.dump_init)
    e->nat.dump_init(g_jni_env, g_fsu_cls);
  static int log_touch = -1;
  if (log_touch < 0)
    log_touch = dcr_config()->log_touch;
  for (int i = 0; i < e->nq; i++) {
    if (log_touch)
      debugPrintf("[touch] %s %.1f,%.1f\n",
                  (const char *const[]){"down", "up", "move", "cancel"}[e->q[i].action & 3], e->q[i].x,
                  e->q[i].y);
    fn_ff f = e->nat.touch[e->q[i].action & 3];
    if (f)
      f(g_jni_env, g_ztouch, e->q[i].x, e->q[i].y);
  }
  e->nq = 0;
  lgl.Disable(GL_SCISSOR_TEST); /* the engine draws the whole surface */
  e->nat.sv_render(g_jni_env, e->sv);
  jni_exception_report("SurfaceView.render");
  g_lab_eng = 0;
  if (!e->race) /* the ghost ball: where it is next frame (lab_ghost.c) */
    lab_ghost_frame(e->idx);
}

void lab_game_eng_frame(int eng, int render) { eng_frame(&E[eng], render); }
int lab_game_loading(void) { return E[g_sp].active && E[g_sp].pending_init != 0; }
int lab_game_eng_finished(int eng) { return E[eng].finish; }

void lab_game_frame(void) {
  Eng *e = &E[g_sp];
  if (!e->active)
    return;
  eng_frame(e, 1);
  if (e->settings) {
    /* startActivity(SettingsActivity): on Android this activity paused (its
     * surface went) and the level began again when Settings closed. Here
     * the level stays as it is, paused under the pause screen, with the
     * Settings over it (hardware 2026-09-27: closing them restarted the
     * level) */
    e->settings = 0;
    lab_ui_open_settings_over_game();
  }
  if (e->finish) {
    e->finish = 0;
    lab_game_end();
    lab_ui_game_ended();
  }
}

/* Settings > Tilt view, from a level too: the renderer read the setting
 * once, when it was made (+0x3bc28 -> its +0x68), and looks at that every
 * frame (+0x3bfce): set there as well (each copy that is loaded). */
static int heap_ptr(uintptr_t p, size_t len);
static uintptr_t eng_base(int eng);

void lab_game_set_tilt_view(int on) {
  static const uint8_t sig[4] = {0xa0, 0x60, 0xa2, 0x66}; /* +0x3bd12: str r0,[r4,8]; str r2,[r4,0x68] */
  for (int eng = 0; eng < 4; eng++) {
    if (!lab_native_in(eng, "JNI_OnLoad"))
      continue; /* not loaded */
    uintptr_t base = eng_base(eng);
    if (!base || memcmp((const void *)(base + 0x3bd12), sig, sizeof sig))
      continue;
    uintptr_t r = *(volatile uintptr_t *)(base + 0x4f3dc);
    if (heap_ptr(r, 0x78))
      *(volatile int *)(r + 0x68) = on ? 1 : 0;
  }
}

/* Settings closed over a game: onResume -> onSurfaceCreated again (only
 * when its surface went: it no longer does). */
void lab_game_resume_after_settings(void) {
  if (E[g_sp].active && !E[g_sp].surface)
    E[g_sp].pending_init = 1;
  else if (E[g_sp].active && lab_game_eng_popup(g_sp) == 0) {
    /* back from Settings on a level whose pause screen closed as they
     * opened: paused again, not a rolling ball */
    debugPrintf("[game] back from Settings: the pause screen again\n");
    lab_game_show_menu();
  }
}

/* ------------------------------------------------------------ the overlay */
static int heap_ptr(uintptr_t p, size_t len);
/* Is one of the engine's popups up (the pause overlay, a level's end, the
 * rating)? showMenu() asks the same (liblabyrinthii 1.29 +0x3beb6): the
 * game singleton (+0x4f3dc), its overlay manager (+0x38), the popup on it
 * (+0x188). Read as data here, after checking the code is that build's.
 * A level's end does not set that: its buttons showing (Menu, Again,
 * Next...) are counted too. */
static int read_button(uintptr_t b, LabBtn *o);
static const uint16_t k_known[] = {0xdc, 0xd4, 0xd8, 0xd0, 0xc8, 0xcc, 0xc4}; /* the focus's order */

/* A button to press is one wholly on the screen, of a button's size: while
 * a level is played the overlay keeps its buttons "visible" parked at the
 * corner (0, 480: half off the screen), which is no popup. */
static int on_screen(const LabBtn *b) {
  return b->w <= 200 && b->h <= 200 && b->x >= -2 && b->y >= -2 && b->x + b->w <= 322 && b->y + b->h <= 482;
}

static uintptr_t eng_base(int eng) { return (uintptr_t)lab_module(eng)->load_virtbase; }

int lab_game_eng_popup(int eng) {
  static int ok = -1;
  uintptr_t base = eng_base(eng);
  if (ok < 0) {
    static const uint8_t sig[12] = {0x08, 0xb5, 0xff, 0xf7, 0xda, 0xff, 0x82, 0x6b, 0xc4, 0x23, 0x5b, 0x00};
    ok = base && !memcmp((const void *)(base + 0x3beb6), sig, sizeof sig);
    debugPrintf("[game] the engine's popup state: %s\n", ok ? "readable" : "unknown (another build)");
  }
  if (!ok || !E[eng].surface)
    return -1;
  uintptr_t game = *(volatile uintptr_t *)(base + 0x4f3dc);
  if (!heap_ptr(game, 0x40))
    return 0;
  uintptr_t mgr = *(volatile uintptr_t *)(game + 0x38);
  if (!heap_ptr(mgr, 0x18c))
    return 0;
  if (*(volatile uintptr_t *)(mgr + 0x188) != 0)
    return 1; /* the pause screen */
  if (!heap_ptr(mgr, 0x300))
    return 0;
  for (unsigned k = 0; k < sizeof k_known / sizeof k_known[0]; k++) {
    LabBtn b;
    if (read_button(*(volatile uintptr_t *)(mgr + k_known[k]), &b) && b.visible && on_screen(&b))
      return 2; /* a level's end (its buttons, no pause) */
  }
  return 0;
}

int lab_game_popup_open(void) { return lab_game_eng_popup(g_sp); }

/* ---- the overlays' way up
 * The engine turns its overlays (the pause screen, a level's end, the
 * rating) for a phone held any way up: GameRenderer.setOrientation (1.29
 * +0x3c0e0: the renderer's +0x4c, then the overlay manager's +0x38980 --
 * 1 portrait, 2 upside down, 3 and 4 the two landscapes: a 480 x 320 layout
 * turned into the portrait screen, its buttons' hit tests turned with it).
 * Android never called it. A level turned to fill the screen (its top to
 * the left) has them read upright with 3, so the pause screen no longer
 * turns the picture back upright. Set again for each level (its overlays
 * are made with it) and each popup. */
static const uint8_t k_orient_sig[16] = {0x08, 0xb5, 0xc1, 0x64, 0x80, 0x6b, 0x00, 0x28,
                                         0x01, 0xd0, 0xfc, 0xf7, 0x49, 0xfc, 0x08, 0xbd};

static void eng_orient(Eng *e) {
  int want = !e->race && lab_gfx_level_turned() ? 3 : 1;
  int pop = lab_game_eng_popup(e->idx);
  if (pop != e->last_popup) {
    e->last_popup = pop;
    e->orient = 0; /* a popup's buttons: turned when it shows */
  }
  if (want == e->orient)
    return;
  uintptr_t base = eng_base(e->idx);
  if (!base || memcmp((const void *)(base + 0x3c0e0), k_orient_sig, sizeof k_orient_sig)) {
    e->orient = want; /* another build: its overlays stay as they are */
    return;
  }
  uintptr_t game = *(volatile uintptr_t *)(base + 0x4f3dc);
  if (!heap_ptr(game, 0x50))
    return;
  uintptr_t mgr = *(volatile uintptr_t *)(game + 0x38);
  int arg = want;
  /* the manager swaps 1 <-> 2 and 3 <-> 4 when its +0x144 is set */
  if (heap_ptr(mgr, 0x148) && *(volatile int *)(mgr + 0x144))
    arg = want == 1 ? 2 : want == 2 ? 1 : want == 3 ? 4 : 3;
  void (*set)(uintptr_t, int) = (void (*)(uintptr_t, int))(base + 0x3c0e0 + 1);
  int dd = lab_draw_suspend();
  g_lab_eng = e->idx;
  set(game, arg);
  e->orient_set = 1;
  g_lab_eng = 0;
  lab_draw_resume(dd);
  if (e->orient == 0 || e->orient != want)
    debugPrintf("[game] engine %d: overlays %s\n", e->idx, want == 3 ? "turned with the level (landscape)" : "upright");
  e->orient = want;
}

/* The buttons on the engine's overlay, for the controller: the overlay
 * manager keeps them (made in its constructor, +0x37180: Previous, Menu,
 * Settings, Restart, Next, Again, Continue at +0xc4..+0xdc, the paused
 * screen's "tap here to resume" at +0xf4). A button (0xa8 bytes): x, y
 * (its centre), w, h (ints, the 320x480 space, y up), hit padding +0x14,
 * alpha +0x10 (float), its turn +0x18 (degrees), visible +0x8c, enabled
 * +0x90, and a matrix (+0x38, 4x4 column-major; its 2x2 inverse +0x78):
 * the overlays' way up (setOrientation, lab_game.c above), identity till
 * then. It is DRAWN at x, y as they are; its hit test (+0x36bf4) takes the
 * touch back through the matrix first -- an iPhone turned on its side sent
 * its touches in the turned frame. So the glow goes where it is drawn, and
 * a press on it is sent through its matrix (touch_frame; hardware
 * 2026-09-26: with the overlays turned the glow sat on the touch spots,
 * a quarter turn away from the buttons). Other popups' buttons are found
 * by scanning the overlay and the popup for pointers that look like
 * buttons. Everything is checked before it is read: heap pointers only. */
static int heap_ptr(uintptr_t p, size_t len) {
  return p && !(p & 3) && p >= g_nxinit.heap_base && p + len <= g_nxinit.heap_base + g_nxinit.heap;
}

static int sane(float v, float lim) { return v == v && fabsf(v) < lim; }

static int read_button(uintptr_t b, LabBtn *o) {
  if (!heap_ptr(b, 0xa8))
    return 0;
  const volatile int32_t *i = (const volatile int32_t *)b;
  const volatile float *f = (const volatile float *)b;
  int x = i[0], y = i[1], w = i[2], h = i[3];
  int vis = i[0x8c / 4], en = i[0x90 / 4];
  float alpha = f[0x10 / 4];
  if (w < 4 || w > 480 || h < 4 || h > 480 || x < -400 || x > 800 || y < -400 || y > 900)
    return 0;
  if ((vis & ~1) || (en & ~1) || !(alpha >= 0.0f && alpha <= 1.01f) || !heap_ptr((uintptr_t)i[0x34 / 4], 32))
    return 0;
  float a = f[0x38 / 4], c = f[0x3c / 4], bb = f[0x48 / 4], d = f[0x4c / 4], tx = f[0x68 / 4], ty = f[0x6c / 4];
  if (!sane(a, 8) || !sane(bb, 8) || !sane(c, 8) || !sane(d, 8) || !(fabsf(a * d - bb * c) > 1e-3f) ||
      !sane(tx, 2000) || !sane(ty, 2000))
    return 0;
  o->ptr = b;
  o->visible = vis && en && alpha > 0.3f;
  o->w = (float)w;
  o->h = (float)h;
  o->x = (float)x - o->w * 0.5f;
  o->y = (480.0f - (float)y) - o->h * 0.5f; /* dp, y down */
  o->m[0] = a, o->m[1] = c, o->m[2] = bb, o->m[3] = d, o->m[4] = tx, o->m[5] = ty;
  return 1;
}

/* the buttons of engine eng's overlay: resume too (for a touch's frame) or
 * only those the controller moves between */
static int eng_buttons(int eng, LabBtn *out, int cap, int with_resume) {
  uintptr_t base = eng_base(eng);
  uintptr_t game = *(volatile uintptr_t *)(base + 0x4f3dc);
  uintptr_t mgr = heap_ptr(game, 0x40) ? *(volatile uintptr_t *)(game + 0x38) : 0;
  if (!heap_ptr(mgr, 0x300))
    return 0;
  uintptr_t popup = *(volatile uintptr_t *)(mgr + 0x188);
  int n = 0;
  /* the known ones first, in the order they are preferred for the focus;
   * not the paused screen's "tap here to resume" (+0xf4: a band across the
   * top, mostly off the screen -- + and B resume) */
  uintptr_t seen[64];
  int nseen = 0;
  for (unsigned k = 0; k < sizeof k_known / sizeof k_known[0]; k++)
    seen[nseen++] = *(volatile uintptr_t *)(mgr + k_known[k]);
  uintptr_t resume = *(volatile uintptr_t *)(mgr + 0xf4);
  for (uintptr_t a = mgr + 0x40; a < mgr + 0x300 && nseen < 64; a += 4) {
    uintptr_t v = *(volatile uintptr_t *)a;
    if (heap_ptr(v, 0xa8))
      seen[nseen++] = v;
  }
  if (heap_ptr(popup, 0x200))
    for (uintptr_t a = popup; a < popup + 0x200 && nseen < 64; a += 4) {
      uintptr_t v = *(volatile uintptr_t *)a;
      if (heap_ptr(v, 0xa8))
        seen[nseen++] = v;
    }
  for (int s = 0; s < nseen && n < cap; s++) {
    int dup = 0;
    for (int k = 0; k < s; k++)
      dup |= seen[k] == seen[s];
    LabBtn b;
    if (dup || (seen[s] == resume && !with_resume) || !read_button(seen[s], &b) || !b.visible)
      continue;
    if (!with_resume && !on_screen(&b))
      continue;
    out[n++] = b;
  }
  return n;
}

/* The frame a touch at (x, y) dp is sent in: the button's under it (with a
 * little room), else, its overlays turned, the manager's (+0x148: every
 * button setOrientation turns has it), else as it is. */
static void touch_frame(Eng *e, float x_dp, float y_dp, float m[6]) {
  static const float id[6] = {1, 0, 0, 1, 0, 0};
  memcpy(m, id, sizeof id);
  if (!e->surface || lab_game_eng_popup(e->idx) < 0)
    return;
  LabBtn b[32];
  int n = eng_buttons(e->idx, b, 32, 1);
  for (int pad = 0; pad <= 12; pad += 12)
    for (int i = 0; i < n; i++)
      if (x_dp >= b[i].x - pad && x_dp < b[i].x + b[i].w + pad && y_dp >= b[i].y - pad &&
          y_dp < b[i].y + b[i].h + pad) {
        memcpy(m, b[i].m, sizeof b[i].m);
        return;
      }
  if (!e->orient_set || e->orient == 1)
    return;
  uintptr_t base = eng_base(e->idx);
  uintptr_t game = *(volatile uintptr_t *)(base + 0x4f3dc);
  uintptr_t mgr = heap_ptr(game, 0x40) ? *(volatile uintptr_t *)(game + 0x38) : 0;
  if (!heap_ptr(mgr, 0x180))
    return;
  const volatile float *f = (const volatile float *)(mgr + 0x148);
  float t[6] = {f[0], f[1], f[4], f[5], f[12], f[13]};
  for (int k = 0; k < 6; k++)
    if (!sane(t[k], 2000))
      return;
  if (!(fabsf(t[0] * t[3] - t[2] * t[1]) > 1e-3f))
    return;
  memcpy(m, t, sizeof t);
}

/* ---- the ball (lab_ghost.c)
 * The game object (+0x4f408, made by +0x3f068): its level +8, its native
 * pack +0xc (+0 the id, +0x2c the level), the run's time +0x1c (s: its
 * update +0x3f92c adds each step's time once the 3-2-1 is over, the frame
 * +0x18; a best time is it in ms, +0x3f460). The level's +8: a vector {data,
 * cap, count} of its ball slots (the list the engine's own ghost recorder
 * takes, +0x2239c) -- four, the first in play in single player (+0x48 set).
 * A ball's getters (vtable +0x10 / +0x14: +0x299f0 / +0x29a0c): while it is
 * in play (+0x28 set: after the level's 3-2-1) its body's +0x5c, +0x60,
 * else where it starts, +0x18, +0x1c. The board's units, y up (found with
 * the Ryujinx test runs, 2026-09-28). */
int lab_game_eng_run(int eng, float *secs, int *level, char *id, size_t idcap) {
  uintptr_t base = eng_base(eng);
  if (!base || !E[eng].surface)
    return -1;
  uintptr_t game = *(volatile uintptr_t *)(base + 0x4f408);
  if (!heap_ptr(game, 0x2c))
    return -1;
  uintptr_t pack = *(volatile uintptr_t *)(game + 0xc);
  if (!heap_ptr(pack, 0x60))
    return -1;
  uintptr_t pid = *(volatile uintptr_t *)pack;
  if (!heap_ptr(pid, 4))
    return -1;
  float t = *(volatile float *)(game + 0x1c);
  *secs = t == t && t >= 0 && t < 1e6f ? t : 0;
  *level = *(volatile int *)(pack + 0x2c);
  snprintf(id, idcap, "%.40s", (const char *)pid);
  return 0;
}

/* the level's ball in play (its slot's +0x48 set), or 0 */
static uintptr_t eng_ball(int eng) {
  uintptr_t base = eng_base(eng);
  if (!base || !E[eng].surface)
    return 0;
  uintptr_t game = *(volatile uintptr_t *)(base + 0x4f408);
  if (!heap_ptr(game, 0x2c))
    return 0;
  uintptr_t level = *(volatile uintptr_t *)(game + 8);
  if (!heap_ptr(level, 0x10))
    return 0;
  uintptr_t vec = *(volatile uintptr_t *)(level + 8);
  if (!heap_ptr(vec, 12))
    return 0;
  uintptr_t data = *(volatile uintptr_t *)vec;
  int n = *(volatile int *)(vec + 8);
  if (n <= 0 || n > 16 || !heap_ptr(data, (size_t)n * 4))
    return 0;
  for (int i = 0; i < n; i++) {
    uintptr_t o = *(volatile uintptr_t *)(data + (uintptr_t)i * 4);
    if (heap_ptr(o, 0x80) && *(volatile uint8_t *)(o + 0x48))
      return o;
  }
  return 0;
}

int lab_game_eng_ball(int eng, float *x, float *y, int *live) {
  uintptr_t o = eng_ball(eng);
  if (!o)
    return -1;
  const volatile float *f = (const volatile float *)o;
  int in_play = *(volatile uint8_t *)(o + 0x28) != 0;
  float bx = in_play ? f[0x5c / 4] : f[0x18 / 4], by = in_play ? f[0x60 / 4] : f[0x1c / 4];
  if (!(bx == bx) || !(by == by) || fabsf(bx) > 4000 || fabsf(by) > 4000)
    return -1;
  *x = bx, *y = by;
  if (live)
    *live = in_play;
  return 0;
}

/* ---- the ghost ball, drawn by the engine (lab_ghost.c)
 * A level's balls are drawn by one batched renderer (+0x2e67c, vtable
 * +0x4d460), made for each level by the game renderer (*(+0x4f3dc), its
 * +0x3cea0) and kept at its +48. It is made for two lists: the level's
 * balls (the level's +8) and its GhostBalls (the level's objects of type
 * 25). A ghost it draws as the iPad did: the player's ball from
 * game-objects (setting-ball-selected) and its shadow, 0.35 as opaque, no
 * reflection, under the walls. Android makes no GhostBall (the manager
 * that would, +0x21b14, is never called), so that list is always empty.
 * Here: once a level's renderer is up, another is made for the same balls
 * and one GhostBall of ours, and takes its place (the old one deleted, as
 * the engine does at the next level). Of a GhostBall the renderer reads
 * its vtable (+0x4ca58: x +24, y +28), shown (+40), its radius (+76). */
static struct {
  uint32_t obj[32]; /* our GhostBall */
  uintptr_t data, list[3];
  int off;          /* another build: no ghost */
} GB[4];

/* the renderer made here: its first entry (+56: a vector of 28-byte
 * entries, the ghosts first; +4 the GhostBall) is ours -- not its address,
 * which the next level's own may be given again */
static int gb_ours(int eng, uintptr_t br) {
  uintptr_t vec = *(volatile uintptr_t *)(br + 56);
  if (*(volatile int *)(br + 136) != 1 || !heap_ptr(vec, 12) || *(volatile int *)(vec + 8) < 1)
    return 0;
  uintptr_t data = *(volatile uintptr_t *)vec, e = heap_ptr(data, 4) ? *(volatile uintptr_t *)data : 0;
  return heap_ptr(e, 28) && *(volatile uintptr_t *)(e + 4) == (uintptr_t)GB[eng].obj;
}

static const uint8_t k_balls_sig[16] = {0xf0, 0xb5, 0x91, 0x4b, 0x00, 0x24, 0xc4, 0x61,
                                        0x7b, 0x44, 0x08, 0x33, 0x03, 0x60, 0x03, 0x1c};

int lab_game_eng_ghost(int eng, int shown, float x, float y) {
  if (eng < 0 || eng > 3 || GB[eng].off)
    return -1;
  uintptr_t base = eng_base(eng);
  if (!base || !E[eng].surface)
    return -1;
  uintptr_t game = *(volatile uintptr_t *)(base + 0x4f3dc);
  if (!heap_ptr(game, 0x78))
    return -1;
  uintptr_t br = *(volatile uintptr_t *)(game + 48);
  uint32_t *o = GB[eng].obj;
  if (!heap_ptr(br, 160) || *(volatile uintptr_t *)br != base + 0x4d460)
    return -1; /* no level's balls (yet) */
  if (!gb_ours(eng, br)) {
    if (!shown)
      return 0;
    if (memcmp((const void *)(base + 0x2e67c), k_balls_sig, sizeof k_balls_sig) ||
        *(volatile uintptr_t *)(base + 0x4ca58 + 0x10) != base + 0x2a8c5) {
      GB[eng].off = 1;
      debugPrintf("[ghost] engine %d: another build, no ghost ball\n", eng);
      return -1;
    }
    uintptr_t gs = *(volatile uintptr_t *)(base + 0x4f408);
    uintptr_t level = heap_ptr(gs, 0x2c) ? *(volatile uintptr_t *)(gs + 8) : 0;
    uintptr_t balls = heap_ptr(level, 0x10) ? *(volatile uintptr_t *)(level + 8) : 0;
    if (!heap_ptr(balls, 12))
      return -1;
    memset(o, 0, sizeof GB[eng].obj);
    o[0] = (uint32_t)(base + 0x4ca58);
    o[64 / 4] = 25;
    float r = 13.0f; /* the iPad's GhostBall; the ball's own when there is one */
    memcpy(&o[76 / 4], &r, 4);
    GB[eng].data = (uintptr_t)o;
    GB[eng].list[0] = (uintptr_t)&GB[eng].data, GB[eng].list[1] = 1, GB[eng].list[2] = 1;
    void *mem = calloc(1, 160);
    if (!mem)
      return -1;
    u64 t0 = armGetSystemTick();
    void (*make)(void *, uintptr_t, uintptr_t *) = (void (*)(void *, uintptr_t, uintptr_t *))(base + 0x2e67c + 1);
    int dd = lab_draw_suspend();
    g_lab_eng = eng;
    make(mem, balls, GB[eng].list);
    *(volatile uintptr_t *)(game + 48) = (uintptr_t)mem;
    void (*del)(uintptr_t) = (void (*)(uintptr_t))(*(volatile uintptr_t *)(*(volatile uintptr_t *)br + 4));
    del(br);
    g_lab_eng = 0;
    lab_draw_resume(dd);
    debugPrintf("[ghost] engine %d: the level's balls drawn with a ghost ball (%llu ms)\n", eng,
                (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
  }
  uintptr_t ball = eng_ball(eng);
  if (ball) {
    float r = *(volatile float *)(ball + 108);
    if (r > 1.0f && r < 100.0f)
      memcpy(&o[76 / 4], &r, 4);
  }
  memcpy(&o[24 / 4], &x, 4);
  memcpy(&o[28 / 4], &y, 4);
  ((uint8_t *)o)[40] = shown ? 1 : 0;
  return 0;
}

int lab_game_overlay_buttons(LabBtn *out, int cap) {
  if (lab_game_popup_open() <= 0)
    return 0;
  int n = eng_buttons(g_sp, out, cap, 0);
  static uintptr_t last_popup;
  static int last_n = -1;
  uintptr_t base = eng_base(g_sp), game = *(volatile uintptr_t *)(base + 0x4f3dc);
  uintptr_t mgr = heap_ptr(game, 0x40) ? *(volatile uintptr_t *)(game + 0x38) : 0;
  uintptr_t popup = heap_ptr(mgr, 0x18c) ? *(volatile uintptr_t *)(mgr + 0x188) : 0;
  if (popup != last_popup || n != last_n) {
    last_popup = popup, last_n = n;
    debugPrintf("[overlay] popup %p: %d buttons\n", (void *)popup, n);
    for (int k = 0; k < n; k++)
      debugPrintf("[overlay]   %p at %.0f,%.0f %.0fx%.0f dp (touched through %.2g %.2g %.2g %.2g + %.0f,%.0f)\n",
                  (void *)out[k].ptr, out[k].x, out[k].y, out[k].w, out[k].h, out[k].m[0], out[k].m[1],
                  out[k].m[2], out[k].m[3], out[k].m[4], out[k].m[5]);
  }
  return n;
}

/* ------------------------------------------------------------ thumbnails */
static JObj *g_thumbs, *g_thumbs_pack;
static int g_th_w, g_th_h;

int lab_thumbs_setup(const LabPack *p, int w, int h) {
  lab_thumbs_release();
  g_th = p->ipad ? 2 : 0;
  if (p->ipad && lab_load_ipad_engine() != 0) {
    g_th = 0;
    return -1;
  }
  resolve_eng(&E[g_th]);
  if (!N.th_setup || !N.th_render || E[g_th].active)
    return -1;
  if (!g_thumbs)
    g_thumbs = jni_new("se/illusionlabs/labyrinth2/managers/ThumbnailManager");
  if (!engine_sees_zip(g_th, p->id))
    return -1;
  g_thumbs_pack = lab_java_levelpack(p);
  g_th_w = w, g_th_h = h;
  u64 t0 = armGetSystemTick();
  int dd = lab_draw_suspend();
  N.th_setup(g_jni_env, g_thumbs, g_thumbs_pack, w, h);
  jni_exception_report("ThumbnailManager.setupThumbnails");
  lab_draw_resume(dd);
  debugPrintf("[thumbs] %s: set up for %dx%d in %llu ms\n", p->id, w, h,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
  return 0;
}

LabTex *lab_thumbs_render(int level) { return lab_thumbs_render_px(level, NULL); }

/* ... and its pixels (RGBA, rows bottom-up as the texture's), for keeping */
LabTex *lab_thumbs_render_px(int level, uint8_t **rgba) {
  if (rgba)
    *rgba = NULL;
  if (!g_thumbs_pack)
    return NULL;
  int dd = lab_draw_suspend();
  unsigned tex = lab_gfx_offscreen_begin(g_th_w, g_th_h);
  if (!tex) {
    lab_draw_resume(dd);
    return NULL;
  }
  lgl.ClearColor(0, 0, 0, 1);
  lgl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  /* the sounds a level makes while it is set up for drawing: none (dr.java) */
  lab_audio_enable(0);
  lgl.Disable(GL_SCISSOR_TEST);
  N.th_render(g_jni_env, g_thumbs, level);
  jni_exception_report("ThumbnailManager.renderThumbnail");
  lab_audio_enable(1);
  if (rgba && (*rgba = malloc((size_t)g_th_w * (size_t)g_th_h * 4)) != NULL) {
    lgl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    lgl.ReadPixels(0, 0, g_th_w, g_th_h, GL_RGBA, GL_UNSIGNED_BYTE, *rgba);
    for (size_t i = 0; i < (size_t)g_th_w * (size_t)g_th_h; i++)
      (*rgba)[i * 4 + 3] = 255; /* a picture of the board: opaque */
  }
  lab_gfx_offscreen_end();
  lab_draw_resume(dd);
  LabTex *t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->tex = tex;
  t->w = g_th_w, t->h = g_th_h;
  t->tw = t->th = 1.0f;
  return t;
}

void lab_thumbs_release(void) {
  if (!g_thumbs_pack)
    return;
  int dd = lab_draw_suspend();
  if (N.th_release)
    N.th_release(g_jni_env, g_thumbs);
  lab_draw_resume(dd);
  jni_release(g_thumbs_pack);
  g_thumbs_pack = NULL;
}
