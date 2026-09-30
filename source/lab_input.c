/* lab_input.c -- controllers, the touchscreen and the motion sensors.
 *
 * PLAYERS. Player 1 is the attached Joy-Cons (handheld) or controller No. 1;
 * player 2 (local play) is No. 2. Each is read for what it is, every frame
 * (a Joy-Con changes style when it is taken off or split from its partner):
 *   handheld, a Joy-Con pair, a Pro Controller   as they are
 *   one Joy-Con                                  held sideways, its rail up:
 *     the left one turned anticlockwise, the right one clockwise. The console
 *     reports its stick and buttons as printed, so the turn is undone here
 *     (bombsquad_nx's reading of the same): the stick turned a quarter; the
 *     four face buttons by where they are (right A, bottom B, left Y, top X);
 *     SL / SR as L / R; its one +/- button pauses; the stick's click is ZL
 *     (motion on / off). A lone right Joy-Con's stick is the "right" one.
 *
 * TILT. On a phone the board follows the accelerometer: Accelerometer.java
 * sends onEvent([x, y, z]) in g, x toward the screen's right, y toward its
 * top, z out of it, minus a calibration (Settings > Calibrate). Here:
 *   - the left stick: x/y = the stick times [controls] stick_tilt (in g),
 *     with a small dead zone and a gentle curve for fine control, z what is
 *     left of 1 g; the D-pad tilts most of the way;
 *   - motion: the controller's accelerometer (each player's own: the
 *     console's, a Pro Controller's, either Joy-Con of a pair, a lone Joy-Con
 *     held sideways), smoothed, RELATIVE to a neutral position: flat, or
 *     where Calibrate / the - button last set it for that kind of
 *     controller (kept with the saves). The gravity vector's change from the
 *     neutral one is measured along the controller's x axis and the axis
 *     across it in the neutral plane, so any comfortable angle works and the
 *     sensor's sign convention does not matter;
 *   - both: motion plus the stick.
 * In the rotated layouts the axes turn with the picture; so do the stick's
 * and the motion's in a level turned to fill the screen (lab_gfx.c).
 *
 * A/B swapped when asked. Touches are mapped into the portrait picture (dp),
 * or the iPad menus' points. Rumble: HD rumble on each player's controller.
 * MIT.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "lab.h"
#include "util.h"

/* the kinds of controller a motion level is kept for */
enum { K_HANDHELD, K_PRO, K_DUAL_LEFT, K_DUAL_RIGHT, K_LEFT, K_RIGHT, K_COUNT };
static const char *const k_kinds[K_COUNT] = {"handheld", "Pro Controller", "left Joy-Con (of a pair)",
                                             "right Joy-Con (of a pair)", "left Joy-Con (alone)",
                                             "right Joy-Con (alone)"};

typedef struct {
  PadState pad;
  HidNpadIdType id;
  /* motion: the handles of each style it can be */
  HidSixAxisSensorHandle six_hh, six_pro, six_dual[2], six_left, six_right;
  int six_ok;
  float acc[3];         /* smoothed gravity, in the held controller's frame */
  int acc_ok, acc_fresh;
  float neutral[3];
  int have_neutral, calibrate_req, kind;
  /* the stick's pseudo-buttons as seen, last frame */
  u64 prev_stick;
  u64 last_style;
  u64 rumble_until;
} Player;

static Player P[2] = {{.id = HidNpadIdType_No1, .kind = -1}, {.id = HidNpadIdType_No2, .kind = -1}};

static int is_single(u64 st) {
  return (st & (HidNpadStyleTag_NpadJoyLeft | HidNpadStyleTag_NpadJoyRight)) &&
         !(st & (HidNpadStyleTag_NpadHandheld | HidNpadStyleTag_NpadJoyDual | HidNpadStyleTag_NpadFullKey));
}

/* a vector in a lone Joy-Con's own frame (x right, y up, held upright) ->
 * the sideways hold's (the player's) */
static void sideways(u64 st, float *x, float *y) {
  float cx = *x, cy = *y;
  if (st & HidNpadStyleTag_NpadJoyLeft) { /* turned anticlockwise */
    *x = -cy;
    *y = cx;
  } else { /* the right one, clockwise */
    *x = cy;
    *y = -cx;
  }
}

static void player_init(int i) {
  Player *pl = &P[i];
  if (i == 0)
    padInitializeDefault(&pl->pad); /* No. 1 and the attached Joy-Cons */
  else
    padInitialize(&pl->pad, pl->id);
  Result r[5] = {0, 0, 0, 0, 0};
  if (i == 0)
    r[0] = hidGetSixAxisSensorHandles(&pl->six_hh, 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
  r[1] = hidGetSixAxisSensorHandles(&pl->six_pro, 1, pl->id, HidNpadStyleTag_NpadFullKey);
  r[2] = hidGetSixAxisSensorHandles(pl->six_dual, 2, pl->id, HidNpadStyleTag_NpadJoyDual);
  r[3] = hidGetSixAxisSensorHandles(&pl->six_left, 1, pl->id, HidNpadStyleTag_NpadJoyLeft);
  r[4] = hidGetSixAxisSensorHandles(&pl->six_right, 1, pl->id, HidNpadStyleTag_NpadJoyRight);
  int ok = 1;
  for (int k = 0; k < 5; k++)
    ok &= R_SUCCEEDED(r[k]);
  if (ok) {
    if (i == 0)
      hidStartSixAxisSensor(pl->six_hh);
    hidStartSixAxisSensor(pl->six_pro);
    hidStartSixAxisSensor(pl->six_dual[0]);
    hidStartSixAxisSensor(pl->six_dual[1]);
    hidStartSixAxisSensor(pl->six_left);
    hidStartSixAxisSensor(pl->six_right);
    pl->six_ok = 1;
  } else {
    debugPrintf("[input] player %d: no motion sensors (%x %x %x %x %x)\n", i + 1, r[0], r[1], r[2], r[3], r[4]);
  }
}

/* ---- presses between frames
 * A frame can take most of a second (a level's picture drawn by the engine,
 * a level loading), and a press made and let go inside it was never seen:
 * the frame after found the button up again (hardware 2026-09-27: after a
 * download, Play had to be pressed twice). A thread looks at the buttons
 * every few milliseconds and keeps each press until the next frame takes
 * it. */
static volatile u64 g_latched[2];
static Thread g_sampler;

static void sampler(void *arg) {
  (void)arg;
  PadState ps[2];
  padInitializeDefault(&ps[0]);
  padInitialize(&ps[1], HidNpadIdType_No2);
  for (;;) {
    for (int i = 0; i < 2; i++) {
      padUpdate(&ps[i]);
      u64 d = padGetButtonsDown(&ps[i]);
      if (d)
        __atomic_or_fetch(&g_latched[i], d, __ATOMIC_RELAXED);
    }
    svcSleepThread(4000000ll);
  }
}

static u64 take_latched(int player) { return __atomic_exchange_n(&g_latched[player & 1], 0, __ATOMIC_RELAXED); }

int lab_test_active(void); /* lab_test.c: a scripted run (emulator tests) */
int lab_test_p2(void);
void lab_test_pad(int pl, u64 *held, float *lx, float *ly);
void lab_test_frame(void);
void lab_test_init(void);

void lab_input_init(void) {
  lab_test_init();
  /* two players (local play); a lone player is player 1 as before */
  padConfigureInput(2, HidNpadStyleSet_NpadStandard);
  for (int i = 0; i < 2; i++)
    player_init(i);
  if (R_FAILED(threadCreate(&g_sampler, sampler, NULL, NULL, 0x2000, 0x2C, -2)) || R_FAILED(threadStart(&g_sampler)))
    debugPrintf("[input] no button sampler: presses inside a long frame may be missed\n");
  hidInitializeTouchScreen();
  static const char *const tilts[] = {"left stick", "motion", "motion + stick"};
  debugPrintf("[input] tilt: %s; motion sensors %s; one Joy-Con a player: held sideways\n", tilts[dcr_config()->tilt],
              P[0].six_ok ? "ready" : "unavailable");
}

/* ------------------------------------------------------------ motion */
/* the player's controller's six-axis state; its x/y made the handheld's way
 * (the Pro Controller's sensor frame is turned half a turn from the
 * Joy-Cons'), a lone Joy-Con's turned to how it is held */
static int read_six(Player *pl, float a[3], int *kind) {
  if (!pl->six_ok)
    return 0;
  const u64 st = padGetStyleSet(&pl->pad);
  HidSixAxisSensorState s;
  memset(&s, 0, sizeof s);
  int got = 0;
  float flip = 1.0f;
  if (st & HidNpadStyleTag_NpadHandheld) {
    if (pl != &P[0])
      return 0;
    *kind = K_HANDHELD;
    got = hidGetSixAxisSensorStates(pl->six_hh, &s, 1) > 0;
  } else if (st & HidNpadStyleTag_NpadFullKey) {
    flip = -1.0f;
    *kind = K_PRO;
    got = hidGetSixAxisSensorStates(pl->six_pro, &s, 1) > 0;
  } else if (st & HidNpadStyleTag_NpadJoyDual) {
    const u64 attr = padGetAttributes(&pl->pad);
    if (attr & HidNpadAttribute_IsRightConnected) {
      *kind = K_DUAL_RIGHT;
      got = hidGetSixAxisSensorStates(pl->six_dual[1], &s, 1) > 0;
    } else if (attr & HidNpadAttribute_IsLeftConnected) {
      *kind = K_DUAL_LEFT;
      got = hidGetSixAxisSensorStates(pl->six_dual[0], &s, 1) > 0;
    }
  } else if (st & HidNpadStyleTag_NpadJoyLeft) {
    *kind = K_LEFT;
    got = hidGetSixAxisSensorStates(pl->six_left, &s, 1) > 0;
  } else if (st & HidNpadStyleTag_NpadJoyRight) {
    *kind = K_RIGHT;
    got = hidGetSixAxisSensorStates(pl->six_right, &s, 1) > 0;
  }
  if (!got)
    return 0;
  a[0] = s.acceleration.x * flip, a[1] = s.acceleration.y * flip, a[2] = s.acceleration.z;
  if (*kind == K_LEFT || *kind == K_RIGHT)
    sideways(st, &a[0], &a[1]);
  return 1;
}

/* The level position: flat, or where the player last calibrated this kind
 * of controller (kept with the saves -- a sensor that reads a few degrees
 * off when lying flat is put right once, not every launch). */
static void level_key(char *out, size_t cap, int kind, char axis) {
  snprintf(out, cap, "port-motion-level-%d-%c", kind, axis);
}

static void load_level(Player *pl, int kind, const float *a) {
  char kx[48], ky[48], kz[48];
  level_key(kx, sizeof kx, kind, 'x'), level_key(ky, sizeof ky, kind, 'y'), level_key(kz, sizeof kz, kind, 'z');
  float x = lab_reg_get_float(kx, 0), y = lab_reg_get_float(ky, 0), z = lab_reg_get_float(kz, 0);
  float n = sqrtf(x * x + y * y + z * z);
  int who = (int)(pl - P) + 1;
  if (n > 0.5f && fabsf(z / n) > 0.5f) {
    pl->neutral[0] = x / n, pl->neutral[1] = y / n, pl->neutral[2] = z / n;
    debugPrintf("[input] player %d motion level (%s): as calibrated, %.3f %.3f %.3f\n", who, k_kinds[kind],
                pl->neutral[0], pl->neutral[1], pl->neutral[2]);
  } else {
    /* flat: gravity straight through the controller's face (these read -1
     * g on z lying face up; one that reads +1 is taken the other way) */
    pl->neutral[0] = pl->neutral[1] = 0;
    pl->neutral[2] = a[2] > 0.3f ? 1.0f : -1.0f;
    debugPrintf("[input] player %d motion level (%s): flat (z %+.0f; reads %.3f %.3f %.3f now)\n", who,
                k_kinds[kind], pl->neutral[2], a[0], a[1], a[2]);
  }
  pl->have_neutral = 1;
}

static void save_level(Player *pl, int kind) {
  char k[48];
  static const char ax[3] = {'x', 'y', 'z'};
  for (int i = 0; i < 3; i++) {
    level_key(k, sizeof k, kind, ax[i]);
    lab_reg_set_float(k, pl->neutral[i]);
  }
  lab_reg_save();
}

static void motion_poll(Player *pl) {
  float a[3];
  int kind = 0;
  pl->acc_fresh = 0;
  if (!read_six(pl, a, &kind))
    return;
  float m = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  if (m < 0.2f || m > 3.0f)
    return; /* shaken, or no reading */
  if (!pl->acc_ok || kind != pl->kind) {
    memcpy(pl->acc, a, sizeof pl->acc);
    pl->acc_ok = 1;
    if (kind != pl->kind) { /* the first reading, or another controller */
      pl->kind = kind;
      load_level(pl, kind, a);
    }
  } else {
    for (int i = 0; i < 3; i++)
      pl->acc[i] += (a[i] - pl->acc[i]) * 0.35f;
  }
  pl->acc_fresh = 1;
  if (pl->calibrate_req) {
    float n = sqrtf(pl->acc[0] * pl->acc[0] + pl->acc[1] * pl->acc[1] + pl->acc[2] * pl->acc[2]);
    for (int i = 0; i < 3; i++)
      pl->neutral[i] = pl->acc[i] / n;
    pl->have_neutral = 1;
    pl->calibrate_req = 0;
    save_level(pl, kind);
    debugPrintf("[input] player %d motion level (%s): calibrated, kept: %.3f %.3f %.3f\n", (int)(pl - P) + 1,
                k_kinds[kind], pl->neutral[0], pl->neutral[1], pl->neutral[2]);
  }
}

void lab_input_calibrate(void) {
  P[0].calibrate_req = 1;
  if (lab_versus_active())
    P[1].calibrate_req = 1;
}

/* local play: one player's controller, as it is held now, is level */
void lab_input_calibrate_player(int player) { P[player & 1].calibrate_req = 1; }

/* the rotated layouts: a vector in the console's frame (x right, y up) as
 * the viewer sees it */
static void to_viewer(float *x, float *y) {
  int layout = dcr_config()->layout;
  float cx = *x, cy = *y;
  if (layout == LAB_LAYOUT_ROTATED_LEFT) { /* the console's left side up */
    *x = cy;
    *y = -cx;
  } else if (layout == LAB_LAYOUT_ROTATED_RIGHT) {
    *x = -cy;
    *y = cx;
  }
}

static float curve(float v) {
  const float dz = 0.10f;
  float a = fabsf(v);
  if (a < dz)
    return 0;
  a = (a - dz) / (1.0f - dz);
  if (a > 1)
    a = 1;
  a = 0.35f * a + 0.65f * a * a; /* finer near the middle */
  return v < 0 ? -a : a;
}

/* the stick (the D-pad all the way) in g */
static void stick_part(const LabPad *p, float *x, float *y) {
  const DcrConfig *c = dcr_config();
  float sx = curve(p->lx), sy = curve(p->ly);
  float dx = (float)(!!(p->held & HidNpadButton_Right) - !!(p->held & HidNpadButton_Left));
  float dy = (float)(!!(p->held & HidNpadButton_Up) - !!(p->held & HidNpadButton_Down));
  if (dx || dy) {
    float l = sqrtf(dx * dx + dy * dy);
    sx = dx / l * 0.75f, sy = dy / l * 0.75f;
  }
  *x += sx * c->stick_tilt;
  *y += sy * c->stick_tilt;
}

/* the motion's tilt (the viewer's frame) in g; 0 if there is none */
static int motion_part(Player *pl, float *x, float *y) {
  const DcrConfig *c = dcr_config();
  if (!pl->acc_ok)
    return 0;
  if (!pl->have_neutral) {
    pl->calibrate_req = 1;
    return 0;
  }
  float n[3] = {pl->neutral[0], pl->neutral[1], pl->neutral[2]};
  float m = sqrtf(pl->acc[0] * pl->acc[0] + pl->acc[1] * pl->acc[1] + pl->acc[2] * pl->acc[2]);
  float a[3] = {pl->acc[0] / m, pl->acc[1] / m, pl->acc[2] / m};
  /* bx: the controller's x axis in the neutral plane; by: across it,
   * toward the controller's +y */
  float bx[3] = {1 - n[0] * n[0], -n[0] * n[1], -n[0] * n[2]};
  float bl = sqrtf(bx[0] * bx[0] + bx[1] * bx[1] + bx[2] * bx[2]);
  if (bl <= 1e-3f)
    return 0;
  for (int i = 0; i < 3; i++)
    bx[i] /= bl;
  float by[3] = {n[1] * bx[2] - n[2] * bx[1], n[2] * bx[0] - n[0] * bx[2], n[0] * bx[1] - n[1] * bx[0]};
  if (by[1] < 0)
    for (int i = 0; i < 3; i++)
      by[i] = -by[i];
  float d[3] = {a[0] - n[0], a[1] - n[1], a[2] - n[2]};
  float tx = d[0] * bx[0] + d[1] * bx[1] + d[2] * bx[2];
  float ty = d[0] * by[0] + d[1] * by[1] + d[2] * by[2];
  /* which way gravity is read: the neutral faces up, so its z sign tells
   * (-1: the sensor reads gravity; +1: the reaction to it) */
  float k = n[2] < 0 ? -1.0f : 1.0f;
  float mx = -k * tx * c->motion_gain, my = -k * ty * c->motion_gain;
  to_viewer(&mx, &my); /* the console's frame (the stick already is the viewer's) */
  *x += mx;
  *y += my;
  return 1;
}

static void finish_tilt(float x, float y, float out[3]) {
  float l2 = x * x + y * y;
  if (l2 > 1.0f) {
    float l = sqrtf(l2);
    x /= l, y /= l, l2 = 1.0f;
  }
  out[0] = x, out[1] = y, out[2] = sqrtf(1.0f - l2);
}

void lab_input_tilt(const LabPad *p, float out[3]) {
  const DcrConfig *c = dcr_config();
  float x = 0, y = 0;
  if (c->tilt != LAB_TILT_MOTION)
    stick_part(p, &x, &y);
  if (c->tilt != LAB_TILT_STICK)
    motion_part(&P[0], &x, &y);
  /* a level turned to fill the screen: the way the player pushes (or
   * tilts) is the way the ball goes on the screen */
  lab_gfx_view_to_board(&x, &y);
  finish_tilt(x, y, out);
}

/* local play: a player's board (upright), from their stick and, with motion
 * on, their controller's motion */
void lab_input_player_tilt(int player, const LabPad *p, float out[3]) {
  const DcrConfig *c = dcr_config();
  float x = 0, y = 0;
  if (c->tilt != LAB_TILT_MOTION)
    stick_part(p, &x, &y);
  if (c->tilt != LAB_TILT_STICK)
    motion_part(&P[player & 1], &x, &y);
  finish_tilt(x, y, out);
}

void lab_input_stick_tilt(const LabPad *p, float out[3]) {
  float x = 0, y = 0;
  stick_part(p, &x, &y);
  finish_tilt(x, y, out);
}

/* ------------------------------------------------------------ buttons */
/* a lone Joy-Con's buttons, as the sideways hold has them */
static u64 single_buttons(u64 st, u64 b) {
  u64 o = b & ~(HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y | HidNpadButton_Up |
                HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_Minus |
                HidNpadButton_StickL | HidNpadButton_StickR);
  if (st & HidNpadStyleTag_NpadJoyLeft) {
    /* its arrows: Down at the right, Left at the bottom, Up at the left,
     * Right at the top */
    if (b & HidNpadButton_Down) o |= HidNpadButton_A;
    if (b & HidNpadButton_Left) o |= HidNpadButton_B;
    if (b & HidNpadButton_Up) o |= HidNpadButton_Y;
    if (b & HidNpadButton_Right) o |= HidNpadButton_X;
  } else {
    /* X at the right, A at the bottom, B at the left, Y at the top */
    if (b & HidNpadButton_X) o |= HidNpadButton_A;
    if (b & HidNpadButton_A) o |= HidNpadButton_B;
    if (b & HidNpadButton_B) o |= HidNpadButton_Y;
    if (b & HidNpadButton_Y) o |= HidNpadButton_X;
  }
  /* SL / SR under the index fingers: L / R; its one of + and -: + (the
   * pause); the stick's click: ZL (motion on / off) */
  if (b & (HidNpadButton_LeftSL | HidNpadButton_RightSL)) o |= HidNpadButton_L;
  if (b & (HidNpadButton_LeftSR | HidNpadButton_RightSR)) o |= HidNpadButton_R;
  if (b & (HidNpadButton_Minus | HidNpadButton_Plus)) o |= HidNpadButton_Plus;
  if (b & (HidNpadButton_StickL | HidNpadButton_StickR)) o |= HidNpadButton_ZL;
  return o;
}

static u64 swap_ab(u64 x) {
  const u64 ab = HidNpadButton_A | HidNpadButton_B;
  return (x & ~ab) | ((x & HidNpadButton_A) ? HidNpadButton_B : 0) | ((x & HidNpadButton_B) ? HidNpadButton_A : 0);
}

static const char *style_name(u64 st) {
  if (st & HidNpadStyleTag_NpadHandheld) return "the console (handheld)";
  if (st & HidNpadStyleTag_NpadFullKey) return "Pro Controller";
  if (st & HidNpadStyleTag_NpadJoyDual) return "Joy-Con pair";
  if (st & HidNpadStyleTag_NpadJoyLeft) return "left Joy-Con";
  if (st & HidNpadStyleTag_NpadJoyRight) return "right Joy-Con";
  if (st) return "a controller";
  return NULL;
}

/* one player's buttons and sticks, as they are held */
static void read_player(Player *pl, LabPad *out, int rotate_layout) {
  memset(out, 0, sizeof *out);
  padUpdate(&pl->pad);
  const u64 st = padGetStyleSet(&pl->pad);
  if (st != pl->last_style) {
    pl->last_style = st;
    debugPrintf("[input] player %d: %s\n", (int)(pl - P) + 1, style_name(st) ? style_name(st) : "none");
  }
  /* what is held, and what was pressed since the last frame (let go again) */
  u64 held = padGetButtons(&pl->pad) | take_latched((int)(pl - P));
  static u64 prev_held[2];
  const int single = is_single(st);
  if (single)
    held = single_buttons(st, held);
  if (dcr_config()->swap_ab)
    held = swap_ab(held);
  float tlx = -2, tly = 0;
  lab_test_pad((int)(pl - P), &held, &tlx, &tly); /* a scripted run's (nothing otherwise) */
  /* the D-pad turns with a rotated picture (menus move the way you see them) */
  if (rotate_layout && dcr_config()->layout != LAB_LAYOUT_PORTRAIT) {
    const u64 dirs[4] = {HidNpadButton_Up, HidNpadButton_Right, HidNpadButton_Down, HidNpadButton_Left};
    int shift = dcr_config()->layout == LAB_LAYOUT_ROTATED_LEFT ? 1 : 3; /* console up = viewer's right / left */
    u64 r = held & ~(dirs[0] | dirs[1] | dirs[2] | dirs[3]);
    for (int d = 0; d < 4; d++)
      if (held & dirs[d])
        r |= dirs[(d + shift) % 4];
    held = r;
  }
  /* the sticks: a lone right Joy-Con's is its "right" one */
  const int right_alone = single && (st & HidNpadStyleTag_NpadJoyRight);
  HidAnalogStickState ls = padGetStickPos(&pl->pad, right_alone ? 1 : 0);
  HidAnalogStickState rs = single ? (HidAnalogStickState){0, 0} : padGetStickPos(&pl->pad, 1);
  out->lx = (float)ls.x / 32767.0f, out->ly = (float)ls.y / 32767.0f;
  out->rx = (float)rs.x / 32767.0f, out->ry = (float)rs.y / 32767.0f;
  if (single)
    sideways(st, &out->lx, &out->ly);
  if (tlx > -2)
    out->lx = tlx, out->ly = tly;
  if (rotate_layout) {
    to_viewer(&out->lx, &out->ly);
    to_viewer(&out->rx, &out->ry);
  }
  /* the left stick's pseudo-buttons from the stick as the viewer holds it
   * (the console's are in the controller's own frame) */
  const u64 sm = HidNpadButton_StickLLeft | HidNpadButton_StickLUp | HidNpadButton_StickLRight |
                 HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRUp |
                 HidNpadButton_StickRRight | HidNpadButton_StickRDown;
  u64 s = 0;
  if (out->lx < -0.5f) s |= HidNpadButton_StickLLeft;
  if (out->lx > 0.5f) s |= HidNpadButton_StickLRight;
  if (out->ly > 0.5f) s |= HidNpadButton_StickLUp;
  if (out->ly < -0.5f) s |= HidNpadButton_StickLDown;
  held = (held & ~sm) | s;
  int who = (int)(pl - P) & 1;
  out->held = held;
  out->down = held & ~prev_held[who];
  out->up = prev_held[who] & ~held;
  prev_held[who] = held;
}

/* ------------------------------------------------------------ polling */
void lab_input_poll(LabPad *out) {
  static int was_touch;
  lab_test_frame();
  read_player(&P[0], out, 1);

  HidTouchScreenState ts = {0};
  int n = hidGetTouchScreenStates(&ts, 1) ? ts.count : 0;
  if (n > 0) {
    out->touch = 1;
    if (lab_gfx_showing_hd()) {
      /* the iPad menus: points on the whole screen */
      lab_gfx_touch_to_canvas((float)ts.touches[0].x, (float)ts.touches[0].y, &out->tx, &out->ty);
      out->touch_in = 1;
    } else {
      out->touch_in = lab_gfx_touch_to_dp((float)ts.touches[0].x, (float)ts.touches[0].y, &out->tx, &out->ty);
    }
  }
  out->touch_began = out->touch && !was_touch;
  out->touch_ended = !out->touch && was_touch;
  was_touch = out->touch;

  if (dcr_config()->tilt != LAB_TILT_STICK || P[0].calibrate_req)
    motion_poll(&P[0]);
  out->accel_ok = P[0].acc_fresh;
  memcpy(out->accel, P[0].acc, sizeof P[0].acc);

  for (int i = 0; i < 2; i++)
    if (P[i].rumble_until && armGetSystemTick() > P[i].rumble_until) {
      P[i].rumble_until = 0;
      lab_input_rumble_player(i, 0, 0);
    }
}

/* ------------------------------------------------------------ local play */
static int g_local;

void lab_input_local_play(int on) {
  if (on == g_local)
    return;
  g_local = on;
  /* a Joy-Con each, held sideways (the controller screen offers that) */
  hidSetNpadJoyHoldType(on ? HidNpadJoyHoldType_Horizontal : HidNpadJoyHoldType_Vertical);
  debugPrintf("[input] local play %s\n", on ? "on: Joy-Cons held sideways" : "off");
}

const char *lab_input_player_name(int player) { return style_name(padGetStyleSet(&P[player & 1].pad)); }

int lab_input_p2_connected(void) {
  padUpdate(&P[1].pad);
  return padGetStyleSet(&P[1].pad) != 0 || lab_test_p2();
}

void lab_input_poll_p2(LabPad *out) {
  read_player(&P[1], out, 0);
  if (dcr_config()->tilt != LAB_TILT_STICK || P[1].calibrate_req)
    motion_poll(&P[1]);
  out->accel_ok = P[1].acc_fresh;
  memcpy(out->accel, P[1].acc, sizeof P[1].acc);
}

/* ------------------------------------------------------------ rumble */
/* the rumble handles of a player's controller (by its style), made once */
static int player_vib(int player, HidVibrationDeviceHandle *h, int *n) {
  static HidVibrationDeviceHandle cache[2][5][2];
  static int made[2][5];
  u64 st = padGetStyleSet(&P[player].pad);
  static const u32 tags[5] = {HidNpadStyleTag_NpadHandheld, HidNpadStyleTag_NpadFullKey,
                              HidNpadStyleTag_NpadJoyDual, HidNpadStyleTag_NpadJoyLeft,
                              HidNpadStyleTag_NpadJoyRight};
  int k = -1;
  for (int i = 0; i < 5 && k < 0; i++)
    if (st & tags[i])
      k = i;
  if (k < 0 || (k == 0 && player))
    return 0;
  int cnt = k >= 3 ? 1 : 2;
  if (!made[player][k]) {
    HidNpadIdType id = k == 0 ? HidNpadIdType_Handheld : P[player].id;
    made[player][k] = R_SUCCEEDED(hidInitializeVibrationDevices(cache[player][k], cnt, id, tags[k])) ? 1 : -1;
  }
  if (made[player][k] < 0)
    return 0;
  memcpy(h, cache[player][k], sizeof(HidVibrationDeviceHandle) * (size_t)cnt);
  *n = cnt;
  return 1;
}

void lab_input_rumble_player(int player, float strength, int ms) {
  if (player < 0)
    player = 0;
  player &= 1;
  if (!dcr_config()->rumble && strength > 0)
    return;
  HidVibrationDeviceHandle h[2];
  int n = 0;
  if (!player_vib(player, h, &n))
    return;
  HidVibrationValue v[2];
  for (int i = 0; i < 2; i++) {
    v[i].amp_low = strength;
    v[i].freq_low = 160.0f;
    v[i].amp_high = strength * 0.6f;
    v[i].freq_high = 320.0f;
  }
  hidSendVibrationValues(h, v, n);
  P[player].rumble_until = ms > 0 ? armGetSystemTick() + armNsToTicks((u64)ms * 1000000ull) : 0;
}

void lab_input_rumble(float strength, int ms) { lab_input_rumble_player(0, strength, ms); }
