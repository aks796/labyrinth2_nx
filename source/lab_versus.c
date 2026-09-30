/* lab_versus.c -- local play: two players, a board each, the same level.
 *
 * The iPhone game raced up to four phones over Wi-Fi or Bluetooth (the
 * engine still has its "GAME OVER!" / "YOU WON!" titles and the players'
 * coloured dots); the Android build left the network out. Here two copies of
 * the engine (lab_loader.c, lab_game.c) play the pack side by side on one
 * screen, each fed its own player's stick (a Joy-Con each, held sideways, or
 * two controllers): a countdown, then the first ball in the goal wins the
 * level -- "YOU WON!" over that board, "GAME OVER!" over the other -- and
 * both go on to the next one; the wins are counted beside the boards. The
 * saves are not touched, but for the pack's multiplayer level (where the
 * next race starts, as the iPhone's currentmultiplayerlevel) and player 1's
 * statistics (lab_java.c). + (or -) pauses: resume, the level again, or
 * leave. An iPad pack races on two iPad boards: the iPad copy of the engine
 * twice (copies 2 and 3, lab_loader.c), 3:4 surfaces. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "dcr_time.h"
#include "lab.h"
#include "lab_gl.h"
#include "lab_ui.h"
#include "util.h"

enum { PH_LOAD, PH_COUNT, PH_PLAY, PH_RESULT, PH_OVER };

enum { REQ_NONE, REQ_START, REQ_LEVEL, REQ_PACK, REQ_QUIT };

static struct {
  int on;
  int e0; /* player 1's copy of the engine (0, or 2 on iPad boards); player 2's is e0 + 1 */
  int req; /* from the race's screen, done at the next frame's start (not inside the menus' drawing) */
  LabPack pack;
  int level, wins[2], phase, winner, paused;
  u64 t0;
  LabPad p2;
  int ready_frames;
} V;

static const uint32_t k_color[2] = {0xe4a624ffu, 0x3eab67ffu}; /* the game's first two dots */

static float since(void) { return (float)armTicksToNs(armGetSystemTick() - V.t0) * 1e-9f; }

static void key_for(const LabPack *p, char *out, size_t cap) { snprintf(out, cap, "port-mp-level-%s", p->id); }
static void level_key(char *out, size_t cap) { key_for(&V.pack, out, cap); }

int lab_versus_pack_level(const LabPack *p) {
  char k[160];
  key_for(p, k, sizeof k);
  int l = lab_reg_get_int(k, 0);
  return l >= 0 && l < p->nlevels ? l : 0;
}

void lab_versus_score(int *p1, int *p2) { *p1 = V.wins[0], *p2 = V.wins[1]; }

static void lab_versus_side(int ww, int wh);
static void board_overlay(int eng, float x, float y, float w, float h);
static void hold(int on);

int lab_versus_active(void) { return V.on; }

static void begin_level(void) {
  V.phase = PH_LOAD;
  V.winner = -1;
  V.ready_frames = 0;
  g_lab_race_level = V.level;
  char k[160];
  level_key(k, sizeof k);
  lab_reg_set_int(k, V.level);
}

/* The race's screen is up (SCR_VERSUS): its first frame loads the second
 * engine and both boards (not inside the menus' drawing, where Start was
 * pressed). */
int lab_versus_start(const LabPack *p) {
  memset(&V, 0, sizeof V);
  V.pack = *p;
  V.on = 1;
  V.req = REQ_START;
  return 0;
}

static int do_start(void) {
  const LabPack *p = &V.pack;
  V.e0 = p->ipad ? 2 : 0;
  g_lab_race_p1 = V.e0;
  if ((p->ipad ? lab_load_ipad_race_engine() : lab_load_second_engine()) != 0) {
    ui_toast("Local play: the second engine could not be loaded (debug.log)");
    return -1;
  }
  if (lab_gfx_race_on(1, p->ipad) != 0) {
    ui_toast("Local play: no room for two boards (debug.log)");
    return -1;
  }
  V.level = lab_versus_pack_level(p);
  V.pack.current = V.level;
  g_lab_race = 1;
  g_lab_race_level = V.level;
  lab_input_local_play(1);
  lab_gfx_race_side_hook = lab_versus_side;
  lab_gfx_race_board_hook = board_overlay;
  for (int i = 0; i < 2; i++)
    if (lab_game_eng_start(V.e0 + i, &V.pack) != 0) {
      ui_toast("Local play could not start (debug.log)");
      return -1;
    }
  begin_level();
  debugPrintf("[versus] %s \"%s\": local play from level %d (player 1: %s, player 2: %s)\n", p->id, p->name,
              V.level + 1, lab_input_player_name(0) ? lab_input_player_name(0) : "none",
              lab_input_player_name(1) ? lab_input_player_name(1) : "none");
  return 0;
}

void lab_versus_end(void) {
  hold(0);
  for (int i = 0; i < 2; i++)
    lab_game_eng_end(V.e0 + i);
  lab_audio_stop_all();
  g_lab_race = 0;
  lab_input_local_play(0);
  lab_gfx_race_on(0, 0);
  lab_reg_save();
  if (V.on)
    debugPrintf("[versus] over: player 1 %d, player 2 %d\n", V.wins[0], V.wins[1]);
  V.on = 0;
}

/* both boards at V.level (the engines ask LevelsDB for it: g_lab_race_level first) */
static void load_level(void) {
  g_lab_race_level = V.level;
  for (int i = 0; i < 2; i++)
    lab_game_eng_restart(V.e0 + i, V.level);
  begin_level();
}

void lab_versus_restart_level(void) { V.req = REQ_LEVEL; }
void lab_versus_restart_pack(void) { V.req = REQ_PACK; }
void lab_versus_quit(void) { V.req = REQ_QUIT; }

void lab_versus_set_paused(int on) {
  if (V.paused && !on) {
    u64 now = armGetSystemTick();
    if (V.phase == PH_COUNT)
      V.t0 = now; /* the countdown again */
    else if (V.phase == PH_RESULT)
      V.t0 = now - armNsToTicks(2000000000ull); /* a second more of the titles */
  }
  V.paused = on;
}

/* The pause holds the engines' clock (bionic_time.c: they see no time go
 * by, and no step as long as the pause) and their sounds (the rolling). */
static int g_held;
static void hold(int on) {
  if (on) {
    dcr_time_suspend();
    lab_audio_pause(1);
    g_held = 1;
  } else if (g_held) {
    dcr_time_resume();
    lab_audio_pause(0);
    g_held = 0;
  }
}
int lab_versus_paused(void) { return V.paused; }

/* ------------------------------------------------------------ the boards */
/* The overlays go on in the composite, over each board's picture (window
 * pixels; the board's dp mapped to them): its surface keeps the engine's
 * picture alone, as it is shown again while the board waits. */
static float B_x, B_y, B_k;
#define BX(v) (B_x + (v) * B_k)
#define BY(v) (B_y + (v) * B_k)
#define BS(v) ((v) * B_k)

static void title(int won, float y) {
  /* overlay-titles-multiplayer.png (512 x 128): "GAME OVER!" in 322 x 44
   * at the top left, "YOU WON!" in 258 x 45 under it */
  const LabTex *t = lab_tex("assets/textures/common/overlay-titles-multiplayer.png");
  if (!t)
    return;
  float pw = won ? 258 : 322, py = won ? 47 : 0, ph = 45, h = 38, w = h * pw / ph;
  lab_draw_rrect(BX(160 - w * 0.5f - 18), BY(y - 16), BS(w + 36), BS(h + 32), BS(14), 0x00000090u);
  lab_draw_image_uv(t, BX(160 - w * 0.5f), BY(y), BS(w), BS(h), 0, py / 128.0f, pw / 512.0f, (py + ph) / 128.0f,
                    0xffffffffu);
}

/* overlay-multiplayer-dots.png (256 x 64): 37 px dots, 40 px apart */
static void dot(float cx, float cy, float r, int player) {
  const LabTex *t = lab_tex("assets/textures/common/overlay-multiplayer-dots.png");
  if (!t) {
    lab_draw_rrect(cx - r, cy - r, 2 * r, 2 * r, r, k_color[player]);
    return;
  }
  float u0 = (float)(40 * player) / 256.0f;
  lab_draw_image_uv(t, cx - r, cy - r, 2 * r, 2 * r, u0, 0, u0 + 37.0f / 256.0f, 37.0f / 64.0f, 0xffffffffu);
}

static void board_overlay(int eng, float x, float y, float w, float h) {
  (void)h;
  /* the board's 320 x 480 overlay space (on an iPad board: in the middle of
   * its 360, as the engine's own overlays) */
  B_k = w / (V.pack.ipad ? 360.0f : 320.0f);
  B_x = x + (V.pack.ipad ? 20.0f * B_k : 0.0f), B_y = y + (h - 480.0f * B_k) * 0.5f;
  if (V.phase == PH_COUNT) {
    float t = since();
    int n = 3 - (int)t;
    char s[8];
    snprintf(s, sizeof s, "%d", n > 0 ? n : 1);
    float k = 1.0f - fmodf(t, 1.0f);
    lab_draw_rrect(BX(110), BY(180), BS(100), BS(100), BS(50), 0x00000080u);
    lab_draw_text(BX(160), BY(252), BS(64 + 10 * k), 0xffffffffu, LAB_CENTER, 1, s);
  } else if (V.phase == PH_PLAY && since() < 0.8f) {
    lab_draw_text(BX(160), BY(250), BS(54), 0xffffff00u | (uint32_t)(255 * (1.0f - since() / 0.8f)), LAB_CENTER, 1,
                  "GO!");
  } else if (V.phase == PH_RESULT || V.phase == PH_OVER) {
    int won = V.phase == PH_OVER ? V.wins[eng] > V.wins[eng ^ 1] : V.winner == eng;
    int tie = V.phase == PH_OVER && V.wins[0] == V.wins[1];
    if (tie) {
      lab_draw_rrect(BX(40), BY(184), BS(240), BS(70), BS(14), 0x00000090u);
      lab_draw_text(BX(160), BY(232), BS(34), 0xf2ebdaffu, LAB_CENTER, 1, "A DRAW!");
    } else {
      title(won, 200);
    }
  }
}

/* before the menus' frame (lab_boot.c): both engines, then what goes on
 * their boards */
void lab_versus_frame(const LabPad *p1) {
  if (!V.on)
    return;
  int req = V.req;
  V.req = REQ_NONE;
  if (req == REQ_START && do_start() != 0) {
    lab_versus_end();
    return;
  }
  if (req == REQ_QUIT) {
    lab_versus_end(); /* the race's screen goes next frame */
    return;
  }
  if (req == REQ_PACK) {
    V.level = 0;
    V.wins[0] = V.wins[1] = 0;
  }
  if (req == REQ_LEVEL || req == REQ_PACK) {
    V.paused = 0;
    load_level();
  }
  hold(V.paused); /* again each frame: the HOME menu's return lets them go */
  lab_input_poll_p2(&V.p2);
  const LabPad *pads[2] = {p1, &V.p2};
  int playing = V.phase == PH_PLAY && !V.paused;
  for (int i = 0; i < 2; i++) {
    float t[3] = {0, 0, 1};
    if (playing)
      lab_input_player_tilt(i, pads[i], t);
    lab_game_eng_tilt(V.e0 + i, t[0], t[1], t[2]);
    int render = !V.paused && V.phase != PH_RESULT && V.phase != PH_OVER;
    lab_game_eng_frame(V.e0 + i, render);
  }
  /* each board: the other player's ball, as a ghost (the iPad's local
   * multiplayer showed the others' balls on yours) */
  for (int i = 0; i < 2; i++)
    lab_ghost_mirror(V.e0 + i, V.e0 + (i ^ 1), V.phase != PH_RESULT && V.phase != PH_OVER);
  switch (V.phase) {
  case PH_LOAD:
    /* both levels loaded (their "Loading..." frames past) */
    if (++V.ready_frames > 3) {
      V.phase = PH_COUNT;
      V.t0 = armGetSystemTick();
    }
    break;
  case PH_COUNT:
    if (!V.paused && since() >= 3.0f) {
      V.phase = PH_PLAY;
      V.t0 = armGetSystemTick();
    }
    break;
  case PH_PLAY:
    for (int i = 0; i < 2 && V.winner < 0; i++)
      if (lab_game_eng_popup(V.e0 + i) == 2) { /* its level's end: this ball is in the goal */
        V.winner = i;
        V.wins[i]++;
        V.phase = PH_RESULT;
        V.t0 = armGetSystemTick();
        lab_input_rumble_player(i, 0.6f, 300);
        debugPrintf("[versus] level %d: player %d wins (%d - %d)\n", V.level + 1, i + 1, V.wins[0], V.wins[1]);
      }
    break;
  case PH_RESULT:
    if (!V.paused && since() >= 3.0f) {
      if (V.level + 1 >= V.pack.nlevels) {
        V.phase = PH_OVER; /* the pack is played through */
        V.t0 = armGetSystemTick();
        char k[160];
        level_key(k, sizeof k);
        lab_reg_set_int(k, 0);
      } else {
        V.level++;
        load_level();
      }
    }
    break;
  default:
    break;
  }
  lab_gfx_begin_portrait();
}

/* ------------------------------------------------------------ beside them */
static void panel_text(float cx, float y, float size, uint32_t c, int bold, const char *s) {
  lab_draw_text(cx, y, size, c, LAB_CENTER, bold, s);
}

static void lab_versus_side(int ww, int wh) {
  const LabTex *bg = lab_tex("main_menu2_bg");
  float s = (float)wh / 720.0f;
  if (bg) {
    float bw = (float)ww, bh = bw * (float)bg->h / (float)bg->w;
    lab_draw_image(bg, 0, ((float)wh - bh) * 0.5f, bw, bh, 0xb8c8ccffu);
  } else {
    lab_draw_rect(0, 0, (float)ww, (float)wh, 0x3d8aa0ffu);
  }
  for (int i = 0; i < 2; i++) {
    float x, y, w, h;
    lab_gfx_race_rect(i, &x, &y, &w, &h);
    for (int k = 0; k < 6; k++) {
      uint32_t a = (uint32_t)(60 - k * 9);
      lab_draw_rect(x - (float)(k + 1) * 2 * s, 0, 2 * s, (float)wh, a);
      lab_draw_rect(x + w + (float)k * 2 * s, 0, 2 * s, (float)wh, a);
    }
    /* the margin beside each board: its player, the wins */
    float mx = i ? x + w : 0, mw = i ? (float)ww - (x + w) : x, cx = mx + mw * 0.5f;
    float cy = (float)wh * 0.40f;
    float r = 18 * s;
    dot(cx, cy, r, i);
    char t[32];
    snprintf(t, sizeof t, "Player %d", i + 1);
    panel_text(cx, cy + r + 32 * s, 18 * s, 0xfff4e6ffu, 1, t);
    snprintf(t, sizeof t, "%d", V.wins[i]);
    panel_text(cx, cy + r + 90 * s, 46 * s, 0xffffffffu, 1, t);
    panel_text(cx, cy + r + 116 * s, 13 * s, 0xe8f4f4ffu, 0, V.wins[i] == 1 ? "win" : "wins");
    if (i == 0) {
      float nh = lab_draw_text_box(mx + 10 * s, (float)wh * 0.08f, mw - 20 * s, 15 * s, 0xfff4e6ffu, LAB_CENTER, 1,
                                   V.pack.name);
      snprintf(t, sizeof t, "Level %d of %d", V.level + 1, V.pack.nlevels);
      panel_text(cx, (float)wh * 0.08f + nh + 16 * s, 13 * s, 0xe8f4f4ffu, 0, t);
    } else {
      panel_text(cx, (float)wh * 0.12f, 15 * s, 0xfff4e6ffu, 1, "+  Pause");
    }
  }
}

/* ------------------------------------------------------------ the pause */
/* the pads' pause (either player's + or -), for the screen (lab_screens.c) */
int lab_versus_pause_pressed(const LabPad *p1) {
  const u64 k = HidNpadButton_Plus | HidNpadButton_Minus;
  return ((p1 ? p1->down : 0) | V.p2.down) & k ? 1 : 0;
}

int lab_versus_phase_over(void) { return V.on && V.phase == PH_OVER && since() > 1.5f; }

/* a player's ZL this frame (the pause's calibration; player 1's pad is the menus') */
int lab_versus_zl_pressed(int player) {
  const LabPad *p = player ? &V.p2 : ui_pad;
  return p && (p->down & HidNpadButton_ZL) ? 1 : 0;
}
