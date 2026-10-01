/* lab_main.c -- Labyrinth 2's part of the boot sequence.
 *
 * The runtime's main.c (runtime/source/main.c) does what every port does:
 * the old folder's move, the log, config.ini, the clocks, HOME / sleep, an
 * update from the launcher NRO, the APK (any *.apk that is Labyrinth 2's,
 * game.apk first), the self-tests. Here is what is Labyrinth 2's:
 *   port_load    the library and classes.txt from the APK (the setup plan
 *                below), the APK's files, the iPad game's files, the saves,
 *                the engine's files dir, then the library loaded;
 *   port_run     the engine's constructors, then the game (lab_boot.c);
 *   port_path_fixup  the iPad engine's files dir over the phone one's. MIT.
 */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "config.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "lab.h"
#include "util.h"

/* ------------------------------------------------------------ the setup
 * liblabyrinthii.so and classes.txt from the APK when they are missing or it
 * has changed (the .setup stamps keep their names); the bar goes on in
 * lab_files.c (the iPad copy 150-400, the game's files 400-850, the iPad
 * packs 850-1000), then "Starting the game" (port_load). */
static const char *const k_libs[] = {LAB_LIB_GAME};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 1,
    .apk_requirement = "This port needs Labyrinth 2 " LAB_VERSION " (" PORT_PACKAGE ", armeabi):\n"
                       "put the APK of that version in the game folder.",
    .libs_p0 = 0,
    .libs_p1 = 100,
    .classes_p0 = 100,
    .classes_p1 = 150,
    .finish_in_port = 1,
};

const char *port_apk_help(void) {
  return "Copy the APK of your own Labyrinth 2 (" PORT_PACKAGE " " LAB_VERSION ")\n"
         "there, under any name ending in .apk: the game's library, levels,\n"
         "pictures and sounds come from it.";
}

/* ------------------------------------------------------------ the paths
 * The iPad engine's resource dir (data/files-ipad, lab_files.c) holds only
 * what the iPad game has of its own; everything else is the phone engine's
 * (data/files), read from there. */
const char *port_path_fixup(const char *r, char *out, size_t cap) {
  const char *k = strstr(r, "/data/files-ipad/");
  if (!k)
    return r;
  struct stat st;
  if (stat(r, &st) == 0)
    return r;
  char tmp[DCR_PATH_MAX];
  snprintf(tmp, sizeof tmp, "%.*s/data/files/%s", (int)(k - r), r, k + 17);
  snprintf(out, cap, "%s", tmp);
  return out;
}

/* ------------------------------------------------------------ the iPad files
 * The player's own .ipa if there is one (game.ipa, else any *.ipa), else
 * the part of it the launcher NRO carries, copied to data/ipad.ipa (the
 * iPad menus, its level packs: lab_hd*.c, lab_files.c). "" for none. */
static int has_ext(const char *n, const char *ext) {
  size_t a = strlen(n), b = strlen(ext);
  return a > b && !strcasecmp(n + a - b, ext) && !(n[0] == '.' && n[1] == '_'); /* not macOS's "._" files */
}

static void find_ipa(char *ipa, size_t cap) {
  const char *root = dcr_game_root();
  snprintf(ipa, cap, "%s/game.ipa", root);
  FILE *t = fopen(ipa, "rb");
  if (t) {
    fclose(t);
    return;
  }
  ipa[0] = 0;
  DIR *d = opendir(root);
  struct dirent *de;
  while (d && (de = readdir(d))) {
    if (has_ext(de->d_name, ".ipa")) {
      snprintf(ipa, cap, "%s/%s", root, de->d_name);
      break;
    }
  }
  if (d)
    closedir(d);
  if (ipa[0])
    return;
  /* none of the player's own: the iPad files the NRO carries (the same
   * data/ipad.ipa.from stamp as before, so nothing is copied again) */
  if (rt_setup_copy_from_nro("ipad.ipa", "data/ipad.ipa", NULL, "Copying the iPad game's menus and levels", 150,
                             400) > 0) {
    rt_root_path(ipa, cap, "data/ipad.ipa");
    debugPrintf("[boot] the iPad game's files: the NRO's (%s)\n", ipa);
  }
}

/* ------------------------------------------------------------ boot */
int port_load(const char *apk) {
  dcr_setup_from_apk(apk);
  if (lab_apk_init(apk) != 0)
    fatal_error("Could not read the game's files from %s.\n\n"
                "Is it the APK of Labyrinth 2 (it must hold assets/ and res/raw/)?", apk);
  char ipa[512];
  find_ipa(ipa, sizeof ipa);
  if (ipa[0])
    lab_ipa_init(ipa);
  else
    debugPrintf("[boot] no .ipa, and no NRO here with the iPad files: the Android game's menus\n");
  /* the saves (the game's registry and level table), then the engine's
   * files dir: assets and official level packs, the SD card's packs */
  lab_reg_load();
  lab_levels_load();
  lab_files_setup();
  rt_setup_finish();
  if (lab_load_module() != 0)
    fatal_error("Could not load the game library from %s.\n\n"
                "It is unpacked from the APK (" LAB_ABI_DIR ") on launch: delete\n"
                LAB_LIB_GAME " and .setup there to unpack it again.",
                dcr_game_root());
  return 0;
}

/* System.loadLibrary: the engine's constructors, then (lab_boot.c) the
 * managers' natives and the menus. */
void port_run(void) {
  lab_run_constructors();
  lab_boot_run();
}
