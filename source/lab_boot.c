/* lab_boot.c -- the app: what StartupActivity and the rest did, and the
 * frame loop.
 *
 * Android's order, and what happens here:
 *   System.loadLibrary          the engine's constructors (lab_loader.c),
 *                               JNI_OnLoad (the JNI version)
 *   ea.a(context)               FileSystemUtil.setResourcePath(files dir),
 *                               ZRegistry/LevelsDB/SoundManager/Accelerometer
 *                               init (lab_java.c)
 *   the first LevelsDB.e()      the files dir and the level table: already
 *                               made by lab_files.c before the engine loaded
 *   activities                  the rebuilt screens (lab_ui.c, lab_screens.c)
 *   GameActivity                a game (lab_game.c), drawn by this thread
 * One thread does it all, as the GLSurfaceView's GL thread did for the
 * engine: input, the game's frame or the menus, the composite, the present
 * (vsync, 60 Hz). The sound mixer has its own thread (lab_audio.c).
 *
 * HOME: the game's pause overlay opens (if no popup is up), the sound
 * pauses, the saves are written; Android's onPause destroyed the GL surface
 * and restarted the level on return, which the port need not do. The system
 * freezes the process for HOME and sleep: the focus messages come when it
 * runs again, and a freeze the clocks saw opens the pause overlay as well
 * (the runtime's rt_applet.c calls port_focus_lost / port_focus_gained /
 * port_process_frozen). Closing: the statistics and the saves, then the
 * process ends. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "dcr_apkcache.h"
#include "dcr_boost.h"
#include "dcr_config.h"
#include "error.h"
#include "gl_layer.h"
#include "jni.h"
#include "lab.h"
#include "lab_online.h"
#include "lab_ui.h"
#include "rt_applet.h"
#include "util.h"
#include "watchdog.h"

typedef jint (*fn_onload)(void *vm, void *reserved);

static uint64_t g_frames;

void lab_request_exit(void) { rt_request_exit(); }
uint64_t lab_frame_count(void) { return g_frames; }

/* the watchdog: frames presented */
uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }

/* ------------------------------------------------------------- lifecycle */
static void save_all(void) {
  lab_reg_save();
  lab_levels_save();
}

/* back to a paused level, not a rolling ball */
static void pause_play(void) {
  if (lab_game_active() && lab_game_popup_open() == 0)
    lab_game_show_menu();
  if (lab_versus_active())
    lab_versus_set_paused(1); /* local play too */
}

/* A freeze (sleep, HOME) the focus messages may not have told of: both can
 * come at once on waking, and leave the focus as it was. */
void port_process_frozen(unsigned count) {
  (void)count;
  if (lab_game_active() || lab_versus_active()) {
    debugPrintf("[boot] the process was frozen: the level pauses\n");
    pause_play();
  }
}

/* Android's onPause / onResume (the runtime then stops / restarts the
 * game's clocks) */
void port_focus_lost(void) {
  pause_play();
  lab_audio_pause(1);
  save_all();
}

void port_focus_gained(void) { lab_audio_pause(0); }

static void exit_guard(void *arg) {
  (void)arg;
  svcSleepThread(5000000000ll);
  debugPrintf("[boot] the game did not close within 5 s: ending the process\n");
  log_flush_ring();
  svcExitProcess();
}

static void exit_guard_start(void) {
  static Thread t;
  if (R_FAILED(threadCreate(&t, exit_guard, NULL, NULL, 0x4000, 0x2B, -2)) || R_FAILED(threadStart(&t)))
    debugPrintf("[boot] no exit backstop thread\n");
}

static void report(void) {
  static u64 last_tick;
  static unsigned long last_presented;
  u64 tick = armGetSystemTick();
  unsigned long presented = (unsigned long)dcr_gl_frames();
  double fps = last_tick ? (double)(presented - last_presented) * 1e9 / (double)armTicksToNs(tick - last_tick) : 0.0;
  last_tick = tick;
  last_presented = presented;
  debugPrintf("[boot] %lu frames presented (%.1f fps), %lu audio mixes, %d Java objects%s\n", presented, fps,
              (unsigned long)lab_audio_mixes(), jni_live_objects(), lab_game_active() ? ", in a game" : "");
  dcr_boost_report();
  dcr_apkcache_report();
  lab_apk_report();
}

static void housekeeping(int *launch_done, unsigned long *quiet_at, u64 *last_report) {
  dcr_boost_poll();
  unsigned long frames = (unsigned long)dcr_gl_frames();
  if (!*launch_done && frames > 0) {
    *launch_done = 1;
    dcr_boost_launch_end();
    debugPrintf("[boot] first frame presented\n");
    *quiet_at = frames + 180;
  }
  /* From ~3 s after the first picture the log goes to a RAM ring (util.c),
   * written out every 10 s and by the watchdog. */
  if (*quiet_at && frames >= *quiet_at) {
    *quiet_at = 0;
    log_set_quiet(1);
  }
  u64 now = armGetSystemTick();
  if (armTicksToNs(now - *last_report) >= 10000000000ull) {
    *last_report = now;
    report();
    log_flush_ring();
  }
  lab_reg_tick();
}

int lab_boot_run(void) {
  lab_java_init();
  dcr_watchdog_start();
  rt_watchdog_add_counter("audio mixes", lab_audio_mixes);

  /* ---- System.loadLibrary: JNI_OnLoad ---- */
  fn_onload onload = (fn_onload)so_try_find_addr_rx(&g_mod_game, "JNI_OnLoad");
  if (onload)
    debugPrintf("[boot] JNI_OnLoad -> 0x%lx\n", (unsigned long)onload(g_jni_vm, NULL));

  /* ---- ea.a(context) ---- */
  lab_java_boot_natives();
  int t = lab_reg_get_int("port-tilt", -1); /* the Settings screen's choice */
  if (t >= 0)
    dcr_config_set_tilt(t);
  int ll = lab_reg_get_int("port-level-layout", -1); /* ZR's in a level */
  if (ll >= 0)
    dcr_config_set_level_layout(ll);

  /* ---- the screen, the sound, the controls, the menus ---- */
  if (lab_gfx_init() != 0)
    fatal_error("Could not set up the graphics (EGL / OpenGL ES 1): see debug.log.");
  lab_font_init();
  lab_audio_init();
  lab_input_init();
  lab_ui_init();
  debugPrintf("[boot] up; this thread draws the frames now\n");
  log_flush_ring();

  u64 last_report = armGetSystemTick();
  int launch_done = 0;
  unsigned long quiet_at = 0;
  while (!rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll();
    if (!rt_focused()) {
      svcSleepThread(50000000ll);
      continue;
    }
    LabPad pad;
    lab_input_poll(&pad);
    lab_online_poll(); /* the level server's answers, to the screens */
    if (lab_versus_active() && ui_top() == SCR_VERSUS) {
      /* local play: both boards, then the race's screen over them */
      lab_versus_frame(&pad);
      lab_ui_frame(&pad);
    } else if (lab_game_active() && ui_top() == SCR_GAME) {
      float tilt[3] = {0, 0, 1};
      /* paused (or a level's end): the stick moves between the buttons, the
       * board lies still */
      if (!lab_ui_game_paused())
        lab_input_tilt(&pad, tilt);
      lab_game_tilt(tilt[0], tilt[1], tilt[2]);
      scr_frame(SCR_GAME); /* the side panel's lines */
      lab_game_frame();
      if (lab_game_active() && ui_top() == SCR_GAME)
        lab_ui_game_frame(&pad);
      else
        lab_ui_frame(NULL); /* the game ended in that frame: its screen now */
    } else {
      lab_ui_frame(&pad);
    }
    /* a level full screen while it is played, paused, and at its end (the
     * engine turns its overlays to read upright: lab_game.c); upright for
     * the menus */
    /* Settings over a level (its pause screen's): with the iPad menus, over
     * the level's picture as it is; the phone's Settings are a screen */
    int over = lab_hd_on() && lab_game_active() && ui_top() == SCR_SETTINGS && scr_settings_over_game();
    int in_game = lab_game_active() && (ui_top() == SCR_GAME || over);
    int rot = 0;
    if (in_game)
      rot = lab_gfx_level_turned() ? 90 : 0;
    lab_gfx_show_ipad(in_game && lab_game_ipad());
    lab_gfx_show_hd(lab_hd_on() && !(lab_game_active() && ui_top() == SCR_GAME));
    lab_gfx_show_hd_over(over);
    /* a level starting: turned at once -- its Loading... stays on the screen
     * while the level loads (a second), and read up it when the turn had only
     * begun (hardware 2026-09-28) */
    if (in_game && lab_game_loading())
      lab_gfx_set_rotation_now(rot);
    else
      lab_gfx_set_rotation(rot);
    lab_gfx_present();
    g_frames++;
    lab_applets_pump(); /* the keyboard / browser a screen asked for (they block) */
    housekeeping(&launch_done, &quiet_at, &last_report);
    if (lab_ui_quit_requested()) {
      debugPrintf("[boot] quit from the main menu\n");
      break;
    }
  }

  /* ---- the way out ---- */
  log_set_quiet(0);
  rt_applet_stop(); /* the clocks run again if the game was in the background */
  exit_guard_start();
  if (lab_versus_active())
    lab_versus_end();
  if (lab_game_active())
    lab_game_end(); /* onPause: the statistics are saved */
  lab_audio_stop_all();
  save_all();
  debugPrintf("[boot] the game has closed\n");
  log_flush_ring();
  return 0;
}
