/* lab_emoji.c -- colour emoji in the menus' text, from an emoji font on the
 * SD card (sd:/switch/labyrinth2_nx/emoji.ttf, or .ttc / .otf, or the font's own
 * file name: k_names, else any font file there with "emoji" in its name). None comes with the port: the player puts one
 * there, e.g. the iPhone's Apple Color Emoji of the game's time.
 *
 * Level pack names on the server are full of the iPhone's emoji of 2010:
 * SoftBank's private-use code points (U+E001..U+E537, what iOS wrote before
 * iOS 5), mapped here to Unicode (lab_emoji_softbank.h); newer names have
 * Unicode emoji. The font's glyphs are bitmaps (PNG):
 *   sbix         Apple's (Apple Color Emoji): strikes of PNGs per glyph
 *   CBLC + CBDT  Google's (Noto Color Emoji, and Android's copies of the
 *                iPhone's emoji)
 * A flag or a keycap is two code points: the font's GSUB ligature (lookup
 * type 4, also inside type 7) gives its glyph, or -- Apple's fonts make
 * them with morx instead -- the glyph named for it ("u1F1EF_u1F1F5",
 * "u0023_u20E3") in the post table. Only the small tables are
 * kept in memory (cmap, GSUB, hmtx, the chosen strike's index); a glyph's
 * PNG is read from the card when first drawn (lab_draw.c keeps its texture).
 *
 * Without a font, the private-use emoji are left out (the Switch's own
 * extension font would draw its button symbols for some of those code
 * points) and so are U+FE0F / U+200D; other emoji go to the shared fonts.
 * MIT. */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lab.h"
#include "lab_emoji_softbank.h"
#include "util.h"

const char *dcr_game_root(void); /* the runtime (dcr_path.c) */

static const char *const k_names[] = {"emoji.ttf",           "emoji.ttc",           "emoji.otf",
                                      "AppleColorEmoji.ttf", "Apple Color Emoji.ttf", "AppleColorEmoji@2x.ttf",
                                      "Apple Color Emoji.ttc", "NotoColorEmoji.ttf"};

static FILE *g_f;
static int g_state; /* 0 not tried, 1 a font, -1 none */
static int g_kind;  /* 1 sbix, 2 CBDT */
static int g_nglyphs, g_upem;
static uint8_t *g_cmap, *g_gsub, *g_hmtx, *g_post;
static uint32_t g_cmap_len, g_gsub_len, g_hmtx_len, g_cmap_sub, g_post_len;
static int g_cmap_fmt, g_nhm;
/* sbix: the chosen strike */
static uint32_t *g_sbix_offs; /* numGlyphs + 1, file offsets */
static int g_ppem;
/* CBLC / CBDT */
static uint8_t *g_cblc;
static uint32_t g_cblc_len, g_cbdt, g_cb_size; /* CBDT's file offset; the BitmapSize record's offset in CBLC */

static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline int16_t bes16(const uint8_t *p) { return (int16_t)be16(p); }
static inline uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int rd(uint32_t off, void *buf, uint32_t n) {
  return fseek(g_f, (long)off, SEEK_SET) == 0 && fread(buf, 1, n, g_f) == n ? 0 : -1;
}

static uint8_t *rd_alloc(uint32_t off, uint32_t n) {
  if (!n || n > (8u << 20))
    return NULL;
  uint8_t *b = malloc(n);
  if (b && rd(off, b, n)) {
    free(b);
    b = NULL;
  }
  return b;
}

/* ------------------------------------------------------------ the font */
typedef struct {
  uint32_t off, len;
} Tab;

static int find_table(uint32_t base, const char *tag, Tab *t) {
  uint8_t h[12];
  if (rd(base, h, 12))
    return 0;
  int n = be16(h + 4);
  for (int i = 0; i < n && i < 64; i++) {
    uint8_t e[16];
    if (rd(base + 12 + 16 * (uint32_t)i, e, 16))
      return 0;
    if (!memcmp(e, tag, 4)) {
      t->off = be32(e + 8);
      t->len = be32(e + 12);
      return t->len > 0;
    }
  }
  return 0;
}

/* cmap: a Unicode subtable, format 12 (all planes) preferred, else 4 */
static int load_cmap(uint32_t base) {
  Tab t;
  if (!find_table(base, "cmap", &t) || !(g_cmap = rd_alloc(t.off, t.len)))
    return -1;
  g_cmap_len = t.len;
  int n = be16(g_cmap + 2), best = 0;
  for (int i = 0; i < n && 4 + 8 * i + 8 <= (int)t.len; i++) {
    const uint8_t *e = g_cmap + 4 + 8 * i;
    int pid = be16(e), eid = be16(e + 2);
    uint32_t o = be32(e + 4);
    if (o + 4 > t.len || !(pid == 0 || (pid == 3 && (eid == 1 || eid == 10))))
      continue;
    int fmt = be16(g_cmap + o), score = fmt == 12 ? 2 : fmt == 4 ? 1 : 0;
    if (score > best) {
      best = score;
      g_cmap_sub = o;
      g_cmap_fmt = fmt;
    }
  }
  return best ? 0 : -1;
}

static int cmap_glyph(uint32_t cp) {
  const uint8_t *c = g_cmap + g_cmap_sub;
  uint32_t room = g_cmap_len - g_cmap_sub;
  if (g_cmap_fmt == 12) {
    if (room < 16)
      return 0;
    uint32_t ng = be32(c + 12);
    if (ng > (room - 16) / 12)
      ng = (room - 16) / 12;
    uint32_t lo = 0, hi = ng;
    while (lo < hi) {
      uint32_t m = (lo + hi) / 2;
      const uint8_t *g = c + 16 + 12 * m;
      if (cp < be32(g))
        hi = m;
      else if (cp > be32(g + 4))
        lo = m + 1;
      else
        return (int)(be32(g + 8) + cp - be32(g));
    }
    return 0;
  }
  if (g_cmap_fmt == 4 && cp < 0x10000 && room >= 14) {
    int segs = be16(c + 6) / 2;
    if ((uint32_t)(16 + 8 * segs) > room)
      return 0;
    const uint8_t *ends = c + 14, *starts = ends + 2 * segs + 2, *deltas = starts + 2 * segs,
                  *ranges = deltas + 2 * segs;
    for (int i = 0; i < segs; i++) {
      if (cp > be16(ends + 2 * i))
        continue;
      if (cp < be16(starts + 2 * i))
        return 0;
      int ro = be16(ranges + 2 * i);
      if (!ro)
        return (int)((cp + be16(deltas + 2 * i)) & 0xffff);
      const uint8_t *gp = ranges + 2 * i + ro + 2 * (cp - be16(starts + 2 * i));
      if (gp + 2 > g_cmap + g_cmap_len)
        return 0;
      int g = be16(gp);
      return g ? (int)((g + be16(deltas + 2 * i)) & 0xffff) : 0;
    }
  }
  return 0;
}

/* ---- GSUB: the ligature of glyphs gl[0..n-1] (a flag, a keycap) */
static int coverage_index(uint32_t cov, int g) {
  if (cov + 4 > g_gsub_len)
    return -1;
  const uint8_t *c = g_gsub + cov;
  int fmt = be16(c), n = be16(c + 2);
  if (fmt == 1) {
    for (int i = 0; i < n && cov + 4 + 2 * (uint32_t)i + 2 <= g_gsub_len; i++)
      if (be16(c + 4 + 2 * i) == g)
        return i;
  } else if (fmt == 2) {
    for (int i = 0; i < n && cov + 4 + 6 * (uint32_t)i + 6 <= g_gsub_len; i++) {
      const uint8_t *r = c + 4 + 6 * i;
      if (g >= be16(r) && g <= be16(r + 2))
        return be16(r + 4) + g - be16(r);
    }
  }
  return -1;
}

static int ligature_in(uint32_t st, const int *gl, int n) {
  if (st + 6 > g_gsub_len || be16(g_gsub + st) != 1)
    return 0;
  int ci = coverage_index(st + be16(g_gsub + st + 2), gl[0]);
  int nsets = be16(g_gsub + st + 4);
  if (ci < 0 || ci >= nsets || st + 6 + 2 * (uint32_t)ci + 2 > g_gsub_len)
    return 0;
  uint32_t set = st + be16(g_gsub + st + 6 + 2 * ci);
  if (set + 2 > g_gsub_len)
    return 0;
  int nl = be16(g_gsub + set);
  for (int i = 0; i < nl && set + 2 + 2 * (uint32_t)i + 2 <= g_gsub_len; i++) {
    uint32_t lig = set + be16(g_gsub + set + 2 + 2 * i);
    if (lig + 4 > g_gsub_len)
      continue;
    int comps = be16(g_gsub + lig + 2);
    if (comps != n || lig + 4 + 2 * (uint32_t)(comps - 1) > g_gsub_len)
      continue;
    int ok = 1;
    for (int k = 1; k < n && ok; k++)
      ok = be16(g_gsub + lig + 4 + 2 * (k - 1)) == gl[k];
    if (ok)
      return be16(g_gsub + lig);
  }
  return 0;
}

static int gsub_ligature(const int *gl, int n) {
  if (!g_gsub || g_gsub_len < 10 || n < 2)
    return 0;
  uint32_t ll = be16(g_gsub + 8);
  if (ll + 2 > g_gsub_len)
    return 0;
  int nlookups = be16(g_gsub + ll);
  for (int i = 0; i < nlookups && ll + 2 + 2 * (uint32_t)i + 2 <= g_gsub_len; i++) {
    uint32_t lk = ll + be16(g_gsub + ll + 2 + 2 * i);
    if (lk + 6 > g_gsub_len)
      continue;
    int type = be16(g_gsub + lk), nsub = be16(g_gsub + lk + 4);
    for (int s = 0; s < nsub && lk + 6 + 2 * (uint32_t)s + 2 <= g_gsub_len; s++) {
      uint32_t st = lk + be16(g_gsub + lk + 6 + 2 * s);
      int t = type;
      if (t == 7 && st + 8 <= g_gsub_len && be16(g_gsub + st) == 1) {
        t = be16(g_gsub + st + 2);
        st += be32(g_gsub + st + 4);
      }
      if (t == 4) {
        int g = ligature_in(st, gl, n);
        if (g)
          return g;
      }
    }
  }
  return 0;
}

/* ---- the bitmaps: the strike nearest above 64 pixels (the menus' names
 * are 30..72 pixels high), else the largest */
static int pick_ppem(int have, int best) {
  if (!best)
    return 1;
  if (best < 64)
    return have > best;
  return have >= 64 && have < best;
}

static int load_sbix(uint32_t base) {
  Tab t;
  if (!find_table(base, "sbix", &t))
    return -1;
  uint8_t h[8];
  if (rd(t.off, h, 8))
    return -1;
  uint32_t n = be32(h + 4), best_off = 0;
  int best = 0;
  for (uint32_t i = 0; i < n && i < 32; i++) {
    uint8_t o[4], s[4];
    if (rd(t.off + 8 + 4 * i, o, 4) || rd(t.off + be32(o), s, 4))
      continue;
    int ppem = be16(s);
    if (ppem > 0 && pick_ppem(ppem, best)) {
      best = ppem;
      best_off = t.off + be32(o);
    }
  }
  if (!best || g_nglyphs <= 0)
    return -1;
  uint32_t cnt = (uint32_t)g_nglyphs + 1;
  uint8_t *raw = rd_alloc(best_off + 4, cnt * 4);
  g_sbix_offs = malloc(cnt * sizeof *g_sbix_offs);
  if (!raw || !g_sbix_offs) {
    free(raw);
    return -1;
  }
  for (uint32_t i = 0; i < cnt; i++)
    g_sbix_offs[i] = best_off + be32(raw + 4 * i);
  free(raw);
  g_ppem = best;
  g_kind = 1;
  return 0;
}

static int load_cbdt(uint32_t base) {
  Tab cl, cd;
  if (!find_table(base, "CBLC", &cl) || !find_table(base, "CBDT", &cd) || !(g_cblc = rd_alloc(cl.off, cl.len)))
    return -1;
  g_cblc_len = cl.len;
  g_cbdt = cd.off;
  uint32_t n = cl.len >= 8 ? be32(g_cblc + 4) : 0;
  int best = 0;
  for (uint32_t i = 0; i < n && 8 + 48 * i + 48 <= cl.len; i++) {
    int ppem = g_cblc[8 + 48 * i + 45]; /* ppemY */
    if (ppem > 0 && pick_ppem(ppem, best)) {
      best = ppem;
      g_cb_size = 8 + 48 * i;
    }
  }
  if (!best)
    return -1;
  g_ppem = best;
  g_kind = 2;
  return 0;
}

static int load_font(const char *path) {
  if (!(g_f = fopen(path, "rb")))
    return -1;
  uint8_t h[12];
  if (rd(0, h, 12)) {
    fclose(g_f);
    g_f = NULL;
    return -1;
  }
  /* a collection: the first font in it with colour bitmaps */
  uint32_t bases[8];
  int nb = 0;
  if (!memcmp(h, "ttcf", 4)) {
    uint32_t n = be32(h + 8);
    for (uint32_t i = 0; i < n && nb < 8; i++) {
      uint8_t o[4];
      if (!rd(12 + 4 * i, o, 4))
        bases[nb++] = be32(o);
    }
  } else {
    bases[nb++] = 0;
  }
  for (int i = 0; i < nb; i++) {
    uint32_t base = bases[i];
    Tab t;
    uint8_t b[64];
    if (!find_table(base, "maxp", &t) || rd(t.off, b, 6))
      continue;
    g_nglyphs = be16(b + 4);
    g_upem = find_table(base, "head", &t) && !rd(t.off, b, 20) ? be16(b + 18) : 1000;
    if (g_upem <= 0)
      g_upem = 1000;
    if (load_cmap(base))
      continue;
    if (load_sbix(base) && load_cbdt(base)) {
      free(g_cmap);
      g_cmap = NULL;
      continue;
    }
    if (find_table(base, "GSUB", &t) && (g_gsub = rd_alloc(t.off, t.len)))
      g_gsub_len = t.len;
    if (find_table(base, "post", &t) && t.len < (1u << 20) && (g_post = rd_alloc(t.off, t.len)))
      g_post_len = t.len;
    if (find_table(base, "hhea", &t) && !rd(t.off, b, 36))
      g_nhm = be16(b + 34);
    if (g_nhm > 0 && find_table(base, "hmtx", &t) && (g_hmtx = rd_alloc(t.off, t.len)))
      g_hmtx_len = t.len;
    return 0;
  }
  fclose(g_f);
  g_f = NULL;
  return -1;
}

static int names_emoji(const char *s) {
  for (; *s; s++) {
    const char *w = "emoji";
    int i = 0;
    while (w[i] && s[i] && tolower((unsigned char)s[i]) == w[i])
      i++;
    if (!w[i])
      return 1;
  }
  return 0;
}

static int try_font(const char *name) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", dcr_game_root(), name);
  FILE *t = fopen(p, "rb");
  if (!t)
    return -1;
  fclose(t);
  if (load_font(p) == 0) {
    g_state = 1;
    debugPrintf("[emoji] %s: %s bitmaps, %d px, %d glyphs%s%s\n", name, g_kind == 1 ? "sbix" : "CBDT", g_ppem,
                g_nglyphs, g_gsub ? ", GSUB ligatures" : "", g_post ? ", glyph names" : "");
    return 0;
  }
  debugPrintf("[emoji] %s: not a colour emoji font (sbix or CBDT) that can be read\n", name);
  return -1;
}

int lab_emoji_init(void) {
  if (g_state)
    return g_state > 0 ? 0 : -1;
  g_state = -1;
  for (unsigned i = 0; i < sizeof k_names / sizeof k_names[0]; i++)
    if (try_font(k_names[i]) == 0)
      return 0;
  /* else any font file with "emoji" in its name, as it was copied */
  DIR *d = opendir(dcr_game_root());
  if (d) {
    struct dirent *e;
    while ((e = readdir(d))) {
      size_t n = strlen(e->d_name);
      if (n < 5 || !names_emoji(e->d_name))
        continue;
      const char *ext = e->d_name + n - 4;
      if (strcasecmp(ext, ".ttf") && strcasecmp(ext, ".ttc") && strcasecmp(ext, ".otf"))
        continue;
      int known = 0;
      for (unsigned i = 0; i < sizeof k_names / sizeof k_names[0] && !known; i++)
        known = !strcmp(e->d_name, k_names[i]);
      if (!known && try_font(e->d_name) == 0) {
        closedir(d);
        return 0;
      }
    }
    closedir(d);
  }
  debugPrintf("[emoji] no emoji font on the SD card (switch/labyrinth2_nx/emoji.ttf): emoji in names are left out\n");
  return -1;
}

int lab_emoji_ready(void) { return lab_emoji_init() == 0; }

/* ------------------------------------------------------------ text */
static int is_pua(uint32_t cp) { return cp >= 0xE000 && cp <= 0xF8FF; }

static int emoji_range(uint32_t cp) {
  return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2190 && cp <= 0x2BFF) || cp == 0x3030 || cp == 0x303D ||
         cp == 0x3297 || cp == 0x3299 || cp == 0x203C || cp == 0x2049 || cp == 0x2122 || cp == 0x2139;
}

static int utf8_dec(const char *s, uint32_t *cp) {
  const uint8_t *p = (const uint8_t *)s;
  if (p[0] < 0x80) {
    *cp = p[0];
    return 1;
  }
  if ((p[0] & 0xe0) == 0xc0 && p[1]) {
    *cp = (uint32_t)(p[0] & 31) << 6 | (p[1] & 63u);
    return 2;
  }
  if ((p[0] & 0xf0) == 0xe0 && p[1] && p[2]) {
    *cp = (uint32_t)(p[0] & 15) << 12 | (uint32_t)(p[1] & 63) << 6 | (p[2] & 63u);
    return 3;
  }
  if ((p[0] & 0xf8) == 0xf0 && p[1] && p[2] && p[3]) {
    *cp = (uint32_t)(p[0] & 7) << 18 | (uint32_t)(p[1] & 63) << 12 | (uint32_t)(p[2] & 63) << 6 | (p[3] & 63u);
    return 4;
  }
  *cp = p[0];
  return 1;
}

/* post format 2: the glyph called `name` (0: none) */
static int post_glyph(const char *name) {
  if (!g_post || g_post_len < 34 || be32(g_post) != 0x20000)
    return 0;
  int n = be16(g_post + 32);
  if (34 + 2 * (uint32_t)n > g_post_len)
    return 0;
  /* the names after the index, in order: the k-th is index 258 + k */
  size_t nl = strlen(name);
  uint32_t q = 34 + 2 * (uint32_t)n;
  int k = 0, want = -1;
  while (q < g_post_len) {
    uint32_t l = g_post[q];
    if (q + 1 + l > g_post_len)
      break;
    if (l == nl && !memcmp(g_post + q + 1, name, nl)) {
      want = 258 + k;
      break;
    }
    q += 1 + l;
    k++;
  }
  if (want < 0)
    return 0;
  for (int g = 0; g < n; g++)
    if (be16(g_post + 34 + 2 * g) == want)
      return g;
  return 0;
}

static int glyph_of(const uint32_t *cps, int n) {
  if (n == 1)
    return cmap_glyph(cps[0]);
  /* a few sequences remembered: widths are measured every frame */
  static struct {
    uint32_t a, b;
    int g;
  } memo[16];
  static int next;
  for (int i = 0; i < 16; i++)
    if (memo[i].a == cps[0] && memo[i].b == cps[1] && memo[i].a)
      return memo[i].g;
  int gl[4], g = 0, ok = 1;
  for (int i = 0; i < n && ok; i++)
    ok = (gl[i] = cmap_glyph(cps[i])) != 0;
  if (ok)
    g = gsub_ligature(gl, n);
  if (!g) {
    char name[32];
    snprintf(name, sizeof name, "u%04X_u%04X", (unsigned)cps[0], (unsigned)cps[1]);
    g = post_glyph(name);
  }
  memo[next].a = cps[0], memo[next].b = cps[1], memo[next].g = g;
  next = (next + 1) % 16;
  return g;
}

static float advance_em(int g) {
  if (g_hmtx && g_nhm > 0) {
    int i = g < g_nhm ? g : g_nhm - 1;
    if ((uint32_t)(4 * i + 2) <= g_hmtx_len)
      return (float)be16(g_hmtx + 4 * i) / (float)g_upem;
  }
  return 1.15f;
}

int lab_emoji_match(const char *s, int *glyph, float *adv_em) {
  *glyph = 0;
  *adv_em = 0;
  const uint8_t c0 = (uint8_t)s[0];
  /* a keycap: 0-9 # *, (U+FE0F), U+20E3 */
  if ((c0 >= '0' && c0 <= '9') || c0 == '#' || c0 == '*') {
    const char *q = s + 1;
    if (!strncmp(q, "\xef\xb8\x8f", 3))
      q += 3;
    if (strncmp(q, "\xe2\x83\xa3", 3))
      return 0;
    uint32_t seq[2] = {c0, 0x20E3};
    int g = lab_emoji_init() == 0 ? glyph_of(seq, 2) : 0;
    if (g) {
      *glyph = g;
      *adv_em = advance_em(g);
      return (int)(q + 3 - s);
    }
    return 0; /* the digit as text, then the lone U+20E3 below */
  }
  if (c0 < 0xe2)
    return 0;
  uint32_t cp;
  int n = utf8_dec(s, &cp);
  if (cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || cp == 0x20E3 || (cp >= 0x1F3FB && cp <= 0x1F3FF))
    return n; /* selectors, joiners, a lone keycap mark or skin tone: nothing */
  int have = lab_emoji_init() == 0;
  uint32_t seq[2] = {cp, 0};
  int len = 1, used = n;
  if (cp >= 0xE001 && cp <= 0xE5FF) {
    /* the iPhone's own: as is if the font has it, else its Unicode */
    int g = have ? cmap_glyph(cp) : 0;
    if (!g && have) {
      int lo = 0, hi = (int)(sizeof k_softbank / sizeof k_softbank[0]);
      while (lo < hi) {
        int m = (lo + hi) / 2;
        if (k_softbank[m].pua < cp)
          lo = m + 1;
        else
          hi = m;
      }
      if (lo < (int)(sizeof k_softbank / sizeof k_softbank[0]) && k_softbank[lo].pua == cp) {
        seq[0] = k_softbank[lo].cp[0];
        seq[1] = k_softbank[lo].cp[1];
        len = seq[1] ? 2 : 1;
        g = glyph_of(seq, len);
        if (!g && len == 2 && seq[0] >= 0x80)
          g = cmap_glyph(seq[0]); /* no ligature: its first part */
      }
    }
    if (!strncmp(s + used, "\xef\xb8\x8f", 3))
      used += 3;
    *glyph = g;
    *adv_em = g ? advance_em(g) : 0;
    return used;
  }
  if (is_pua(cp))
    return have && (*glyph = cmap_glyph(cp)) ? (*adv_em = advance_em(*glyph), n) : n;
  if (!have || !emoji_range(cp))
    return 0;
  /* a flag: two regional indicators */
  if (cp >= 0x1F1E6 && cp <= 0x1F1FF) {
    uint32_t c2;
    int n2 = utf8_dec(s + n, &c2);
    if (c2 >= 0x1F1E6 && c2 <= 0x1F1FF) {
      seq[1] = c2;
      len = 2;
      used += n2;
    }
  }
  int g = glyph_of(seq, len);
  if (!g && len == 2) {
    len = 1;
    used = n;
    g = cmap_glyph(cp);
  }
  if (!g)
    return 0; /* the shared fonts', then */
  if (!strncmp(s + used, "\xef\xb8\x8f", 3))
    used += 3;
  *glyph = g;
  *adv_em = advance_em(g);
  return used;
}

/* ------------------------------------------------------------ pictures */
static uint8_t *sbix_png(int g, int depth, uint32_t *len, int *ox, int *oy) {
  if (g < 0 || g >= g_nglyphs || depth > 2)
    return NULL;
  uint32_t a = g_sbix_offs[g], b = g_sbix_offs[g + 1];
  if (b <= a + 8 || b - a > (4u << 20))
    return NULL;
  uint8_t *d = rd_alloc(a, b - a);
  if (!d)
    return NULL;
  *ox = bes16(d);
  *oy = bes16(d + 2);
  if (!memcmp(d + 4, "dupe", 4) && b - a >= 10) {
    int to = be16(d + 8);
    free(d);
    return sbix_png(to, depth + 1, len, ox, oy);
  }
  if (memcmp(d + 4, "png ", 4) && memcmp(d + 4, "jpg ", 4)) {
    free(d);
    return NULL;
  }
  *len = b - a - 8;
  memmove(d, d + 8, *len);
  return d;
}

/* CBLC's index for glyph g in the chosen size: the image's CBDT offset,
 * length, image format, and big metrics when the index holds them */
static int cb_locate(int g, uint32_t *off, uint32_t *len, int *ifmt, uint8_t big[8], int *have_big) {
  const uint8_t *bs = g_cblc + g_cb_size;
  uint32_t arr = be32(bs), nsub = be32(bs + 8);
  *have_big = 0;
  for (uint32_t i = 0; i < nsub && arr + 8 * i + 8 <= g_cblc_len; i++) {
    const uint8_t *e = g_cblc + arr + 8 * i;
    int first = be16(e), last = be16(e + 2);
    if (g < first || g > last)
      continue;
    uint32_t sh = arr + be32(e + 4);
    if (sh + 8 > g_cblc_len)
      return -1;
    int idx = be16(g_cblc + sh);
    *ifmt = be16(g_cblc + sh + 2);
    uint32_t data = be32(g_cblc + sh + 4), k = (uint32_t)(g - first);
    if (idx == 1 && sh + 8 + 4 * (k + 2) <= g_cblc_len) {
      uint32_t a = be32(g_cblc + sh + 8 + 4 * k), b = be32(g_cblc + sh + 12 + 4 * k);
      *off = data + a, *len = b > a ? b - a : 0;
    } else if (idx == 3 && sh + 8 + 2 * (k + 2) <= g_cblc_len) {
      uint32_t a = be16(g_cblc + sh + 8 + 2 * k), b = be16(g_cblc + sh + 10 + 2 * k);
      *off = data + a, *len = b > a ? b - a : 0;
    } else if (idx == 2 && sh + 20 <= g_cblc_len) {
      uint32_t size = be32(g_cblc + sh + 8);
      memcpy(big, g_cblc + sh + 12, 8);
      *have_big = 1;
      *off = data + size * k, *len = size;
    } else if ((idx == 4 || idx == 5) && sh + 12 <= g_cblc_len) {
      uint32_t p = sh + 8, size = 0;
      if (idx == 5) {
        if (sh + 24 > g_cblc_len)
          return -1;
        size = be32(g_cblc + sh + 8);
        memcpy(big, g_cblc + sh + 12, 8);
        *have_big = 1;
        p = sh + 20;
      }
      uint32_t ng = be32(g_cblc + p);
      p += 4;
      for (uint32_t j = 0; j < ng; j++) {
        if (idx == 4) {
          if (p + 4 * (j + 2) > g_cblc_len)
            return -1;
          if (be16(g_cblc + p + 4 * j) == g) {
            uint32_t a = be16(g_cblc + p + 4 * j + 2), b = be16(g_cblc + p + 4 * (j + 1) + 2);
            *off = data + a, *len = b > a ? b - a : 0;
            return *len ? 0 : -1;
          }
        } else {
          if (p + 2 * (j + 1) > g_cblc_len)
            return -1;
          if (be16(g_cblc + p + 2 * j) == g) {
            *off = data + size * j, *len = size;
            return 0;
          }
        }
      }
      return -1;
    } else {
      return -1;
    }
    return *len ? 0 : -1;
  }
  return -1;
}

static uint8_t *cbdt_png(int g, uint32_t *len, int *left, int *top) {
  uint32_t off, n;
  int ifmt, have_big;
  uint8_t big[8] = {0};
  if (cb_locate(g, &off, &n, &ifmt, big, &have_big) || n > (4u << 20))
    return NULL;
  uint8_t *d = rd_alloc(g_cbdt + off, n);
  if (!d)
    return NULL;
  uint32_t head = 0;
  if (ifmt == 17 && n >= 9) {
    *left = (int8_t)d[2], *top = (int8_t)d[3];
    head = 5;
  } else if (ifmt == 18 && n >= 12) {
    *left = (int8_t)d[2], *top = (int8_t)d[3];
    head = 8;
  } else if (ifmt == 19 && have_big && n >= 4) {
    *left = (int8_t)big[2], *top = (int8_t)big[3];
    head = 0;
  } else {
    free(d);
    return NULL;
  }
  uint32_t dl = be32(d + head);
  if (dl > n - head - 4) {
    free(d);
    return NULL;
  }
  memmove(d, d + head + 4, dl);
  *len = dl;
  return d;
}

uint8_t *lab_emoji_rgba(int g, int *w, int *h, float *left_em, float *top_em, float *px_em) {
  if (lab_emoji_init() != 0 || g <= 0)
    return NULL;
  uint32_t len = 0;
  int ox = 0, oy = 0;
  uint8_t *png = g_kind == 1 ? sbix_png(g, 0, &len, &ox, &oy) : cbdt_png(g, &len, &ox, &oy);
  if (!png)
    return NULL;
  uint8_t *rgba = lab_image_decode(png, len, w, h);
  free(png);
  if (!rgba)
    return NULL;
  float pp = (float)g_ppem;
  *left_em = (float)ox / pp;
  /* sbix: the bitmap's bottom edge is oy above the baseline; CBDT: its top
   * edge is `top` above it */
  *top_em = g_kind == 1 ? (float)(oy + *h) / pp : (float)oy / pp;
  *px_em = 1.0f / pp;
  return rgba;
}
