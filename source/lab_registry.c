/* lab_registry.c -- se.illusionlabs.common.ZRegistry.
 *
 * On a phone this is an SQLite database with four key/value tables
 * (stringdata, binarydata, integerdata, floatdata), each get/set a query. The
 * engine keeps nearly everything in it: the settings, the statistics and
 * awards ("stat-*", "ball-unlocked-id-*"), the best times ("TIME_<pack>_<n>")
 * next to the designer's ("DTIME_..."), the calibration. Here the tables live
 * in memory (one hash map; a key can be in several tables, as in SQL) and are
 * written to <root>/data/registry.txt a moment after a change, atomically:
 *
 *   i <key> <int>     f <key> <float>     s <key> <escaped string>
 *
 * MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <unistd.h>

#include "lab.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

enum { T_STRING, T_BINARY, T_INT, T_FLOAT };

typedef struct Item {
  struct Item *next;
  uint32_t hash;
  uint8_t table;
  char *key;
  int i;
  float f;
  char *s;
} Item;

#define NB 1024
static Item *g_b[NB];
static int g_count;
static Mutex g_lock;
static volatile int g_dirty;
static u64 g_dirty_tick;

static uint32_t fnv(const char *s, int t) {
  uint32_t h = 2166136261u ^ (uint32_t)t;
  while (*s)
    h = (h ^ (uint8_t)*s++) * 16777619u;
  return h;
}

static Item *find2(const char *key, int t, int create, int *created) {
  *created = 0;
  if (!key)
    return NULL;
  uint32_t h = fnv(key, t);
  for (Item *it = g_b[h % NB]; it; it = it->next)
    if (it->hash == h && it->table == t && !strcmp(it->key, key))
      return it;
  if (!create)
    return NULL;
  Item *it = calloc(1, sizeof *it);
  if (!it)
    return NULL;
  it->hash = h;
  it->table = (uint8_t)t;
  it->key = strdup(key);
  it->next = g_b[h % NB];
  g_b[h % NB] = it;
  g_count++;
  *created = 1;
  return it;
}

static Item *find(const char *key, int t, int create) {
  int created;
  return find2(key, t, create, &created);
}

static void touch(void) {
  if (!g_dirty)
    g_dirty_tick = armGetSystemTick();
  g_dirty = 1;
}

int lab_reg_get_int(const char *key, int def) {
  mutexLock(&g_lock);
  Item *it = find(key, T_INT, 0);
  int v = it ? it->i : def;
  mutexUnlock(&g_lock);
  return v;
}

float lab_reg_get_float(const char *key, float def) {
  mutexLock(&g_lock);
  Item *it = find(key, T_FLOAT, 0);
  float v = it ? it->f : def;
  mutexUnlock(&g_lock);
  return v;
}

/* The pointer stays valid until the value is set again: copy it. */
const char *lab_reg_get_string(const char *key) {
  mutexLock(&g_lock);
  Item *it = find(key, T_STRING, 0);
  const char *v = it ? it->s : NULL;
  mutexUnlock(&g_lock);
  return v;
}

void lab_reg_set_int(const char *key, int v) {
  mutexLock(&g_lock);
  int created;
  Item *it = find2(key, T_INT, 1, &created);
  if (it && (created || it->i != v)) {
    it->i = v;
    touch();
  }
  mutexUnlock(&g_lock);
}

void lab_reg_set_float(const char *key, float v) {
  mutexLock(&g_lock);
  Item *it = find(key, T_FLOAT, 1);
  if (it) {
    it->f = v;
    touch();
  }
  mutexUnlock(&g_lock);
}

void lab_reg_set_string(const char *key, const char *v) {
  mutexLock(&g_lock);
  Item *it = find(key, T_STRING, 1);
  if (it) {
    free(it->s); /* callers copy what they read at once (lab_java.c) */
    it->s = strdup(v ? v : "");
    touch();
  }
  mutexUnlock(&g_lock);
}

void lab_reg_remove(const char *key, int table) {
  mutexLock(&g_lock);
  uint32_t h = fnv(key, table);
  for (Item **pp = &g_b[h % NB]; *pp; pp = &(*pp)->next) {
    Item *it = *pp;
    if (it->hash == h && it->table == table && !strcmp(it->key, key)) {
      *pp = it->next;
      free(it->key);
      free(it);
      g_count--;
      touch();
      break;
    }
  }
  mutexUnlock(&g_lock);
}

/* ------------------------------------------------------------ the file */
static void path(char *out, size_t cap, const char *suffix) {
  snprintf(out, cap, "%s/data/registry.txt%s", dcr_game_root(), suffix);
}

static void put_escaped(FILE *f, const char *s) {
  for (; *s; s++) {
    if (*s == '\\')
      fputs("\\\\", f);
    else if (*s == '\n')
      fputs("\\n", f);
    else if (*s == '\r')
      fputs("\\r", f);
    else if (*s == ' ')
      fputs("\\s", f); /* keys end at the first plain space */
    else
      fputc(*s, f);
  }
}

static void unescape(char *s) {
  char *o = s;
  for (; *s; s++) {
    if (*s == '\\' && s[1]) {
      s++;
      *o++ = *s == 'n' ? '\n' : *s == 'r' ? '\r' : *s == 's' ? ' ' : *s;
    } else {
      *o++ = *s;
    }
  }
  *o = 0;
}

void lab_reg_load(void) {
  char p[300];
  path(p, sizeof p, "");
  FILE *f = fopen(p, "r");
  if (!f) {
    /* a save cut off between its two steps leaves only the new copy */
    char part[310];
    path(part, sizeof part, ".part");
    f = fopen(part, "r");
    if (f)
      debugPrintf("[registry] registry.txt missing: reading the last save's registry.txt.part\n");
  }
  if (!f) {
    debugPrintf("[registry] no registry.txt yet: the game starts fresh\n");
    return;
  }
  static char line[4096];
  int n = 0;
  mutexLock(&g_lock);
  while (fgets(line, sizeof line, f)) {
    size_t l = strlen(line);
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = 0;
    if (l < 3 || line[1] != ' ')
      continue;
    char *key = line + 2, *sp = strchr(key, ' ');
    if (!sp)
      continue;
    *sp = 0;
    unescape(key);
    char *val = sp + 1;
    Item *it = NULL;
    switch (line[0]) {
    case 'i':
      if ((it = find(key, T_INT, 1)))
        it->i = (int)strtol(val, NULL, 10);
      break;
    case 'f':
      if ((it = find(key, T_FLOAT, 1)))
        it->f = strtof(val, NULL);
      break;
    case 's':
      if ((it = find(key, T_STRING, 1))) {
        unescape(val);
        free(it->s);
        it->s = strdup(val);
      }
      break;
    }
    n += it != NULL;
  }
  mutexUnlock(&g_lock);
  fclose(f);
  debugPrintf("[registry] %d values loaded\n", n);
}

void lab_reg_save(void) {
  if (!g_dirty)
    return;
  char p[300], tmp[310];
  path(p, sizeof p, "");
  path(tmp, sizeof tmp, ".part");
  mutexLock(&g_lock);
  g_dirty = 0;
  FILE *f = fopen(tmp, "w");
  if (!f) {
    touch(); /* try again later */
    mutexUnlock(&g_lock);
    debugPrintf("[registry] cannot write %s\n", tmp);
    return;
  }
  for (int b = 0; b < NB; b++)
    for (Item *it = g_b[b]; it; it = it->next) {
      if (it->table == T_INT || it->table == T_FLOAT || (it->table == T_STRING && it->s)) {
        fprintf(f, "%c ", it->table == T_INT ? 'i' : it->table == T_FLOAT ? 'f' : 's');
        put_escaped(f, it->key);
        if (it->table == T_INT)
          fprintf(f, " %d\n", it->i);
        else if (it->table == T_FLOAT)
          fprintf(f, " %.9g\n", (double)it->f);
        else {
          fputc(' ', f);
          put_escaped(f, it->s);
          fputc('\n', f);
        }
      }
    }
  int ok = fclose(f) == 0;
  mutexUnlock(&g_lock);
  if (ok) {
    unlink(p);
    ok = rename(tmp, p) == 0;
  }
  if (!ok) {
    debugPrintf("[registry] saving failed\n");
    touch(); /* try again later */
  }
}

/* Saved 2 s after the first change of a burst (the engine sets a handful of
 * values at a level's end), and at exit / HOME (lab_boot.c). */
void lab_reg_tick(void) {
  if (g_dirty && armTicksToNs(armGetSystemTick() - g_dirty_tick) > 2000000000ull)
    lab_reg_save();
}
