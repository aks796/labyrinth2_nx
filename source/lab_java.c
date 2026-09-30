/* lab_java.c -- the game's Java, as the engine sees it.
 *
 * liblabyrinthii.so never looks a class up by name: each manager hands
 * itself to a native init() (ZRegistry, LevelsDB, SoundManager, ZFont,
 * Accelerometer, GameActivity), which keeps a global reference and caches
 * method IDs from GetObjectClass. So the objects below are what those
 * init()s are given, and the handlers are the methods they cache:
 *
 *   ZRegistry      set/get{Int,Float,String}Value, removeValue   lab_registry.c
 *   LevelsDB       get/setCurrentLevel, get/setNbrLevelsFinished,
 *                  setMydifficulty, setMyrating                  lab_levels.c
 *   SoundManager   playSound(IIFF), stopSound(II), updateGainAndPitch(IIFF),
 *                  stopAllSounds, disableSounds, enableSounds    lab_audio.c
 *   ZFont          renderTextToTexture (+ its four int fields)   lab_text.c
 *   GameActivity   finish, showSettings, showAdPopup, finishedLevelPack
 *   FileSystemUtil dumpBuffer (a debug dump to the sdcard: nothing)
 *
 * A LevelPack is an object whose fields the engine reads (jni_set_field). MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "lab.h"
#include "util.h"

#define IL "se/illusionlabs/"
#define L2 IL "labyrinth2/"

JObj *g_zregistry, *g_levelsdb, *g_soundmgr, *g_accel, *g_ztouch, *g_game_activity;
static JObj *g_accel_e[4]; /* each copy's Accelerometer (the first's is g_accel) */
JObj *lab_java_accel(int eng) { return eng > 0 && eng < 4 && g_accel_e[eng] ? g_accel_e[eng] : g_accel; }
int g_lab_race_p1; /* local play: the copy player 1's board is */

/* Local play (lab_versus.c): two copies of the engine call the same Java.
 * g_lab_eng (lab_game.c) says which one is calling; during a race nothing
 * of it changes the saves but engine 0's statistics: the level table's
 * progress, the best times and engine 1's registry writes are kept out, and
 * the level each asks for is the race's. */
extern int g_lab_eng;
int g_lab_race;       /* a race is on */
int g_lab_race_level; /* its level */
static int race_drop_reg(const char *key) {
  return g_lab_race && (g_lab_eng != g_lab_race_p1 || !strncmp(key, "TIME_", 5) || !strncmp(key, "TIMEG_", 6) ||
                        !strncmp(key, "GHOST", 5));
}

void lab_ghost_on_best(const char *key); /* lab_ghost.c */

static jvalue h_void(JObj *self, const jvalue *a, const JMethod *m) { return jv_none(); }

/* -------------------------------------------------------------- ZRegistry */
static jvalue h_reg_set_int(JObj *self, const jvalue *a, const JMethod *m) {
  if (race_drop_reg(jni_utf(a[0].l)))
    return jv_none();
  lab_reg_set_int(jni_utf(a[0].l), a[1].i);
  lab_ghost_on_best(jni_utf(a[0].l)); /* a new ghost time: its run kept */
  return jv_none();
}
static jvalue h_reg_set_float(JObj *self, const jvalue *a, const JMethod *m) {
  if (race_drop_reg(jni_utf(a[0].l)))
    return jv_none();
  lab_reg_set_float(jni_utf(a[0].l), a[1].f);
  return jv_none();
}
static jvalue h_reg_set_string(JObj *self, const jvalue *a, const JMethod *m) {
  if (race_drop_reg(jni_utf(a[0].l)))
    return jv_none();
  lab_reg_set_string(jni_utf(a[0].l), a[1].l ? jni_utf(a[1].l) : "");
  return jv_none();
}
static jvalue h_reg_remove(JObj *self, const jvalue *a, const JMethod *m) {
  if (race_drop_reg(jni_utf(a[0].l)))
    return jv_none();
  lab_reg_remove(jni_utf(a[0].l), a[1].i);
  return jv_none();
}
static jvalue h_reg_get_int(JObj *self, const jvalue *a, const JMethod *m) {
  return jv_i(lab_reg_get_int(jni_utf(a[0].l), a[1].i));
}
static jvalue h_reg_get_float(JObj *self, const jvalue *a, const JMethod *m) {
  return jv_f(lab_reg_get_float(jni_utf(a[0].l), a[1].f));
}
static jvalue h_reg_get_string(JObj *self, const jvalue *a, const JMethod *m) {
  const char *s = lab_reg_get_string(jni_utf(a[0].l));
  return jv_l(s ? jni_str(s) : NULL); /* null when there is none, as SQL gave */
}

/* ---------------------------------------------------------------- LevelsDB */
static LabPack *pack_arg(const jvalue *a) {
  LabPack *p = lab_levels_find(jni_utf(a[0].l));
  if (!p)
    debugPrintf("[levels] the engine asked about an unknown pack %s\n", jni_utf(a[0].l));
  return p;
}
static jvalue h_db_get_current(JObj *self, const jvalue *a, const JMethod *m) {
  if (g_lab_race)
    return jv_i(g_lab_race_level);
  LabPack *p = pack_arg(a);
  return jv_i(p ? p->current : 0);
}
static jvalue h_db_get_finished(JObj *self, const jvalue *a, const JMethod *m) {
  LabPack *p = pack_arg(a);
  return jv_i(p ? p->nfinished : 0);
}
static void set_col(const jvalue *a, int which) {
  if (g_lab_race) {
    debugPrintf("[levels] local play, engine %d: column %d -> %d (not kept)\n", g_lab_eng, which, a[1].i);
    return;
  }
  LabPack *p = pack_arg(a);
  if (!p)
    return;
  int v = a[1].i, *col = which == 0 ? &p->current : which == 1 ? &p->nfinished
                       : which == 2 ? &p->mydifficulty : &p->myrating;
  if (*col != v) {
    static const char *const names[] = {"current level", "levels finished", "my difficulty", "my rating"};
    debugPrintf("[levels] %s: %s %d -> %d\n", p->id, names[which], *col, v);
    *col = v;
    lab_levels_save();
  }
}
static jvalue h_db_set_current(JObj *self, const jvalue *a, const JMethod *m) {
  set_col(a, 0);
  return jv_none();
}
static jvalue h_db_set_finished(JObj *self, const jvalue *a, const JMethod *m) {
  set_col(a, 1);
  return jv_none();
}
static jvalue h_db_set_mydifficulty(JObj *self, const jvalue *a, const JMethod *m) {
  set_col(a, 2);
  return jv_none();
}
static jvalue h_db_set_myrating(JObj *self, const jvalue *a, const JMethod *m) {
  set_col(a, 3);
  return jv_none();
}

/* ------------------------------------------------------------ SoundManager */
/* the second engine's sound sources apart from the first's (a loop is
 * tracked by its source) */
static int src(const jvalue *a) { return a[0].i ^ (g_lab_eng << 24); }

static jvalue h_snd_play(JObj *self, const jvalue *a, const JMethod *m) {
  lab_audio_play(src(a), a[1].i, a[2].f, a[3].f);
  /* rumble ([controls] rumble): what the ball feels */
  float g = a[2].f < 0 ? 0 : a[2].f > 1 ? 1 : a[2].f;
  int pl = g_lab_race ? g_lab_eng - g_lab_race_p1 : -1; /* a race: the player whose ball it is */
  /* only what happens to your ball (hardware 2026-09-28: a cannon firing
   * across the board shook the controller every second): hit by a
   * cannonball or a clown ball, bounced by a bumper, and the goal */
  switch (a[1].i) {
  case SND_BALL_CANNONBALL_COLLISION: lab_input_rumble_player(pl, 0.8f, 220); break;
  case SND_BALL_CLOWNBALL_COLLISION: lab_input_rumble_player(pl, 0.6f, 160); break;
  case SND_BUMPER_ROUND: case SND_BUMPER_TRI: lab_input_rumble_player(pl, 0.3f + 0.3f * g, 80); break;
  case SND_GOAL: lab_input_rumble_player(pl, 0.6f, 350); break;
  default: break;
  }
  return jv_none();
}
static jvalue h_snd_stop(JObj *self, const jvalue *a, const JMethod *m) {
  lab_audio_stop(src(a), a[1].i);
  return jv_none();
}
static jvalue h_snd_update(JObj *self, const jvalue *a, const JMethod *m) {
  lab_audio_update(src(a), a[1].i, a[2].f, a[3].f);
  return jv_none();
}
static jvalue h_snd_stop_all(JObj *self, const jvalue *a, const JMethod *m) {
  if (!g_lab_race) /* one player's level ending does not silence the other */
    lab_audio_stop_all();
  return jv_none();
}
static jvalue h_snd_disable(JObj *self, const jvalue *a, const JMethod *m) {
  lab_audio_enable(0);
  return jv_none();
}
static jvalue h_snd_enable(JObj *self, const jvalue *a, const JMethod *m) {
  lab_audio_enable(1);
  return jv_none();
}

/* ------------------------------------------------------------------- ZFont */
static jvalue h_font_render(JObj *self, const jvalue *a, const JMethod *m) {
  return jv_i(lab_text_render(self, jni_utf(a[0].l), jni_utf(a[1].l), a[2].i, a[3].i, a[4].i, a[5].i,
                              a[6].i, a[7].i, a[8].i));
}

/* ------------------------------------------------------------ GameActivity */
static jvalue h_act_finish(JObj *self, const jvalue *a, const JMethod *m) {
  lab_game_on_finish();
  return jv_none();
}
static jvalue h_act_settings(JObj *self, const jvalue *a, const JMethod *m) {
  lab_game_on_show_settings();
  return jv_none();
}
static jvalue h_act_finished_pack(JObj *self, const jvalue *a, const JMethod *m) {
  lab_game_on_finished_pack();
  return jv_none();
}

const JMethodDef jni_method_defs[] = {
    {IL "common/ZRegistry", "setIntValue", NULL, h_reg_set_int},
    {IL "common/ZRegistry", "setFloatValue", NULL, h_reg_set_float},
    {IL "common/ZRegistry", "setStringValue", NULL, h_reg_set_string},
    {IL "common/ZRegistry", "removeValue", NULL, h_reg_remove},
    {IL "common/ZRegistry", "getIntValue", NULL, h_reg_get_int},
    {IL "common/ZRegistry", "getFloatValue", NULL, h_reg_get_float},
    {IL "common/ZRegistry", "getStringValue", NULL, h_reg_get_string},
    {L2 "levelpack/LevelsDB", "getCurrentLevel", NULL, h_db_get_current},
    {L2 "levelpack/LevelsDB", "setCurrentLevel", NULL, h_db_set_current},
    {L2 "levelpack/LevelsDB", "getNbrLevelsFinished", NULL, h_db_get_finished},
    {L2 "levelpack/LevelsDB", "setNbrLevelsFinished", NULL, h_db_set_finished},
    {L2 "levelpack/LevelsDB", "setMydifficulty", NULL, h_db_set_mydifficulty},
    {L2 "levelpack/LevelsDB", "setMyrating", NULL, h_db_set_myrating},
    {L2 "managers/SoundManager", "playSound", NULL, h_snd_play},
    {L2 "managers/SoundManager", "stopSound", NULL, h_snd_stop},
    {L2 "managers/SoundManager", "updateGainAndPitch", NULL, h_snd_update},
    {L2 "managers/SoundManager", "stopAllSounds", NULL, h_snd_stop_all},
    {L2 "managers/SoundManager", "disableSounds", NULL, h_snd_disable},
    {L2 "managers/SoundManager", "enableSounds", NULL, h_snd_enable},
    {IL "common/ZFont", "renderTextToTexture", NULL, h_font_render},
    {L2 "activities/GameActivity", "finish", NULL, h_act_finish},
    {L2 "activities/GameActivity", "showSettings", NULL, h_act_settings},
    {L2 "activities/GameActivity", "showAdPopup", NULL, h_void},
    {L2 "activities/GameActivity", "finishedLevelPack", NULL, h_act_finished_pack},
    {L2 "util/FileSystemUtil", "dumpBuffer", NULL, h_void},
    {L2 "managers/AdRenderManager", "adClicked", NULL, h_void},
    {NULL, NULL, NULL, NULL},
};

const JFieldDef jni_field_defs[] = {
    {"android/os/Build$VERSION", "SDK_INT", NULL, 10, NULL},
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {L2 "activities/GameActivity", "android/app/Activity"},
    {"android/app/Activity", "android/content/Context"},
    {L2 "views/SurfaceView", "android/opengl/GLSurfaceView"},
    {"android/opengl/GLSurfaceView", "android/view/View"},
    {NULL, NULL},
};

const char *const jni_missing_classes[] = {NULL};

/* ================================================================ setup */
void lab_java_init(void) {
  jni_init();
  g_jni_log = dcr_config()->log_jni;
  g_zregistry = jni_singleton(IL "common/ZRegistry");
  g_levelsdb = jni_singleton(L2 "levelpack/LevelsDB");
  g_soundmgr = jni_singleton(L2 "managers/SoundManager");
  g_accel = jni_singleton(L2 "managers/Accelerometer");
  g_ztouch = jni_singleton(IL "common/ZTouch");
  g_game_activity = jni_singleton(L2 "activities/GameActivity");
}

typedef void (*fn_v)(void *env, void *thiz);
typedef void (*fn_s)(void *env, void *cls, void *s);

static void call_init_eng(int eng, const char *sym, JObj *thiz) {
  fn_v f = (fn_v)lab_native_in(eng, sym);
  if (!f) {
    debugPrintf("[java] MISSING native %s\n", sym);
    return;
  }
  f(g_jni_env, thiz);
  jni_exception_report(sym);
}

/* ea.a(context): what every activity's onCreate did before anything else. */
void lab_java_boot_natives_eng(int eng) {
  g_lab_eng = eng;
  if (eng > 0 && eng < 4 && !g_accel_e[eng])
    g_accel_e[eng] = jni_new(L2 "managers/Accelerometer");
  /* FileSystemUtil.a(context): setResourcePath(getFilesDir()) -- the Android
   * path, which the bionic shims turn into <root>/data/files */
  fn_s set_path = (fn_s)lab_native_in(eng, "Java_se_illusionlabs_labyrinth2_util_FileSystemUtil_setResourcePath");
  JObj *fsu = jni_class(L2 "util/FileSystemUtil")->obj;
  JObj *dir = jni_str(lab_eng_ipad(eng) ? "/data/data/" "se.illusionlabs.labyrinth2" "/files-ipad"
                               : "/data/data/" "se.illusionlabs.labyrinth2" "/files");
  if (set_path)
    set_path(g_jni_env, fsu, dir);
  else
    debugPrintf("[java] MISSING native FileSystemUtil.setResourcePath\n");
  jni_release(dir);
  /* ZRegistry.a(context) -> init(); LevelsDB.a(context) -> init();
   * SoundManager.a() -> initFields(); Accelerometer.a(context) -> init() */
  call_init_eng(eng, "Java_se_illusionlabs_common_ZRegistry_init", g_zregistry);
  call_init_eng(eng, "Java_se_illusionlabs_labyrinth2_levelpack_LevelsDB_init", g_levelsdb);
  call_init_eng(eng, "Java_se_illusionlabs_labyrinth2_managers_SoundManager_initFields", g_soundmgr);
  call_init_eng(eng, "Java_se_illusionlabs_labyrinth2_managers_Accelerometer_init", lab_java_accel(eng));
  g_lab_eng = 0;
  debugPrintf("[java] engine %d: managers initialised (registry, levels, sound, accelerometer)\n", eng);
}

void lab_java_boot_natives(void) { lab_java_boot_natives_eng(0); }

static void set_str(JObj *o, const char *name, const char *s) {
  JObj *v = s ? jni_str(s) : NULL;
  jni_set_field(o, name, jv_l(v), 1);
  if (v)
    jni_release(v); /* the field holds its own reference */
}

/* LevelsDB.a(id) (the list's Bundle): a LevelPack made from the row; its
 * fileName is null there, and the engine opens zipfiles/<levelId>. */
JObj *lab_java_levelpack(const LabPack *p) {
  JObj *o = jni_new(L2 "levelpack/LevelPack");
  set_str(o, "fileName", NULL);
  set_str(o, "levelId", p->id);
  set_str(o, "levelName", p->name);
  set_str(o, "authorId", p->author_id);
  set_str(o, "authorName", p->author);
  jni_set_field(o, "difficulty", jv_i(p->difficulty), 0);
  jni_set_field(o, "nbrLevels", jv_i(p->nlevels), 0);
  jni_set_field(o, "rating", jv_d(p->rating), 0);
  /* the engine has three themes (classic, metal, plastic); the iPad's BRIO
   * packs (3..) play on the classic one */
  jni_set_field(o, "theme", jv_i(p->theme >= 0 && p->theme <= 2 ? p->theme : 0), 0);
  jni_set_field(o, "revision", jv_i(p->revision), 0);
  jni_set_field(o, "published", jv_z(p->published || !p->ownlevel), 0);
  jni_set_field(o, "currentLevel", jv_i(p->current), 0);
  jni_set_field(o, "numFinished", jv_i(p->nfinished), 0);
  jni_set_field(o, "requiredVersion", jv_i(p->reqver), 0);
  jni_set_field(o, "preloaded", jv_z(p->preloaded), 0);
  jni_set_field(o, "tutorial", jv_z(p->tutorial), 0);
  jni_set_field(o, "ownlevel", jv_z(p->ownlevel), 0);
  jni_set_field(o, "mydifficulty", jv_i(p->mydifficulty), 0);
  jni_set_field(o, "myrating", jv_i(p->myrating), 0);
  jni_set_field(o, "playcount", jv_i(p->playcount), 0);
  return o;
}
