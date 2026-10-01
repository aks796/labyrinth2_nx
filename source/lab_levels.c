/* lab_levels.c -- se.illusionlabs.labyrinth2.levelpack.LevelsDB.
 *
 * On a phone: levels.db, table `levels` (one row per level pack: its name,
 * author, difficulty, number of levels, how many are finished, the current
 * one, official or downloaded...) and `imported_zips` (asset zips already
 * unpacked). The engine reads and writes four columns through JNI
 * (currentlevel, nbrlevelsfinished, myrating, mydifficulty); the list screens
 * query the rest. Here: an array in memory, written to <root>/data/levels.txt
 * (tab-separated, one pack per line) when it changes. The best times are not
 * here: the engine keeps them in the registry (TIME_<pack>_<level>), next to
 * the designer's (DTIME_..., written when a pack is added). MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <unistd.h>

#include "lab.h"
#include "util.h"

const char *dcr_game_root(void); /* the runtime (dcr_path.c) */

#define MAX_PACKS 1024
#define MAX_ZIPS 256
static LabPack *g_packs[MAX_PACKS];
static int g_npacks;
static char g_zips[MAX_ZIPS][64];
static int g_nzips;
static Mutex g_lock;

int lab_levels_count(void) { return g_npacks; }
LabPack *lab_levels_at(int i) { return i >= 0 && i < g_npacks ? g_packs[i] : NULL; }

LabPack *lab_levels_find(const char *id) {
  if (!id)
    return NULL;
  for (int i = 0; i < g_npacks; i++)
    if (!strcmp(g_packs[i]->id, id))
      return g_packs[i];
  return NULL;
}

/* ------------------------------------------------------------ the file */
static void path(char *out, size_t cap, const char *suffix) {
  snprintf(out, cap, "%s/data/levels.txt%s", dcr_game_root(), suffix);
}

/* text fields cannot hold tabs or newlines */
static void put_field(FILE *f, const char *s) {
  fputc('\t', f);
  for (; *s; s++)
    fputc(*s == '\t' || *s == '\n' || *s == '\r' ? ' ' : *s, f);
}

void lab_levels_save(void) {
  char p[300], tmp[310];
  path(p, sizeof p, "");
  path(tmp, sizeof tmp, ".part");
  mutexLock(&g_lock);
  FILE *f = fopen(tmp, "w");
  if (!f) {
    mutexUnlock(&g_lock);
    debugPrintf("[levels] cannot write %s\n", tmp);
    return;
  }
  fputs("# Labyrinth 2 level packs: P id name author_id author difficulty levels finished "
        "current tutorial preloaded published ownlevel revision theme reqver myrating "
        "mydifficulty playcount rating ipad fave / Z imported zip\n", f);
  for (int i = 0; i < g_npacks; i++) {
    const LabPack *k = g_packs[i];
    fputc('P', f);
    put_field(f, k->id);
    put_field(f, k->name);
    put_field(f, k->author_id);
    put_field(f, k->author);
    fprintf(f, "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.6f\t%d\t%d\n", k->difficulty,
            k->nlevels, k->nfinished, k->current, k->tutorial, k->preloaded, k->published,
            k->ownlevel, k->revision, k->theme, k->reqver, k->myrating, k->mydifficulty,
            k->playcount, k->rating, k->ipad, k->fave);
  }
  for (int i = 0; i < g_nzips; i++)
    fprintf(f, "Z\t%s\n", g_zips[i]);
  int ok = fclose(f) == 0;
  mutexUnlock(&g_lock);
  if (ok) {
    unlink(p);
    ok = rename(tmp, p) == 0;
  }
  if (!ok)
    debugPrintf("[levels] saving failed\n");
}

/* a field into a fixed buffer, cut to fit */
static void copy_field(char *dst, size_t cap, const char *src) {
  size_t n = strlen(src);
  if (n >= cap)
    n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

static char *next_field(char **s) {
  char *b = *s;
  if (!b)
    return (char *)"";
  char *t = strchr(b, '\t');
  if (t) {
    *t = 0;
    *s = t + 1;
  } else {
    *s = NULL;
  }
  return b;
}

void lab_levels_load(void) {
  char p[300];
  path(p, sizeof p, "");
  FILE *f = fopen(p, "r");
  if (!f) {
    /* a save cut off between its two steps leaves only the new copy */
    path(p, sizeof p, ".part");
    f = fopen(p, "r");
  }
  if (!f)
    return;
  static char line[2048];
  mutexLock(&g_lock);
  while (fgets(line, sizeof line, f)) {
    size_t l = strlen(line);
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = 0;
    if (line[0] == 'Z' && line[1] == '\t' && g_nzips < MAX_ZIPS) {
      copy_field(g_zips[g_nzips++], sizeof g_zips[0], line + 2);
      continue;
    }
    if (line[0] != 'P' || line[1] != '\t' || g_npacks >= MAX_PACKS)
      continue;
    char *s = line + 2;
    LabPack *k = calloc(1, sizeof *k);
    if (!k)
      break;
    copy_field(k->id, sizeof k->id, next_field(&s));
    copy_field(k->name, sizeof k->name, next_field(&s));
    copy_field(k->author_id, sizeof k->author_id, next_field(&s));
    copy_field(k->author, sizeof k->author, next_field(&s));
    int *ints[] = {&k->difficulty, &k->nlevels, &k->nfinished, &k->current, &k->tutorial,
                   &k->preloaded, &k->published, &k->ownlevel, &k->revision, &k->theme,
                   &k->reqver, &k->myrating, &k->mydifficulty, &k->playcount};
    for (unsigned i = 0; i < sizeof ints / sizeof ints[0]; i++)
      *ints[i] = atoi(next_field(&s));
    k->rating = atof(next_field(&s));
    k->ipad = atoi(next_field(&s)); /* (older saves: none) */
    k->fave = atoi(next_field(&s));
    if (!k->id[0] || lab_levels_find(k->id)) {
      free(k);
      continue;
    }
    g_packs[g_npacks++] = k;
  }
  mutexUnlock(&g_lock);
  fclose(f);
  debugPrintf("[levels] %d level packs, %d imported zips\n", g_npacks, g_nzips);
}

/* ------------------------------------------------------------ changes */
/* LevelsDB.a(LevelPack): insert, or update an existing row. The Java's
 * update also overwrote the progress with the new object's (0): kept here,
 * so a pack replaced by a newer copy keeps what was played. */
LabPack *lab_levels_add(const LabPack *p, const int *times, int ntimes) {
  mutexLock(&g_lock);
  LabPack *k = lab_levels_find(p->id);
  int fresh = !k;
  if (fresh) {
    if (g_npacks >= MAX_PACKS || !(k = calloc(1, sizeof *k))) {
      mutexUnlock(&g_lock);
      return NULL;
    }
    *k = *p;
    k->myrating = k->mydifficulty = -1;
    g_packs[g_npacks++] = k;
  } else {
    int nf = k->nfinished, cur = k->current, mr = k->myrating, md = k->mydifficulty, pc = k->playcount;
    int fave = k->fave;
    *k = *p;
    k->fave = fave;
    k->nfinished = nf < k->nlevels ? nf : k->nlevels;
    k->current = cur < k->nlevels ? cur : 0;
    k->myrating = mr;
    k->mydifficulty = md;
    k->playcount = pc;
  }
  mutexUnlock(&g_lock);
  if (fresh && p->preloaded)
    lab_reg_set_int("official-levels-count", lab_reg_get_int("official-levels-count", 0) + 1);
  if (fresh && p->tutorial)
    lab_reg_set_int("official-tutorial-count", lab_reg_get_int("official-tutorial-count", 0) + 1);
  for (int i = 0; i < ntimes; i++) {
    char key[128];
    snprintf(key, sizeof key, "DTIME_%s_%d", p->id, i);
    lab_reg_set_int(key, times[i]);
  }
  return k;
}

int lab_levels_remove(const char *id) {
  mutexLock(&g_lock);
  for (int i = 0; i < g_npacks; i++)
    if (!strcmp(g_packs[i]->id, id)) {
      free(g_packs[i]);
      memmove(&g_packs[i], &g_packs[i + 1], sizeof g_packs[0] * (size_t)(g_npacks - i - 1));
      g_npacks--;
      mutexUnlock(&g_lock);
      return 1;
    }
  mutexUnlock(&g_lock);
  return 0;
}

int lab_levels_imported(const char *zip) {
  for (int i = 0; i < g_nzips; i++)
    if (!strcmp(g_zips[i], zip))
      return 1;
  return 0;
}

void lab_levels_mark_imported(const char *zip) {
  if (lab_levels_imported(zip) || g_nzips >= MAX_ZIPS)
    return;
  snprintf(g_zips[g_nzips++], sizeof g_zips[0], "%s", zip);
}

/* ------------------------------------------------------------ queries */
/* ORDER BY istutorial DESC, levelname ASC (SQLite's binary collation) */
static int cmp_list(const void *a, const void *b) {
  const LabPack *x = *(LabPack *const *)a, *y = *(LabPack *const *)b;
  if (x->tutorial != y->tutorial)
    return y->tutorial - x->tutorial;
  return strcmp(x->name, y->name);
}

int lab_levels_query(int group, int official, LabPack **out, int cap) {
  int n = 0;
  mutexLock(&g_lock);
  for (int i = 0; i < g_npacks && n < cap; i++) {
    LabPack *k = g_packs[i];
    if (k->ownlevel || k->ipad || (k->preloaded != 0) != (official != 0))
      continue;
    int in = group == 0 ? (k->nfinished != 0 && k->nfinished < k->nlevels)
           : group == 1 ? k->nfinished == 0
                        : k->nfinished == k->nlevels;
    if (in)
      out[n++] = k;
  }
  mutexUnlock(&g_lock);
  qsort(out, (size_t)n, sizeof *out, cmp_list);
  return n;
}

/* LevelsDB.c() / d(): your own packs (made in the web editor), not
 * published / published, ORDER BY levelname */
int lab_levels_query_dev(int group, int kind, int ipad, LabPack **out, int cap) {
  int n = 0;
  mutexLock(&g_lock);
  for (int i = 0; i < g_npacks && n < cap; i++) {
    LabPack *k = g_packs[i];
    if ((k->ipad != 0) != (ipad != 0))
      continue;
    if (kind == 2 ? !k->fave : k->ownlevel || (k->preloaded != 0) != (kind == 1))
      continue;
    int in = group < 0    ? 1
           : group == 0 ? (k->nfinished != 0 && k->nfinished < k->nlevels)
           : group == 1 ? k->nfinished == 0
                        : k->nfinished == k->nlevels;
    if (in)
      out[n++] = k;
  }
  mutexUnlock(&g_lock);
  qsort(out, (size_t)n, sizeof *out, cmp_list);
  return n;
}

int lab_levels_query_own(int published, LabPack **out, int cap) {
  int n = 0;
  mutexLock(&g_lock);
  for (int i = 0; i < g_npacks && n < cap; i++) {
    LabPack *k = g_packs[i];
    if (k->ownlevel && (k->published != 0) == (published != 0))
      out[n++] = k;
  }
  mutexUnlock(&g_lock);
  qsort(out, (size_t)n, sizeof *out, cmp_list);
  return n;
}

int lab_levels_best_time(const char *id, int level) {
  char key[128];
  snprintf(key, sizeof key, "TIME_%s_%d", id, level);
  return lab_reg_get_int(key, 0);
}

int lab_levels_designer_time(const char *id, int level) {
  char key[128];
  snprintf(key, sizeof key, "DTIME_%s_%d", id, level);
  return lab_reg_get_int(key, 0);
}

/* ------------------------------------------------------------ info.xml */
/* The text of the first <tag>...</tag>, as the Java read it: the element's
 * text taken literally (its loader escaped the raw text before parsing, so
 * entities in the file come out as written). */
static int tag_text(const char *xml, const char *tag, char *out, size_t cap) {
  char open[48], close[48];
  snprintf(open, sizeof open, "<%s>", tag);
  snprintf(close, sizeof close, "</%s>", tag);
  const char *a = strstr(xml, open);
  if (!a)
    return -1;
  a += strlen(open);
  const char *b = strstr(a, close);
  if (!b)
    return -1;
  size_t n = (size_t)(b - a);
  if (n >= cap)
    n = cap - 1;
  memcpy(out, a, n);
  out[n] = 0;
  /* trim */
  char *s = out;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
    s++;
  if (s != out)
    memmove(out, s, strlen(s) + 1);
  size_t l = strlen(out);
  while (l && (out[l - 1] == ' ' || out[l - 1] == '\t' || out[l - 1] == '\r' || out[l - 1] == '\n'))
    out[--l] = 0;
  return 0;
}

int lab_levels_parse_info(const char *xml, LabPack *p, int *times, int cap, int *ntimes) {
  memset(p, 0, sizeof *p);
  char v[1024];
  if (tag_text(xml, "levelid", p->id, sizeof p->id) || !p->id[0])
    return -1;
  if (tag_text(xml, "levelname", p->name, sizeof p->name))
    snprintf(p->name, sizeof p->name, "%s", p->id);
  tag_text(xml, "authorid", p->author_id, sizeof p->author_id);
  tag_text(xml, "authorname", p->author, sizeof p->author);
  if (!tag_text(xml, "difficulty", v, sizeof v))
    p->difficulty = atoi(v);
  if (!tag_text(xml, "nbrunits", v, sizeof v))
    p->nlevels = atoi(v);
  if (!tag_text(xml, "theme", v, sizeof v))
    p->theme = atoi(v);
  if (!tag_text(xml, "reqver", v, sizeof v))
    p->reqver = atoi(v);
  p->tutorial = !strncmp(p->id, "ZTUT", 4) || !strncmp(p->id, "YTUT", 4); /* (YTUT: the iPad's) */
  p->published = 1; /* p(): published || !ownlevel */
  p->myrating = p->mydifficulty = -1;
  *ntimes = 0;
  if (!tag_text(xml, "times", v, sizeof v)) {
    /* "17395,11745,...," -- Java's split drops the trailing empty field */
    for (char *s = v; *s && *ntimes < cap;) {
      char *e = strchr(s, ',');
      if (e)
        *e = 0;
      if (*s)
        times[(*ntimes)++] = atoi(s);
      if (!e)
        break;
      s = e + 1;
    }
  }
  return p->nlevels > 0 ? 0 : -1;
}
