/* lab_loader.c -- loading Labyrinth 2's engine.
 *
 * The APK carries one library, lib/armeabi/liblabyrinthii.so: Illusion Labs'
 * engine and the game (libpng 1.2.34, TinyXML, minizip statically linked),
 * ARMv5TE/Thumb, soft-float. It imports bionic libc/libm, GLES 1.x, zlib,
 * liblog and libstdc++'s new/delete and guards. Java loads it with
 * System.loadLibrary (ea.a): its one constructor runs then; it has a
 * JNI_OnLoad (version only).
 *
 * Nothing in it writes code at run time, so the module is mapped and sealed
 * in one go. The only import resolved differently from the other ports: a few
 * GL entry points go to lab_gfx.c first (lab_gl_overrides), because the
 * engine's "screen" is the port's portrait surface. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "codespace.h"
#include "config.h"
#include "dcr_net.h"
#include "error.h"
#include "imports.h"
#include "lab.h"
#include "so_util.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

so_module g_mod_game, g_mod_game2, g_mod_game3, g_mod_game4;
static int g_up[4] = {1, 0, 0, 0}; /* each copy: 1 up, -1 could not be, 0 not yet */

/* 0 the game, 1 local play's second board, 2 the iPad boards (and local
 * play's first on iPad boards), 3 local play's second iPad board */
so_module *lab_module(int eng) {
  return eng == 3 ? &g_mod_game4 : eng == 2 ? &g_mod_game3 : eng ? &g_mod_game2 : &g_mod_game;
}

void *lab_native(const char *symbol) { return (void *)so_try_find_addr_rx(&g_mod_game, symbol); }

void *lab_native_in(int eng, const char *symbol) {
  if (eng < 0 || eng > 3 || g_up[eng] <= 0)
    return NULL;
  return (void *)so_try_find_addr_rx(lab_module(eng), symbol);
}

/* ------------------------------------------------------------ the iPad copy
 * The engine's world is the iPhone's board, 320 x 480 units: its camera
 * (glOrthof over it, after the tilt), the fit of that 2:3 into the surface
 * it is given (letterboxed), the scale of every mesh and position to the
 * surface's pixels, the floor's texture coordinates, the baked floor pass,
 * the bounds of what moves -- all from the constants 320.0 and 480.0 in the
 * code's literal pools. An iPad level is a board of 576 x 768 units (its
 * level0.xml says <labyrinth size="1">, which this engine never reads); with
 * those constants 576.0 and 768.0 instead, and a 3:4 surface, the same
 * engine plays it as the iPad did, every object its own size on the bigger
 * board. The constants that are the screen's, not the board's -- the game's
 * own overlays (pause, level end), the ad banner, the level's text popups --
 * stay: those keep a 320 x 480 screen (lab_gl_orthof widens it to the 3:4
 * surface). Each word is checked before it is changed (build 1.29 only). */
typedef struct {
  uint32_t at;   /* the literal's address in the library */
  float was, now;
} Patch;

static const Patch k_ipad[] = {
    {0x1d608, 480, 768}, {0x1d60c, 320, 576}, /* the fit of the board into the surface */
    {0x1d6b4, 320, 576}, {0x1d6d0, 480, 768}, /* board units -> pixels (x, y) */
    {0x1d6ec, 320, 576}, {0x1d708, 480, 768}, /* ... of the whole surface */
    {0x2dac4, 320, 576}, {0x2dac8, 480, 768}, /* meshes to the board's scale */
    {0x2e578, 320, 576}, {0x2e57c, 480, 768},
    {0x2fde0, 320, 576}, {0x2fde8, 480, 768}, /* objects drawn flat on the board */
    {0x31698, 480, 768}, {0x31730, 480, 768}, /* doors */
    {0x324c0, 320, 576}, {0x324c4, 480, 768},
    {0x33628, 320, 576}, {0x3362c, 480, 768}, /* the floor */
    {0x33a6c, 320, 576}, {0x33a70, 480, 768}, /* the floor baked into a texture */
    {0x34884, 320, 576}, {0x34888, 480, 768},
    {0x34db4, 320, 576}, {0x34db8, 480, 768}, /* the board's edges (what moves stays in) */
    {0x35bac, 320, 576}, {0x35ba8, 480, 768}, /* the floor's texture coordinates */
    {0x3c0c8, 320, 576}, {0x3c0d0, 480, 768}, /* the camera */
    {0x3c73c, 320, 576}, {0x3c744, 480, 768}, /* the camera for a snapshot of the board */
    /* the holes' map: the board drawn into a 1024 x 1024 texture the floor
     * is shaded with (the holes, their shadows), glScalef(1024/320, 1024/480)
     * -- without it an iPad board's holes fell off the map (hardware
     * 2026-09-26: no holes) */
    {0x32730, 1024.0f / 320.0f, 1024.0f / 576.0f}, {0x3272c, 1024.0f / 480.0f, 1024.0f / 768.0f},
};

static int patch_ipad(so_module *m) {
  uint8_t *img = (uint8_t *)m->load_base;
  for (unsigned i = 0; i < sizeof k_ipad / sizeof k_ipad[0]; i++) {
    float v;
    memcpy(&v, img + k_ipad[i].at, 4);
    if (v != k_ipad[i].was) {
      debugPrintf("[boot] the iPad engine: 0x%05x is %g, not %g -- not the 1.29 library\n", (unsigned)k_ipad[i].at,
                  (double)v, (double)k_ipad[i].was);
      return -1;
    }
  }
  for (unsigned i = 0; i < sizeof k_ipad / sizeof k_ipad[0]; i++)
    memcpy(img + k_ipad[i].at, &k_ipad[i].now, 4);
  debugPrintf("[boot] the iPad engine: %u constants for the 576 x 768 board\n",
              (unsigned)(sizeof k_ipad / sizeof k_ipad[0]));
  return 0;
}

static int load_into_ex(so_module *m, int (*patch)(so_module *));
static int load_into(so_module *m) { return load_into_ex(m, NULL); }

static int load_into_ex(so_module *m, int (*patch)(so_module *)) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), LAB_LIB_GAME);
  int rc = so_load(m, path, NULL, SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(m);
  /* the shim table with the GL redirects in front (the first match wins) */
  int n = lab_gl_overrides_count + dcr_imports_count;
  DynLibFunction *tab = malloc(sizeof *tab * (size_t)n);
  if (!tab)
    return -1;
  memcpy(tab, lab_gl_overrides, sizeof *tab * (size_t)lab_gl_overrides_count);
  memcpy(tab + lab_gl_overrides_count, dcr_imports, sizeof *tab * (size_t)dcr_imports_count);
  int missing = so_resolve(m, tab, n, 1);
  free(tab);
  debugPrintf("[boot] %s %u KB staged %p -> %p (%d unresolved imports)\n", m->base_name,
              (unsigned)(m->load_size >> 10), m->load_base, m->load_virtbase, missing);
  if (patch && patch(m) != 0)
    return -1; /* (its staging stays allocated: once, and rare) */
  so_finalize(m);
  so_flush_caches(m);
  return 0;
}

int lab_load_module(void) { return load_into(&g_mod_game); }

/* Local play: the engine a second time, its own code and data (its globals
 * are its own: its relocations resolve its symbols to itself), started as
 * the first one was -- constructors, JNI_OnLoad, the managers' init()s. */
static int load_copy(int eng, int (*patch)(so_module *), const char *what) {
  if (g_up[eng])
    return g_up[eng] > 0 ? 0 : -1;
  g_up[eng] = -1;
  u64 t0 = armGetSystemTick();
  so_module *m = lab_module(eng);
  if (load_into_ex(m, patch) != 0)
    return -1;
  so_execute_init_array(m);
  typedef jint (*fn_onload)(void *vm, void *reserved);
  fn_onload onload = (fn_onload)so_try_find_addr_rx(m, "JNI_OnLoad");
  if (onload)
    onload(g_jni_vm, NULL);
  g_up[eng] = 1;
  lab_java_boot_natives_eng(eng);
  debugPrintf("[boot] the engine for %s is up in %llu ms\n", what,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
  return 0;
}

int lab_load_second_engine(void) { return load_copy(1, NULL, "local play"); }

/* The iPad copy: its resource dir is data/files-ipad (lab_java.c). */
int lab_load_ipad_engine(void) { return load_copy(2, patch_ipad, "iPad levels"); }

/* Local play on iPad boards: the iPad copy twice (2 and 3). */
int lab_load_ipad_race_engine(void) {
  if (lab_load_ipad_engine() != 0)
    return -1;
  return load_copy(3, patch_ipad, "local play on iPad boards");
}

/* System.loadLibrary runs the library's constructors (and JNI_OnLoad, which
 * only answers the JNI version: lab_boot.c calls it). */
void lab_run_constructors(void) {
  extern int g_so_trace_ctors;
  g_so_trace_ctors = dcr_is_emulator();
  u64 t0 = armGetSystemTick();
  so_execute_init_array(&g_mod_game);
  debugPrintf("[boot] %s constructors done in %llu ms\n", g_mod_game.base_name,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}

/* ------------------------------------------------ the shared runtime's hooks
 * codespace.h: code written at run time by the game's own modules (PvZ's mod
 * did that); this engine never does, so every request is the plain shim's. */
volatile int g_cs_armed;
void *cs_mmap(size_t len, int prot, const void *caller) { return NULL; }
int cs_munmap(void *addr, size_t len) { return 0; }
int cs_mprotect(void *addr, size_t len, int prot, const void *caller) {
  /* The engine's own pages: never a real change (text stays RX, data RW). */
  return so_find_module_by_addr(addr) != NULL;
}
int cs_write(void *dst, const void *src, size_t n, int c, int kind) { return 0; }

/* exc_handler.c: no trampoline pool here. */
int dcr_in_code_pool(const void *p) { return 0; }

/* dcr_net.h: offline; the engine has no sockets. */
int dcr_net_owns(int fd) { return 0; }
int dcr_net_close(int fd) { return -1; }
int dcr_net_fcntl(int fd, int cmd, long arg) { return -1; }
int dcr_net_ioctl(int fd, unsigned long req, void *arg) { return -1; }
short dcr_net_ready(int fd, short events) { return 0; }
