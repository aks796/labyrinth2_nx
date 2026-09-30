/* lab_apk.c -- the user's game.apk (and iPad .ipa) as read-only file stores.
 *
 * The engine itself never opens the APK (it reads its files dir, which
 * lab_files.c fills from the APK's assets/ once). What reads the APK at run
 * time is the port: the menus' pictures (res/drawable-hdpi-v4/...), the
 * sounds (res/raw/<name>.ogg), and the setup. The central directory is indexed
 * once (every entry, by its full name); each read goes through the APK block
 * cache (dcr_apkcache.c: RAM, filled from the SD card on first use) and is
 * inflated with miniz if the entry is deflated.
 *
 * The iPad game's .ipa (Labyrinth 2 HD, the player's own copy, any *.ipa in
 * the game's folder) is read the same way, without the cache: its
 * Payload/<name>.app/ is where every name is looked up (the iPad menus'
 * pictures, its level packs and floors: lab_hd*.c, lab_files.c). MIT.
 */
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "lab.h"
#include "util.h"

ssize_t dcr_apkcache_read(uint64_t off, void *buf, size_t n); /* dcr_apkcache.c */

typedef struct {
  uint32_t hash;
  uint32_t name_off;   /* into names */
  uint32_t local_off;  /* local file header */
  uint32_t csize, usize, crc;
  uint16_t method;
} Entry;

typedef struct {
  const char *what;    /* "the APK" */
  Entry *ent;
  int nent;
  char *names;
  uint32_t *tab;       /* open addressing: entry index + 1 */
  uint32_t mask;
  FILE *f;             /* the reader (the APK: when the cache can't) */
  int cached;          /* reads go through dcr_apkcache first */
  Mutex lock;
  uint64_t reads, bytes, misses;
} Zip;

static Zip g_apk = {.what = "the APK", .cached = 1};
static Zip g_ipa = {.what = "the .ipa"};

static uint32_t fnv(const char *s) {
  uint32_t h = 2166136261u;
  while (*s)
    h = (h ^ (uint8_t)*s++) * 16777619u;
  return h;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int zread(Zip *z, uint64_t off, void *buf, size_t n) {
  if (z->cached) {
    ssize_t r = dcr_apkcache_read(off, buf, n);
    if (r == (ssize_t)n)
      return 0;
  }
  mutexLock(&z->lock);
  int ok = z->f && fseek(z->f, (long)off, SEEK_SET) == 0 && fread(buf, 1, n, z->f) == n;
  mutexUnlock(&z->lock);
  return ok ? 0 : -1;
}

/* the central directory, indexed; `strip`: only names under the first
 * "Payload/<x>.app/" (the .ipa), kept without it */
static int zip_open(Zip *z, const char *path, int strip) {
  z->f = fopen(path, "rb");
  if (!z->f) {
    debugPrintf("[apk] cannot open %s\n", path);
    return -1;
  }
  fseek(z->f, 0, SEEK_END);
  long size = ftell(z->f);
  /* the end of central directory record: in the last 64 KB + 22 bytes */
  long tail = size < 0x10000 + 22 ? size : 0x10000 + 22;
  uint8_t *t = malloc((size_t)tail);
  if (!t || zread(z, (uint64_t)(size - tail), t, (size_t)tail)) {
    free(t);
    return -1;
  }
  long eocd = -1;
  for (long i = tail - 22; i >= 0; i--)
    if (rd32(t + i) == 0x06054b50u) {
      eocd = i;
      break;
    }
  if (eocd < 0) {
    free(t);
    debugPrintf("[apk] %s: not a zip (no end of central directory)\n", path);
    return -1;
  }
  uint32_t n_total = rd16(t + eocd + 10), cd_size = rd32(t + eocd + 12), cd_off = rd32(t + eocd + 16);
  free(t);
  uint8_t *cd = malloc(cd_size);
  if (!cd || zread(z, cd_off, cd, cd_size)) {
    free(cd);
    return -1;
  }
  z->ent = calloc(n_total ? n_total : 1, sizeof *z->ent);
  z->names = malloc(cd_size + 1); /* the names fit in the directory they come from */
  if (!z->ent || !z->names) {
    free(cd);
    return -1;
  }
  char prefix[160] = "";
  size_t plen = 0;
  uint32_t names_len = 0;
  for (uint32_t i = 0, p = 0; i < n_total && p + 46 <= cd_size; i++) {
    if (rd32(cd + p) != 0x02014b50u)
      break;
    uint16_t method = rd16(cd + p + 10), nlen = rd16(cd + p + 28), xlen = rd16(cd + p + 30),
             clen = rd16(cd + p + 32);
    uint32_t crc = rd32(cd + p + 16), csize = rd32(cd + p + 20), usize = rd32(cd + p + 24),
             loff = rd32(cd + p + 42);
    if ((uint64_t)p + 46 + nlen + xlen + clen > cd_size)
      break; /* a damaged directory */
    const char *name = (const char *)cd + p + 46;
    p += 46 + nlen + xlen + clen;
    if (!nlen || name[nlen - 1] == '/' || usize == 0xffffffffu || csize == 0xffffffffu)
      continue; /* a folder, or Zip64 (never in a package this old) */
    if (strip) {
      if (!plen) {
        /* Payload/<x>.app/ from the first entry under it */
        const char *app = nlen > 8 && !memcmp(name, "Payload/", 8) ? memchr(name + 8, '/', nlen - 8) : NULL;
        if (!app || app - name < 12 || memcmp(app - 4, ".app", 4) || (size_t)(app - name + 1) >= sizeof prefix)
          continue;
        plen = (size_t)(app - name) + 1;
        memcpy(prefix, name, plen);
        prefix[plen] = 0;
      }
      if (nlen <= plen || memcmp(name, prefix, plen))
        continue;
      name += plen;
      nlen = (uint16_t)(nlen - plen);
    }
    Entry *e = &z->ent[z->nent++];
    e->name_off = names_len;
    memcpy(z->names + names_len, name, nlen);
    names_len += nlen;
    z->names[names_len++] = 0;
    e->local_off = loff;
    e->csize = csize;
    e->usize = usize;
    e->crc = crc;
    e->method = method;
    e->hash = fnv(z->names + e->name_off);
  }
  free(cd);
  uint32_t cap = 16;
  while (cap < (uint32_t)z->nent * 2)
    cap <<= 1;
  z->tab = calloc(cap, sizeof *z->tab);
  if (!z->tab)
    return -1;
  z->mask = cap - 1;
  for (int i = 0; i < z->nent; i++) {
    uint32_t h = z->ent[i].hash & z->mask;
    while (z->tab[h])
      h = (h + 1) & z->mask;
    z->tab[h] = (uint32_t)i + 1;
  }
  debugPrintf("[apk] %d files in %s%s%s\n", z->nent, z->what, plen ? " under " : "", prefix);
  return z->nent > 0 ? 0 : -1;
}

static const Entry *lookup(const Zip *z, const char *name) {
  if (!z->tab || !name)
    return NULL;
  uint32_t hv = fnv(name);
  for (uint32_t h = hv & z->mask, k; (k = z->tab[h]) != 0; h = (h + 1) & z->mask) {
    const Entry *e = &z->ent[k - 1];
    if (e->hash == hv && !strcmp(z->names + e->name_off, name))
      return e;
  }
  return NULL;
}

static void *zip_read(Zip *z, const char *name, size_t *len) {
  const Entry *e = lookup(z, name);
  if (!e) {
    z->misses++;
    return NULL;
  }
  uint8_t lh[30];
  if (zread(z, e->local_off, lh, sizeof lh) || rd32(lh) != 0x04034b50u)
    return NULL;
  uint64_t data = (uint64_t)e->local_off + 30 + rd16(lh + 26) + rd16(lh + 28);
  uint8_t *out = malloc(e->usize + 1);
  if (!out)
    return NULL;
  if (e->method == 0) {
    if (zread(z, data, out, e->usize)) {
      free(out);
      return NULL;
    }
  } else if (e->method == 8) {
    uint8_t *in = malloc(e->csize ? e->csize : 1);
    size_t got = TINFL_DECOMPRESS_MEM_TO_MEM_FAILED;
    if (in && !zread(z, data, in, e->csize))
      got = tinfl_decompress_mem_to_mem(out, e->usize, in, e->csize, 0);
    free(in);
    if (got != e->usize) {
      debugPrintf("[apk] %s: inflate failed (%u of %u bytes)\n", name, (unsigned)got,
                  (unsigned)e->usize);
      free(out);
      return NULL;
    }
  } else {
    debugPrintf("[apk] %s: compression method %u not supported\n", name, e->method);
    free(out);
    return NULL;
  }
  out[e->usize] = 0;
  if (len)
    *len = e->usize;
  z->reads++;
  z->bytes += e->usize;
  return out;
}

static void zip_list(const Zip *z, const char *prefix, void (*fn)(const char *name, uint32_t crc, void *arg),
                     void *arg) {
  size_t n = strlen(prefix);
  for (int i = 0; i < z->nent; i++) {
    const char *nm = z->names + z->ent[i].name_off;
    if (!strncmp(nm, prefix, n))
      fn(nm, z->ent[i].crc, arg);
  }
}

/* ------------------------------------------------------------ game.apk */
int lab_apk_init(const char *apk_path) { return zip_open(&g_apk, apk_path, 0); }
int lab_apk_exists(const char *name) { return lookup(&g_apk, name) != NULL; }
void *lab_apk_read(const char *name, size_t *len) { return zip_read(&g_apk, name, len); }
void lab_apk_list(const char *prefix, void (*fn)(const char *name, uint32_t crc, void *arg), void *arg) {
  zip_list(&g_apk, prefix, fn, arg);
}

void lab_apk_report(void) {
  debugPrintf("[apk] %llu reads, %llu KB, %llu not found\n", (unsigned long long)g_apk.reads,
              (unsigned long long)(g_apk.bytes >> 10), (unsigned long long)g_apk.misses);
  if (g_ipa.nent)
    debugPrintf("[apk] the .ipa: %llu reads, %llu KB, %llu not found\n", (unsigned long long)g_ipa.reads,
                (unsigned long long)(g_ipa.bytes >> 10), (unsigned long long)g_ipa.misses);
}

/* ------------------------------------------------------------ the .ipa */
static char g_ipa_path[512];

int lab_ipa_init(const char *path) {
  if (g_ipa.nent)
    return 0;
  if (zip_open(&g_ipa, path, 1) != 0 || !lookup(&g_ipa, "Info.plist")) {
    debugPrintf("[apk] %s: not an iPhone / iPad app\n", path);
    return -1;
  }
  snprintf(g_ipa_path, sizeof g_ipa_path, "%s", path);
  return 0;
}

int lab_ipa_present(void) { return g_ipa.nent > 0; }
const char *lab_ipa_path(void) { return g_ipa_path; }
int lab_ipa_exists(const char *name) { return lookup(&g_ipa, name) != NULL; }
void *lab_ipa_read(const char *name, size_t *len) { return g_ipa.nent ? zip_read(&g_ipa, name, len) : NULL; }
void lab_ipa_list(const char *prefix, void (*fn)(const char *name, uint32_t crc, void *arg), void *arg) {
  zip_list(&g_ipa, prefix, fn, arg);
}
uint32_t lab_ipa_crc(const char *name) {
  const Entry *e = lookup(&g_ipa, name);
  return e ? e->crc : 0;
}
