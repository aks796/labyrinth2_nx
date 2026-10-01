/* lab_applet.c -- the Switch's own keyboard and web browser, for the online
 * screens (the search field; the level editor's page), and the local time.
 *
 * Both applets block until closed, so a screen only asks for one; it is
 * shown after the frame has been presented (lab_applets_pump from the frame
 * loop), with the sound paused, and the answer goes to the screen's
 * callback. The browser is the full WebApplet (the port always runs as an
 * application), with the Wi-Fi login browser as the fallback, as
 * sm127_nx's web_applet.c. MIT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "lab.h"
#include "rt_applet.h"
#include "util.h"

static struct {
  int kind; /* 0 none, 1 keyboard, 2 browser, 3 the controllers */
  char header[64], guide[96], initial[64];
  int maxlen;
  LabKbdDone cb;
  void *arg;
  char url[0x400];
} Q;

void lab_kbd_request(const char *header, const char *guide, const char *initial, int maxlen, LabKbdDone cb,
                     void *arg) {
  if (Q.kind)
    return;
  Q.kind = 1;
  snprintf(Q.header, sizeof Q.header, "%s", header ? header : "");
  snprintf(Q.guide, sizeof Q.guide, "%s", guide ? guide : "");
  snprintf(Q.initial, sizeof Q.initial, "%s", initial ? initial : "");
  Q.maxlen = maxlen > 0 && maxlen < 60 ? maxlen : 60;
  Q.cb = cb;
  Q.arg = arg;
}

int lab_web_request(const char *url) {
  if (Q.kind || !url || strncmp(url, "http", 4) || strlen(url) >= sizeof Q.url)
    return -1;
  Q.kind = 2;
  snprintf(Q.url, sizeof Q.url, "%s", url);
  return 0;
}

/* Local play: the console's controller screen for two players (a Joy-Con
 * each, held sideways, or any controllers); cb(1) when two are there. */
static void (*g_ctl_cb)(int ok);
int lab_controllers_request(void (*cb)(int ok)) {
  if (Q.kind)
    return -1;
  Q.kind = 3;
  g_ctl_cb = cb;
  return 0;
}

static void show_controllers(void) {
  HidLaControllerSupportArg arg;
  hidLaCreateControllerSupportArg(&arg);
  arg.hdr.player_count_min = 2;
  arg.hdr.player_count_max = 2;
  arg.hdr.enable_permit_joy_dual = 1;
  arg.hdr.enable_single_mode = 0;
  arg.hdr.enable_identification_color = 1;
  /* the players' colours: the game's own dots (yellow, green) */
  arg.identification_color[0] = (HidLaControllerSupportArgColor){0xe4, 0xa6, 0x24, 0xff};
  arg.identification_color[1] = (HidLaControllerSupportArgColor){0x3e, 0xab, 0x67, 0xff};
  arg.enable_explain_text = 1;
  hidLaSetExplainText(&arg, "Player 1: the left board", HidNpadIdType_No1);
  hidLaSetExplainText(&arg, "Player 2: the right board", HidNpadIdType_No2);
  HidLaControllerSupportResultInfo info;
  memset(&info, 0, sizeof info);
  Result rc = hidLaShowControllerSupport(&info, &arg);
  debugPrintf("[applet] controllers: 0x%x, %d player(s)\n", rc, info.player_count);
  if (g_ctl_cb)
    g_ctl_cb(R_SUCCEEDED(rc) && info.player_count >= 2);
}

int lab_applet_pending(void) { return Q.kind != 0; }

static void show_keyboard(void) {
  SwkbdConfig kbd;
  Result rc = swkbdCreate(&kbd, 0);
  if (R_FAILED(rc)) {
    debugPrintf("[applet] no keyboard (0x%x)\n", rc);
    if (Q.cb)
      Q.cb(NULL, Q.arg);
    return;
  }
  swkbdConfigMakePresetDefault(&kbd);
  if (Q.header[0])
    swkbdConfigSetHeaderText(&kbd, Q.header);
  if (Q.guide[0])
    swkbdConfigSetGuideText(&kbd, Q.guide);
  if (Q.initial[0])
    swkbdConfigSetInitialText(&kbd, Q.initial);
  swkbdConfigSetStringLenMax(&kbd, (u32)Q.maxlen);
  char out[256] = {0};
  rc = swkbdShow(&kbd, out, sizeof out);
  swkbdClose(&kbd);
  debugPrintf("[applet] keyboard: %s\n", R_SUCCEEDED(rc) ? "entered" : "cancelled");
  if (Q.cb)
    Q.cb(R_SUCCEEDED(rc) ? out : NULL, Q.arg);
}

static void show_browser(void) {
  AppletType type = appletGetAppletType();
  int application = type == AppletType_Application || type == AppletType_SystemApplication;
  Result rc = MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);
  if (application) {
    WebCommonConfig cfg;
    rc = webPageCreate(&cfg, Q.url);
    if (R_SUCCEEDED(rc))
      rc = webConfigSetWhitelist(&cfg, "^http");
    if (R_SUCCEEDED(rc)) {
      /* the editor is a page for a mouse: the stick's pointer, and touch
       * (either may be refused on an old firmware: not needed) */
      webConfigSetPointer(&cfg, true);
      webConfigSetTouchEnabledOnContents(&cfg, true);
      rc = webConfigShow(&cfg, NULL);
    }
    debugPrintf("[applet] browser: 0x%x\n", rc);
  }
  if (!application || R_FAILED(rc)) {
    WebWifiConfig cfg;
    Uuid uuid;
    memset(&uuid, 0, sizeof uuid);
    webWifiCreate(&cfg, NULL, Q.url, uuid, 0);
    rc = webWifiShow(&cfg, NULL);
    debugPrintf("[applet] Wi-Fi login browser: 0x%x\n", rc);
  }
}

void lab_applets_pump(void) {
  int kind = Q.kind;
  if (!kind)
    return;
  lab_audio_pause(1);
  dcr_applet_busy(1); /* no frames while it is up, and nothing wrong (the watchdog) */
  if (kind == 1)
    show_keyboard();
  else if (kind == 3)
    show_controllers();
  else
    show_browser();
  dcr_applet_busy(0);
  lab_audio_pause(0);
  Q.kind = 0;
}

void lab_local_time(char *out, size_t cap) {
  u64 now = 0;
  TimeCalendarTime ct;
  TimeCalendarAdditionalInfo ai;
  if (R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &now)) &&
      R_SUCCEEDED(timeToCalendarTimeWithMyRule(now, &ct, &ai)))
    snprintf(out, cap, "%04d-%02d-%02d %02d:%02d", ct.year, ct.month, ct.day, ct.hour, ct.minute);
  else
    snprintf(out, cap, "just now");
}
