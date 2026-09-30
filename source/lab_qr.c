/* lab_qr.c -- a QR code for a short text (the level editor's address, so a
 * phone can open it): ISO/IEC 18004, byte mode, error correction level M,
 * versions 1 to 10 (up to 213 bytes), the mask with the lowest penalty.
 * The drawing is the caller's (a square of dark/light modules). MIT. */
#include <stdint.h>
#include <string.h>

#include "lab_qr.h"

/* level M: error correction codewords per block, and the blocks
 * (group 1 count, data per block; group 2 count, data per block) */
static const struct {
  uint8_t ecc, b1, d1, b2, d2;
} k_ver[11] = {
    {0, 0, 0, 0, 0},     {10, 1, 16, 0, 0},  {16, 1, 28, 0, 0},  {26, 1, 44, 0, 0},
    {18, 2, 32, 0, 0},   {24, 2, 43, 0, 0},  {16, 4, 27, 0, 0},  {18, 4, 31, 0, 0},
    {22, 2, 38, 2, 39},  {22, 3, 36, 2, 37}, {26, 4, 43, 1, 44},
};

static int data_capacity(int v) { return k_ver[v].b1 * k_ver[v].d1 + k_ver[v].b2 * k_ver[v].d2; }

/* ---------------------------------------------------------------- GF(256) */
static uint8_t gmul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  while (b) {
    if (b & 1)
      r ^= a;
    a = (uint8_t)((a << 1) ^ (a & 0x80 ? 0x1d : 0));
    b >>= 1;
  }
  return r;
}

static void rs_divisor(int deg, uint8_t *out) {
  memset(out, 0, (size_t)deg);
  out[deg - 1] = 1;
  uint8_t root = 1;
  for (int i = 0; i < deg; i++) {
    for (int j = 0; j < deg; j++) {
      out[j] = gmul(out[j], root);
      if (j + 1 < deg)
        out[j] ^= out[j + 1];
    }
    root = gmul(root, 2);
  }
}

static void rs_remainder(const uint8_t *data, int n, const uint8_t *div, int deg, uint8_t *out) {
  memset(out, 0, (size_t)deg);
  for (int i = 0; i < n; i++) {
    uint8_t f = data[i] ^ out[0];
    memmove(out, out + 1, (size_t)(deg - 1));
    out[deg - 1] = 0;
    for (int j = 0; j < deg; j++)
      out[j] ^= gmul(div[j], f);
  }
}

/* ---------------------------------------------------------------- matrix */
typedef struct {
  int n;
  uint8_t m[LAB_QR_MAX * LAB_QR_MAX];   /* 1 dark */
  uint8_t fn[LAB_QR_MAX * LAB_QR_MAX];  /* 1 a function module (not data) */
} Q;

static void set(Q *q, int x, int y, int dark, int function) {
  q->m[y * q->n + x] = (uint8_t)dark;
  if (function)
    q->fn[y * q->n + x] = 1;
}

static void finder(Q *q, int cx, int cy) {
  for (int dy = -4; dy <= 4; dy++)
    for (int dx = -4; dx <= 4; dx++) {
      int x = cx + dx, y = cy + dy;
      if (x < 0 || y < 0 || x >= q->n || y >= q->n)
        continue;
      int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy, r = d > e ? d : e;
      set(q, x, y, r != 2 && r != 4, 1);
    }
}

static void alignment(Q *q, int cx, int cy) {
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy, r = d > e ? d : e;
      set(q, cx + dx, cy + dy, r != 1, 1);
    }
}

static int align_positions(int v, int *out) {
  if (v == 1)
    return 0;
  int num = v / 7 + 2, n = v * 4 + 17;
  int step = v == 32 ? 26 : ((v * 4 + num * 2 + 1) / (num * 2 - 2)) * 2;
  out[0] = 6;
  for (int i = num - 1, pos = n - 7; i >= 1; i--, pos -= step)
    out[i] = pos;
  return num;
}

static void format_bits(Q *q, int mask) {
  /* level M = 0b00 */
  int data = (0 << 3) | mask, rem = data;
  for (int i = 0; i < 10; i++)
    rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  int bits = ((data << 10) | rem) ^ 0x5412;
  for (int i = 0; i <= 5; i++)
    set(q, 8, i, (bits >> i) & 1, 1);
  set(q, 8, 7, (bits >> 6) & 1, 1);
  set(q, 8, 8, (bits >> 7) & 1, 1);
  set(q, 7, 8, (bits >> 8) & 1, 1);
  for (int i = 9; i < 15; i++)
    set(q, 14 - i, 8, (bits >> i) & 1, 1);
  for (int i = 0; i < 8; i++)
    set(q, q->n - 1 - i, 8, (bits >> i) & 1, 1);
  for (int i = 8; i < 15; i++)
    set(q, 8, q->n - 15 + i, (bits >> i) & 1, 1);
  set(q, 8, q->n - 8, 1, 1); /* the dark module */
}

static void version_bits(Q *q, int v) {
  if (v < 7)
    return;
  int rem = v;
  for (int i = 0; i < 12; i++)
    rem = (rem << 1) ^ ((rem >> 11) * 0x1f25);
  long bits = ((long)v << 12) | rem;
  for (int i = 0; i < 18; i++) {
    int bit = (bits >> i) & 1, a = q->n - 11 + i % 3, b = i / 3;
    set(q, a, b, bit, 1);
    set(q, b, a, bit, 1);
  }
}

static void function_patterns(Q *q, int v) {
  for (int i = 0; i < q->n; i++) {
    set(q, 6, i, i % 2 == 0, 1);
    set(q, i, 6, i % 2 == 0, 1);
  }
  finder(q, 3, 3);
  finder(q, q->n - 4, 3);
  finder(q, 3, q->n - 4);
  int pos[7], np = align_positions(v, pos);
  for (int i = 0; i < np; i++)
    for (int j = 0; j < np; j++)
      if (!((i == 0 && j == 0) || (i == 0 && j == np - 1) || (i == np - 1 && j == 0)))
        alignment(q, pos[i], pos[j]);
  format_bits(q, 0); /* reserved now, written with the mask later */
  version_bits(q, v);
}

static void place_codewords(Q *q, const uint8_t *cw, int len) {
  int i = 0;
  for (int right = q->n - 1; right >= 1; right -= 2) {
    if (right == 6)
      right = 5;
    for (int vert = 0; vert < q->n; vert++)
      for (int j = 0; j < 2; j++) {
        int x = right - j, upward = ((right + 1) & 2) == 0;
        int y = upward ? q->n - 1 - vert : vert;
        if (!q->fn[y * q->n + x] && i < len * 8) {
          q->m[y * q->n + x] = (cw[i >> 3] >> (7 - (i & 7))) & 1;
          i++;
        }
      }
  }
}

static int mask_bit(int mask, int x, int y) {
  switch (mask) {
  case 0: return (x + y) % 2 == 0;
  case 1: return y % 2 == 0;
  case 2: return x % 3 == 0;
  case 3: return (x + y) % 3 == 0;
  case 4: return (x / 3 + y / 2) % 2 == 0;
  case 5: return x * y % 2 + x * y % 3 == 0;
  case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
  default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
  }
}

static void apply_mask(Q *q, int mask) {
  for (int y = 0; y < q->n; y++)
    for (int x = 0; x < q->n; x++)
      if (!q->fn[y * q->n + x] && mask_bit(mask, x, y))
        q->m[y * q->n + x] ^= 1;
}

static long penalty(const Q *q) {
  long p = 0;
  const int n = q->n;
  /* runs of 5+ in rows and columns */
  for (int pass = 0; pass < 2; pass++)
    for (int a = 0; a < n; a++) {
      int run = 1;
      for (int b = 1; b < n; b++) {
        int c0 = pass ? q->m[(b - 1) * n + a] : q->m[a * n + b - 1], c1 = pass ? q->m[b * n + a] : q->m[a * n + b];
        if (c0 == c1) {
          run++;
          if (run == 5)
            p += 3;
          else if (run > 5)
            p++;
        } else {
          run = 1;
        }
      }
    }
  /* 2x2 blocks */
  for (int y = 0; y + 1 < n; y++)
    for (int x = 0; x + 1 < n; x++) {
      int c = q->m[y * n + x];
      if (c == q->m[y * n + x + 1] && c == q->m[(y + 1) * n + x] && c == q->m[(y + 1) * n + x + 1])
        p += 3;
    }
  /* finder-like patterns 1011101 with 4 light on a side */
  static const uint8_t pat[2][11] = {{1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1}};
  for (int pass = 0; pass < 2; pass++)
    for (int a = 0; a < n; a++)
      for (int b = 0; b + 11 <= n; b++)
        for (int k = 0; k < 2; k++) {
          int ok = 1;
          for (int i = 0; i < 11 && ok; i++)
            ok = (pass ? q->m[(b + i) * n + a] : q->m[a * n + b + i]) == pat[k][i];
          if (ok)
            p += 40;
        }
  /* balance */
  int dark = 0;
  for (int i = 0; i < n * n; i++)
    dark += q->m[i];
  int total = n * n, dev = dark * 20 - total * 10;
  if (dev < 0)
    dev = -dev;
  p += (long)((dev + total - 1) / total - 1 > 0 ? (dev + total - 1) / total - 1 : 0) * 10;
  return p;
}

int lab_qr_encode(const char *text, uint8_t *modules, int *size) {
  size_t len = strlen(text);
  int v = 1;
  /* byte mode: 4 bits mode, 8 bits count (16 from version 10), the bytes */
  for (; v <= 10; v++) {
    int bits = 4 + (v < 10 ? 8 : 16) + (int)len * 8;
    if (bits <= data_capacity(v) * 8)
      break;
  }
  if (v > 10)
    return -1;
  int cap = data_capacity(v);
  uint8_t data[400];
  memset(data, 0, sizeof data);
  int bit = 0;
#define PUT(val, nb)                                         \
  for (int i_ = (nb) - 1; i_ >= 0; i_--, bit++)              \
    if (((val) >> i_) & 1)                                    \
      data[bit >> 3] |= (uint8_t)(0x80 >> (bit & 7));
  PUT(4, 4);
  PUT((int)len, v < 10 ? 8 : 16);
  for (size_t i = 0; i < len; i++)
    PUT((uint8_t)text[i], 8);
  int term = cap * 8 - bit < 4 ? cap * 8 - bit : 4;
  bit += term;
  bit = (bit + 7) & ~7;
  for (int pad = 0xec; bit < cap * 8; pad ^= 0xec ^ 0x11) {
    PUT(pad, 8);
  }
#undef PUT
  /* the blocks, their error correction, interleaved */
  const int ecc = k_ver[v].ecc, nb = k_ver[v].b1 + k_ver[v].b2;
  uint8_t div[32], blocks_ecc[8][32];
  const uint8_t *bdata[8];
  int blen[8];
  rs_divisor(ecc, div);
  int off = 0;
  for (int b = 0; b < nb; b++) {
    blen[b] = b < k_ver[v].b1 ? k_ver[v].d1 : k_ver[v].d2;
    bdata[b] = data + off;
    rs_remainder(bdata[b], blen[b], div, ecc, blocks_ecc[b]);
    off += blen[b];
  }
  uint8_t cw[500];
  int n = 0, maxlen = k_ver[v].d2 > k_ver[v].d1 ? k_ver[v].d2 : k_ver[v].d1;
  for (int i = 0; i < maxlen; i++)
    for (int b = 0; b < nb; b++)
      if (i < blen[b])
        cw[n++] = bdata[b][i];
  for (int i = 0; i < ecc; i++)
    for (int b = 0; b < nb; b++)
      cw[n++] = blocks_ecc[b][i];

  static Q q, best;
  long best_p = -1;
  for (int mask = 0; mask < 8; mask++) {
    memset(&q, 0, sizeof q);
    q.n = v * 4 + 17;
    function_patterns(&q, v);
    place_codewords(&q, cw, n);
    apply_mask(&q, mask);
    format_bits(&q, mask);
    long p = penalty(&q);
    if (best_p < 0 || p < best_p) {
      best_p = p;
      best = q;
    }
  }
  *size = best.n;
  memcpy(modules, best.m, (size_t)best.n * best.n);
  return 0;
}
