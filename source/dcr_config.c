/* dcr_config.c -- <game folder>/config.ini, the user's settings: Labyrinth
 * 2's options, on the runtime's INI engine (runtime/source/rt_cfg.c).
 *
 * Written with every option, its default and a line of explanation on the
 * first start; an existing file is appended to (options a newer build adds,
 * at the end, with their defaults), so edits and comments survive updates.
 * Plain INI: [section], key = value, # comments; booleans take true/false,
 * yes/no, on/off, 1/0. Read once at start-up: changes apply the next time the
 * game starts. The rows, their order, defaults and help text are the ones
 * this port always wrote, so a player's config.ini reads as before; the file
 * header is the runtime's ("# Labyrinth 2 for Switch -- settings."), the
 * same words. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

static DcrConfig g_cfg = {
    .tilt = LAB_TILT_STICK,
    .stick_tilt = 0.5f,
    .motion_gain = 1.0f,
    .pointer_speed = 5.0f,
    .rumble = 1,
    .supersample = 2,
    .side_panels = 1,
    .res_w = 1280,
    .res_h = 720,
    .boost = 1,
    .online = 1,
    .level_layout = LAB_LAYOUT_ROTATED_LEFT,
};

const DcrConfig *dcr_config(void) { return &g_cfg; }
void dcr_config_set_tilt(int tilt) {
  if (tilt >= LAB_TILT_STICK && tilt <= LAB_TILT_BOTH)
    g_cfg.tilt = tilt;
}

void dcr_config_set_level_layout(int layout) {
  /* upright, or turned with the board's top at the left (its top at the
   * right showed the board upside down to the player: not offered) */
  if (layout >= LAB_LAYOUT_PORTRAIT && layout <= LAB_LAYOUT_ROTATED_RIGHT)
    g_cfg.level_layout = layout == LAB_LAYOUT_PORTRAIT ? LAB_LAYOUT_PORTRAIT : LAB_LAYOUT_ROTATED_LEFT;
}

/* dst NULL: derived in apply() (level_layout, supersample, device_id), or
 * the runtime's own (resolution: rt_config()). */
static const CfgOpt k_opts[] = {
    CFG_ROW_SWAP_AB("Swap A and B (true: B selects, A goes back).", &g_cfg.swap_ab),
    {"controls", "tilt", "stick",
     "What tilts the board. stick: the left stick (the D-pad tilts all the way).\n"
     "# motion: the controller's motion sensors -- hold it (or the console, in\n"
     "# handheld) flat like the board, and tilt it; Settings > Calibrate sets\n"
     "# the level position. both: motion, and the stick adds to it. The game's\n"
     "# Settings screen changes this too, and ZL in a level (motion on / off);\n"
     "# then it is kept with the saves.",
     CFG_CHOICE, "stick,motion,both", &g_cfg.tilt},
    {"controls", "stick_tilt", "0.5",
     "How far the board tilts with the stick all the way over, in g (the pull\n"
     "# a phone tilted 30 degrees gives the ball): 0.1 to 1.",
     CFG_FLOAT, NULL, &g_cfg.stick_tilt, 0.1f, 1.0f},
    {"controls", "motion_sensitivity", "1.0",
     "Motion tilt: 1 = as far as the controller is tilted, 2 = twice as far\n"
     "# (0.25 to 4).",
     CFG_FLOAT, NULL, &g_cfg.motion_gain, 0.25f, 4.0f},
    {"controls", "pointer_speed", "5",
     "Speed of the pointer on the game's own screens (pause, level end), which\n"
     "# are touch screens: 1 to 20.",
     CFG_FLOAT, NULL, &g_cfg.pointer_speed, 1.0f, 20.0f},
    {"controls", "rumble", "true",
     "Rumble when the ball drops into a hole, reaches the goal or hits a bumper.", CFG_BOOL, NULL,
     &g_cfg.rumble},
    CFG_ROW_RESOLUTION("720", CFG_HELP_RESOLUTION),
    {"display", "layout", "portrait",
     "The game is a portrait (upright) phone game. portrait: upright in the\n"
     "# middle of the screen, the side panels beside it. rotated_left /\n"
     "# rotated_right: turned a quarter to fill the screen, for a console held\n"
     "# upright (its left / right side at the top; touch and tilt turn with it).",
     CFG_CHOICE, "portrait,rotated_left,rotated_right", &g_cfg.layout},
    {"display", "level_layout", "rotated_left",
     "With layout = portrait: how a level is shown while it is played (the\n"
     "# menus stay upright). rotated_left: the board turned a quarter to fill\n"
     "# the screen, its top at the left (the stick and motion turn with it; the\n"
     "# game's pause and end-of-level screens turn back upright to be read).\n"
     "# portrait: upright, as the menus. ZR in a level changes it (kept with\n"
     "# the saves).",
     CFG_CHOICE, "portrait,rotated_left", NULL},
    {"display", "supersample", "auto",
     "Draw the game at twice the size and scale it down (smoother edges).\n"
     "# auto: at 720p yes, at 1080p no. true / false.",
     CFG_TEXT, NULL, NULL},
    {"display", "menus", "ipad",
     "The menus: ipad = Labyrinth 2 HD's (landscape; needs the iPad game's .ipa\n"
     "# in this folder: its pictures, its iPad level packs); android = the phone's\n"
     "# (portrait).",
     CFG_CHOICE, "ipad,android", &g_cfg.menus},
    {"display", "side_panels", "true",
     "Portrait layout: the blue background and the controls beside the game\n"
     "# (false: black).",
     CFG_BOOL, NULL, &g_cfg.side_panels},
    {"online", "enabled", "true",
     "The Labyrinth 2 level server: Download levels (the community's level\n"
     "# packs, their ratings) and Create (your own packs from the web editor\n"
     "# at labyrinth2.com, and publishing them). false: offline.",
     CFG_BOOL, NULL, &g_cfg.online},
    {"online", "device_id", "",
     "Leave empty. The server keeps one account (your ID and PIN) per device;\n"
     "# the port makes this console's id once (from its serial number, else its\n"
     "# user, never shared with another console) and keeps it in\n"
     "# data/device_id. To use the account of another device, put its id here\n"
     "# (8 to 16 hex digits, e.g. a phone's Android ID).",
     CFG_TEXT, NULL, g_cfg.device_id, 0, 0, sizeof g_cfg.device_id},
    CFG_ROW_BOOST(CFG_HELP_BOOST, &g_cfg.boost),
    CFG_ROW_GL_SELFTEST(&g_cfg.gl_selftest),
    CFG_ROW_BOOT_LOG(CFG_HELP_BOOT_LOG, &g_cfg.boot_log),
    CFG_ROW_LOG_JNI(CFG_HELP_LOG_JNI, &g_cfg.log_jni),
    {"debug", "log_touches", "false",
     "Write every touch the game gets (its 320x480 coordinates) to debug.log.", CFG_BOOL, NULL,
     &g_cfg.log_touch},
};

/* index of the value in a comma-separated list, -1 if it is not one */
static int choice_of(const char *choices, const char *v) {
  const char *c = choices;
  for (int idx = 0; *c; idx++) {
    const char *e = strchr(c, ',');
    size_t n = e ? (size_t)(e - c) : strlen(c);
    if (strlen(v) == n && !strncasecmp(v, c, n))
      return idx;
    if (!e)
      break;
    c = e + 1;
  }
  return -1;
}

static void apply(void) {
  const RtConfig *rt = rt_config();
  g_cfg.res_w = rt->res_w;
  g_cfg.res_h = rt->res_h;
  /* a level_layout that is none of its choices is portrait, as this port
   * always read it (the engine's rows fall back to the default instead) */
  const char *ll = rt_config_get("display", "level_layout");
  int lli = choice_of("portrait,rotated_left", ll);
  if (lli < 0) {
    if (strcasecmp(ll, "rotated_left"))
      debugPrintf("[config] level_layout = %s: not one of portrait,rotated_left, using rotated_left\n", ll);
    lli = 0;
  }
  dcr_config_set_level_layout(lli);
  const char *ss = rt_config_get("display", "supersample");
  if (!strcasecmp(ss, "auto"))
    g_cfg.supersample = g_cfg.res_h <= 720 ? 2 : 1;
  else
    g_cfg.supersample = rt_config_bool("display", "supersample") ? 2 : 1;

  static const char *const tilts[] = {"stick", "motion", "both"};
  static const char *const layouts[] = {"portrait", "rotated left", "rotated right"};
  int docked = appletGetOperationMode() == AppletOperationMode_Console;
  debugPrintf("[config] %dx%d (%s, %s), %s (levels %s), supersample %dx, side panels %s; A/B %s; tilt %s "
              "(stick %.2f g, motion x%.2f), pointer %.0f; CPU boost %s\n",
              g_cfg.res_w, g_cfg.res_h, rt_config_get("display", "resolution"), docked ? "docked" : "handheld",
              layouts[g_cfg.layout], layouts[g_cfg.level_layout], g_cfg.supersample,
              g_cfg.side_panels ? "on" : "off", g_cfg.swap_ab ? "swapped" : "normal", tilts[g_cfg.tilt],
              (double)g_cfg.stick_tilt, (double)g_cfg.motion_gain, (double)g_cfg.pointer_speed,
              g_cfg.boost ? "on" : "off");
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
