/* tools/test_host.c -- the port's portable parts against the real APK, on a
 * PC: the APK index, the first-launch setup (files dir + level table), the
 * level queries, the registry and levels files, the SD card's level packs,
 * the engine's text layout, and the decoding of every picture and sound the
 * port reads (stb_image, stb_vorbis). Run by tools/test_host.sh. MIT. */
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lab.h"
#include "lab_gl.h"

/* ------------------------------------------------------------ stubs */
void dcr_dircache_forget(void) {} /* dcr_dircache.c's: the engine's file listings */
/* lab_draw.c's (the iPad textures' conversion: not run here) */
uint8_t *lab_image_decode(const uint8_t *data, size_t len, int *w, int *h) {
  (void)data, (void)len, (void)w, (void)h;
  return NULL;
}
static char g_root[512];
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
void dcr_setup_progress(const char *what, int permille) { (void)what, (void)permille; } /* the setup bar (the runtime's dcr_setup.c) */
ssize_t dcr_apkcache_read(uint64_t off, void *buf, size_t n) { return -1; } /* lab_apk's own reads */

/* lab_text.c's font and GL: 0.5 em a character, a fake texture */
float lab_font_width(float px, const char *s, int bold) { return (float)strlen(s) * px * 0.5f + (bold ? 1 : 0); }
float lab_font_ascent(float px) { return px * 0.8f; }
float lab_font_descent(float px) { return px * 0.2f; }
void lab_font_draw(uint8_t *dst, int w, int h, int stride, float x, float y, float px, const char *s, int bold) {}
static void f_gen(GLsizei n, GLuint *t) { *t = 77; }
static void f_i(GLenum a, GLint *v) { *v = 0; }
static void f_bind(GLenum a, GLuint b) {}
static void f_par(GLenum a, GLenum b, GLint c) {}
static void f_store(GLenum a, GLint b) {}
static void f_img(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void *p) {}
LabGL lgl = {.GenTextures = f_gen, .GetIntegerv = f_i, .BindTexture = f_bind, .TexParameteri = f_par,
             .PixelStorei = f_store, .TexImage2D = f_img};
static struct { char name[32]; int v; } g_fields[8];
void jni_set_field(JObj *o, const char *name, jvalue val, int is_object) {
  for (int i = 0; i < 8; i++)
    if (!g_fields[i].name[0] || !strcmp(g_fields[i].name, name)) {
      snprintf(g_fields[i].name, sizeof g_fields[i].name, "%s", name);
      g_fields[i].v = val.i;
      return;
    }
}
static int field(const char *n) {
  for (int i = 0; i < 8; i++)
    if (!strcmp(g_fields[i].name, n))
      return g_fields[i].v;
  return -1;
}

/* the pictures and sounds */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.inc"

static int g_fail;
#define TCHECK(c, ...)                        \
  do {                                       \
    if (!(c)) {                              \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                   \
      printf("\n");                          \
      g_fail++;                              \
    }                                        \
  } while (0)

static int g_pics, g_pic_fail, g_snds, g_snd_fail;
static void decode_one(const char *name, uint32_t crc, void *arg) {
  size_t n = strlen(name), len = 0;
  int is_img = n > 4 && (!strcmp(name + n - 4, ".png") || !strcmp(name + n - 4, ".jpg"));
  int is_ogg = n > 4 && !strcmp(name + n - 4, ".ogg");
  if (!is_img && !is_ogg)
    return;
  uint8_t *d = lab_apk_read(name, &len);
  if (!d) {
    TCHECK(0, "%s: unreadable", name);
    return;
  }
  if (is_img) {
    int w, h, c;
    uint8_t *px = stbi_load_from_memory(d, (int)len, &w, &h, &c, 4);
    g_pics++;
    if (!px) {
      g_pic_fail++;
      printf("  cannot decode %s: %s\n", name, stbi_failure_reason());
    }
    stbi_image_free(px);
  } else {
    int ch = 0, rate = 0;
    short *pcm = NULL;
    int frames = stb_vorbis_decode_memory(d, (int)len, &ch, &rate, &pcm);
    g_snds++;
    if (frames <= 0) {
      g_snd_fail++;
      printf("  cannot decode %s\n", name);
    } else if (arg) {
      printf("  %s: %d frames, %d ch, %d Hz\n", name, frames, ch, rate);
    }
    free(pcm);
  }
  free(d);
}

static int count_files(const char *dir) {
  DIR *d = opendir(dir);
  if (!d)
    return -1;
  int n = 0;
  struct dirent *e;
  while ((e = readdir(d)))
    n += e->d_name[0] != '.';
  closedir(d);
  return n;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: test_host game.apk root [pass]\n");
    return 2;
  }
  snprintf(g_root, sizeof g_root, "%s", argv[2]);
  int pass = argc > 3 ? atoi(argv[3]) : 1;
  char p[1024];
  snprintf(p, sizeof p, "%s/data", g_root);
  mkdir(g_root, 0777);
  mkdir(p, 0777);

  TCHECK(lab_apk_init(argv[1]) == 0, "APK index");
  TCHECK(lab_apk_exists("lib/armeabi/liblabyrinthii.so"), "the library is in the APK");
  lab_reg_load();
  lab_levels_load();
  lab_files_setup();

  /* ---- the files dir ---- */
  snprintf(p, sizeof p, "%s/data/files/zipfiles", g_root);
  int nz = count_files(p);
  printf("zipfiles: %d files\n", nz);
  TCHECK(nz >= 21, "the official packs unpacked (%d)", nz);
  snprintf(p, sizeof p, "%s/data/files/textures/common/overlay-buttons.png", g_root);
  struct stat st;
  TCHECK(stat(p, &st) == 0 && st.st_size > 1000, "textures copied");
  snprintf(p, sizeof p, "%s/data/files/meshes/fan.zmesh", g_root);
  TCHECK(stat(p, &st) == 0, "meshes copied");
  snprintf(p, sizeof p, "%s/data/files/awards/awards.xml", g_root);
  TCHECK(stat(p, &st) == 0, "awards copied");

  /* ---- the table ---- */
  int n = lab_levels_count(), official = 0, tut = 0;
  for (int i = 0; i < n; i++) {
    LabPack *k = lab_levels_at(i);
    official += k->preloaded;
    tut += k->tutorial;
    if (pass == 1)
      printf("  %-12s %-34s by %-14s diff %d, %2d levels, theme %d%s\n", k->id, k->name, k->author,
             k->difficulty, k->nlevels, k->theme, k->tutorial ? " (tutorial)" : "");
    TCHECK(k->nlevels > 0 && k->nlevels < 100, "%s: levels %d", k->id, k->nlevels);
    TCHECK(lab_levels_designer_time(k->id, 0) > 0, "%s: designer time", k->id);
    snprintf(p, sizeof p, "%s/data/files/zipfiles/%s", g_root, k->id);
    TCHECK(stat(p, &st) == 0, "%s: its zip is at zipfiles/<levelid>", k->id);
  }
  printf("%d packs (%d official, %d tutorial)\n", n, official, tut);
  TCHECK(official == 21, "21 official packs (%d)", official);
  TCHECK(tut == 1, "one tutorial");
  TCHECK(lab_reg_get_int("official-levels-count", 0) == 21, "official-levels-count %d",
        lab_reg_get_int("official-levels-count", 0));
  /* 2.zip's newer copies won: Z0000000.04 and .18 are the 2010-01-22 ones */
  LabPack *k4 = lab_levels_find("Z0000000.04");
  TCHECK(k4 != NULL, "Z0000000.04 present");

  /* ---- the list's groups ---- */
  LabPack *rows[256];
  int g0 = lab_levels_query(0, 1, rows, 256), g1 = lab_levels_query(1, 1, rows, 256),
      g2 = lab_levels_query(2, 1, rows, 256);
  printf("official groups: ongoing %d, new %d, finished %d\n", g0, g1, g2);
  if (pass == 1) {
    TCHECK(g1 == 21 && g0 == 0 && g2 == 0, "all new at first");
    lab_levels_query(1, 1, rows, 256);
    TCHECK(rows[0]->tutorial, "the tutorial first (istutorial DESC)");
    for (int i = 2; i < g1; i++)
      TCHECK(strcmp(rows[i - 1]->name, rows[i]->name) <= 0, "sorted by name");
    /* play some */
    LabPack *a = lab_levels_find("Z0000000.01");
    a->nfinished = 3, a->current = 3;
    LabPack *b = lab_levels_find("Z0000000.02");
    b->nfinished = b->nlevels;
    lab_levels_save();
    lab_reg_set_int("TIME_Z0000000.01_0", 15000);
    lab_reg_set_float("AccCalX", 0.25f);
    lab_reg_set_string("s-key", "a\\b\nc");
    lab_reg_save();
  } else {
    TCHECK(g0 == 1 && g2 == 1 && g1 == 19, "the played packs moved (ongoing %d finished %d new %d)", g0, g2, g1);
    TCHECK(lab_levels_best_time("Z0000000.01", 0) == 15000, "best time kept");
    TCHECK(fabsf(lab_reg_get_float("AccCalX", 0) - 0.25f) < 1e-6f, "float kept");
    const char *s = lab_reg_get_string("s-key");
    TCHECK(s && !strcmp(s, "a\\b\nc"), "string kept: %s", s ? s : "(null)");
    TCHECK(lab_reg_get_int("official-levels-count", 0) == 21, "not counted twice");
    /* the SD card's pack (tools/test_host.sh put one there) */
    LabPack *u = lab_levels_find("UTEST.01");
    TCHECK(u && !u->preloaded && u->nlevels == 2, "the SD card's pack imported as downloaded");
    TCHECK(lab_levels_query(1, 0, rows, 256) == 2, "listed under Downloaded");
    LabPack *u2 = lab_levels_find("UTEST.02");
    TCHECK(u2 && !u2->preloaded && u2->difficulty == 2, "a pack from an SD card collection");
    snprintf(p, sizeof p, "%s/data/files/zipfiles/UTEST.02", g_root);
    TCHECK(stat(p, &st) == 0, "... written as zipfiles/<levelid>");
    snprintf(p, sizeof p, "%s/data/files/evil.zip", g_root);
    TCHECK(stat(p, &st) != 0, "no file outside zipfiles/");
    LabPack *o = lab_levels_find("Z0000000.05");
    TCHECK(o && o->preloaded, "an official pack's copy on the SD card leaves it official");

    /* ---- level packs as the server sends them (tools/test_host.sh made
     * them in <root>/server): info.xml made readable for the engine ---- */
    static const struct {
      const char *file, *id;
      int rc;
      const char *name; /* info.xml's <levelname> as stored */
    } sv[] = {
        {"escape.zip", "ATEST001.01", 0, "&lt;/T\\&gt; Hold up! &lt;/T\\&gt;"},
        {"markup.zip", "ATEST001.02", 0, "&lt;b&gt;x&lt;/b&gt;"},
        {"plain.zip", "ATEST001.03", 0, "Plain &amp; simple"},
        {"notzip.zip", "ATEST001.04", -1, NULL},
        {"plain.zip", "../escape", -1, NULL},
    };
    for (unsigned i = 0; i < sizeof sv / sizeof sv[0]; i++) {
      snprintf(p, sizeof p, "%s/server/%s", g_root, sv[i].file);
      FILE *f = fopen(p, "rb");
      TCHECK(f != NULL, "%s", p);
      if (!f)
        continue;
      static uint8_t buf[1 << 16];
      size_t len = fread(buf, 1, sizeof buf, f);
      fclose(f);
      LabPack sp;
      int times[256], nt = 0;
      int rc = lab_files_store_pack(sv[i].id, buf, len, &sp, times, 256, &nt);
      TCHECK(rc == sv[i].rc, "%s as %s: stored %d (want %d)", sv[i].file, sv[i].id, rc, sv[i].rc);
      if (rc != 0)
        continue;
      TCHECK(!strcmp(sp.name, sv[i].name), "%s: <levelname> \"%s\"", sv[i].id, sp.name);
      TCHECK(sp.nlevels == 2 && nt == 2 && times[1] == 2000, "%s: levels %d, times %d", sv[i].id, sp.nlevels, nt);
      TCHECK(lab_files_have_pack(sv[i].id), "%s: in zipfiles/", sv[i].id);
    }
    /* the plain one is written as it came */
    {
      snprintf(p, sizeof p, "%s/server/plain.zip", g_root);
      struct stat a, b;
      char q[1024];
      snprintf(q, sizeof q, "%s/data/files/zipfiles/ATEST001.03", g_root);
      TCHECK(stat(p, &a) == 0 && stat(q, &b) == 0 && a.st_size == b.st_size, "a readable pack: its zip untouched");
    }
    /* deleting one: its zip and its row */
    LabPack dp = {0};
    snprintf(dp.id, sizeof dp.id, "ATEST001.03");
    snprintf(dp.name, sizeof dp.name, "x");
    dp.nlevels = 2;
    lab_levels_add(&dp, NULL, 0);
    TCHECK(lab_files_delete_pack("ATEST001.03") == 0 && !lab_levels_find("ATEST001.03") &&
               !lab_files_have_pack("ATEST001.03"),
           "a downloaded pack deleted");
    TCHECK(lab_files_delete_pack("Z0000000.01") != 0 && lab_files_have_pack("Z0000000.01"),
           "an official pack cannot be deleted");

    /* one of yours, not published: the server's zip has no info.xml (as
     * A8FZLCUC.01's: level0.xml and dirtybits.txt, stored) */
    {
      snprintf(p, sizeof p, "%s/server/unpublished.zip", g_root);
      FILE *f = fopen(p, "rb");
      static uint8_t ub[1 << 16];
      size_t ul = f ? fread(ub, 1, sizeof ub, f) : 0;
      if (f)
        fclose(f);
      LabPack up;
      int ut[8], unt = 0;
      TCHECK(lab_files_store_pack("ATEST002.01", ub, ul, &up, ut, 8, &unt) == 1, "stored, no info.xml read");
      LabPack row = {0};
      snprintf(row.id, sizeof row.id, "ATEST002.01");
      snprintf(row.name, sizeof row.name, "Test <3 & more");
      snprintf(row.author_id, sizeof row.author_id, "ATEST002");
      snprintf(row.author, sizeof row.author, "AK");
      row.nlevels = 1, row.ownlevel = 1, row.theme = 0, row.reqver = 16875520;
      int zero = 0;
      LabPack *r = lab_levels_add(&row, &zero, 1);
      char why[160];
      TCHECK(r && lab_files_complete_pack(r) == 0, "an info.xml made for it");
      TCHECK(lab_files_check_pack(r, why, sizeof why) == 0, "then it can be played (%s)", why);
      TCHECK(lab_files_complete_pack(r) == 0, "a second time: nothing to do");
      r->nlevels = 2;
      TCHECK(lab_files_check_pack(r, why, sizeof why) != 0 && strstr(why, "level1.xml"),
             "a level it does not have: refused (%s)", why);
      r->nlevels = 1;
    }
  }

  /* ---- ZFont's layout ---- */
  int tex = lab_text_render(NULL, "You beat the level designer!", "HelveticaNeue-Bold", 20, 1, 200, 0, 0, 256,
                            128);
  TCHECK(tex == 77, "a texture");
  printf("text: %dx%d in %dx%d\n", field("textWidth"), field("textHeight"), field("textureWidth"),
         field("textureHeight"));
  TCHECK(field("textWidth") == 200, "wrapped at maxWidth");
  TCHECK(field("textHeight") == 2 * (int)lroundf(20 * 1.3f), "two lines at 1.3 spacing");
  TCHECK(field("textureWidth") == 256 && field("textureHeight") == 128, "the size asked for");
  lab_text_render(NULL, "Short", "HelveticaNeue", 20, 0, 400, 0, 0, 1024, 1024);
  TCHECK(field("textWidth") == 50, "short: its own width (%d)", field("textWidth"));
  {
    int starts[16], lens[16];
    int k = lab_text_wrap("aaaa bbbb cccc\nd", 10, 0, 50, starts, lens, 16);
    TCHECK(k == 3, "wrap lines %d", k);
    int k2 = lab_text_wrap("abcdefghijklmnop", 10, 0, 30, starts, lens, 16);
    TCHECK(k2 == 3 && lens[0] == 6, "a long word broken (%d lines, %d)", k2, lens[0]);
  }

  /* ---- pictures and sounds ---- */
  if (pass == 1) {
    lab_apk_list("res/", decode_one, NULL);
    lab_apk_list("assets/", decode_one, NULL);
    printf("pictures: %d decoded, %d failed; sounds: %d decoded, %d failed\n", g_pics - g_pic_fail,
           g_pic_fail, g_snds - g_snd_fail, g_snd_fail);
    TCHECK(g_pic_fail == 0 && g_pics > 200, "every picture decodes");
    TCHECK(g_snd_fail == 0 && g_snds == 34, "every sound decodes");
    lab_apk_list("res/raw/ball_roll_wood", decode_one, (void *)1);
    lab_apk_list("res/raw/button_menu", decode_one, (void *)1);
  }
  printf(g_fail ? "%d FAILED\n" : "pass %d: all OK\n", g_fail ? g_fail : pass);
  return g_fail != 0;
}
