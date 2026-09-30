/* lab_online.c -- the Labyrinth 2 level server, as the game's df.java spoke
 * to it.
 *
 * http://contentsystem.labyrinth2.com (Google App Engine, plain HTTP, still
 * up in 2026): community level packs (lists, downloads, ratings) and each
 * player's own packs, made in the web editor at labyrinth2.com with the ID
 * and PIN the server gives the device when it registers. Every request is
 *   <path>?<query>&c=<checksum>
 * where the checksum is 8 hex digits of a random salt ^ 44665651, then 8 of
 * a running sum over the query's bytes ^ 44665651 (df.a(byte[])); the
 * answers are JSON (dg.java reads the body as text). df registers the
 * device in its constructor and again before a request when it has no id
 * yet; the server gives the same id and PIN to the same device each time.
 *
 * THE DEVICE ID. A phone sends did = b(ANDROID_ID) (below). The account is
 * bound to it, so no two consoles may send the same one -- and consoles on
 * emuNAND with a blanked PRODINFO all read the same serial (or none). As
 * bs_joyride_nx does: a salted SHA-256 of the serial when it is a real one,
 * else of the console's user (each profile has a random 128-bit id), else
 * a random id; [online] device_id in config.ini overrides them. The id is
 * kept in data/device_id the first time: it must never change, or the
 * player's packs and PIN would be lost (the user it comes from can change:
 * the last one to open a game). Nothing that identifies the console leaves
 * it: only the hash.
 *
 * One worker thread does the requests, in order; their answers (parsed
 * there) wait for lab_online_poll on the main thread, where the callbacks
 * run and the level table and the files are changed. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lab_net.h"
#include "lab_online.h"

#ifdef __SWITCH__
#include <switch.h>
#include "dcr_config.h"
#include "util.h"
#define OLOG(...) debugPrintf("[online] " __VA_ARGS__)
#else
#include <pthread.h>
#include <unistd.h>
#define OLOG(...) fprintf(stderr, "[online] " __VA_ARGS__)
#endif

#define SERVER "http://contentsystem.labyrinth2.com"
/* Labyrinth 2 HD's: the iPad packs (the same requests, its own accounts) */
#define SERVER_IPAD "http://contentsystem-ipad.labyrinth2.com"
#define UA "User-Agent: Labyrinth2\r\n"

/* ------------------------------------------------------------ the host's bits */
const char *dcr_game_root(void);
const char *lab_reg_get_string(const char *key);
void lab_reg_set_string(const char *key, const char *v);
void lab_reg_save(void);

#ifdef __SWITCH__
static Mutex g_lock;
static CondVar g_cv;
#define LOCK() mutexLock(&g_lock)
#define UNLOCK() mutexUnlock(&g_lock)
#define WAIT() condvarWait(&g_cv, &g_lock)
#define WAKE() condvarWakeAll(&g_cv)
static void rnd(void *b, size_t n) { randomGet(b, n); }
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
#define LOCK() pthread_mutex_lock(&g_lock)
#define UNLOCK() pthread_mutex_unlock(&g_lock)
#define WAIT() pthread_cond_wait(&g_cv, &g_lock)
#define WAKE() pthread_cond_broadcast(&g_cv)
static void rnd(void *b, size_t n) { arc4random_buf(b, n); }
#endif

/* ------------------------------------------------------------ signing */
static void hex8(char *out, uint64_t v) {
  /* Long.toHexString, upper case, at least 8 digits */
  char tmp[20];
  snprintf(tmp, sizeof tmp, "%llX", (unsigned long long)v);
  size_t n = strlen(tmp);
  size_t pad = n < 8 ? 8 - n : 0;
  memset(out, '0', pad);
  memcpy(out + pad, tmp, n + 1);
}

static void checksum(const char *query, char *out) {
  uint64_t j = 0, j2 = 0;
  for (const unsigned char *p = (const unsigned char *)query; *p; p++) {
    uint64_t j3 = (j << 9) & 0xffffffffull;
    j = (j3 | (j3 >> 23)) ^ (uint64_t)*p;
    j2 += j;
  }
  uint32_t r;
  rnd(&r, sizeof r);
  uint64_t salt = (uint64_t)(r % 2147483647u) ^ 44665651u;
  hex8(out, salt);
  hex8(out + strlen(out), j2 ^ 44665651u);
}

static void url_dev(char *out, size_t cap, const char *pq, int ipad) {
  const char *q = strchr(pq, '?');
  char c[48];
  checksum(q ? q + 1 : "", c);
  snprintf(out, cap, "%s%s%sc=%s", ipad ? SERVER_IPAD : SERVER, pq, q ? "&" : "?", c);
}

void lab_online_url(char *out, size_t cap, const char *pq) { url_dev(out, cap, pq, 0); }

/* df.b(String): the id (at least 16 characters), 12 picked from it
 * starting at its first digit's value, then from the end backwards starting
 * at its second's, to 40 */
void lab_online_did(const char *aid, char out[48]) {
  char s[40];
  size_t n = strlen(aid);
  if (n > 16)
    n = 16; /* an Android ID; longer would index before the string (the Java threw) */
  size_t pad = n < 16 ? 16 - n : 0;
  memset(s, '0', pad);
  memcpy(s + pad, aid, n);
  s[pad + n] = 0;
  size_t len = strlen(s), o = 0;
  memcpy(out, s, len);
  o = len;
  char d0[2] = {s[0], 0}, d1[2] = {s[1], 0};
  unsigned i = (unsigned)strtoul(d0, NULL, 16), i3 = (unsigned)strtoul(d1, NULL, 16);
  for (int k = 0; k < 12 && o < 47; k++, i++)
    out[o++] = s[i % len];
  while (o < 40)
    out[o++] = s[15 - (i3++ % len)];
  out[o] = 0;
}

/* ------------------------------------------------------------ the device id */
static char g_aid[33];

static void kept_path(char *out, size_t cap) { snprintf(out, cap, "%s/data/device_id", dcr_game_root()); }

/* SHA-256 (FIPS 180-4): libnx32 has no sha256CalculateHash (the 64-bit
 * libnx's is AArch64 crypto instructions) */
static uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(uint32_t h[8], const uint8_t *p) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1 = hh + (ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
    uint32_t t2 = (ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
  }
  h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
}

void lab_sha256(const void *data, size_t n, uint8_t out[32]);
void lab_sha256(const void *data, size_t n, uint8_t out[32]) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const uint8_t *p = data;
  size_t left = n;
  for (; left >= 64; left -= 64, p += 64)
    sha256_block(h, p);
  uint8_t tail[128] = {0};
  memcpy(tail, p, left);
  tail[left] = 0x80;
  size_t tl = left < 56 ? 64 : 128;
  uint64_t bits = (uint64_t)n * 8;
  for (int i = 0; i < 8; i++)
    tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
  sha256_block(h, tail);
  if (tl == 128)
    sha256_block(h, tail + 64);
  for (int i = 0; i < 8; i++)
    out[i * 4] = (uint8_t)(h[i] >> 24), out[i * 4 + 1] = (uint8_t)(h[i] >> 16), out[i * 4 + 2] = (uint8_t)(h[i] >> 8),
                  out[i * 4 + 3] = (uint8_t)h[i];
}

#ifdef __SWITCH__
static void hash16(const char *seed, char *out) {
  uint8_t d[32];
  lab_sha256(seed, strlen(seed), d);
  for (int i = 0; i < 8; i++)
    snprintf(out + i * 2, 3, "%02x", d[i]);
}

/* a hidden serial reads as nothing, or a three-letter prefix and zeros */
static int serial_is_real(const char *s) {
  size_t n = strlen(s);
  if (n < 10)
    return 0;
  for (size_t i = 3; i < n; i++)
    if (s[i] != '0')
      return 1;
  return 0;
}

/* the user the game was started with, else the last one to open anything,
 * else the first on the console */
static int console_user(AccountUid *uid) {
  if (R_FAILED(accountInitialize(AccountServiceType_Application)))
    return 0;
  int ok = R_SUCCEEDED(accountGetPreselectedUser(uid)) && accountUidIsValid(uid);
  if (!ok)
    ok = R_SUCCEEDED(accountGetLastOpenedUser(uid)) && accountUidIsValid(uid);
  if (!ok) {
    AccountUid users[ACC_USER_LIST_SIZE];
    s32 count = 0;
    if (R_SUCCEEDED(accountListAllUsers(users, ACC_USER_LIST_SIZE, &count)) && count > 0) {
      *uid = users[0];
      ok = accountUidIsValid(uid);
    }
  }
  accountExit();
  return ok;
}

static const char *make_id(void) {
  SetSysSerialNumber serial;
  memset(&serial, 0, sizeof serial);
  int have = R_SUCCEEDED(setsysInitialize()) && R_SUCCEEDED(setsysGetSerialNumber(&serial));
  setsysExit();
  serial.number[sizeof serial.number - 1] = 0;
  char seed[96];
  if (have && serial_is_real(serial.number)) {
    snprintf(seed, sizeof seed, "labyrinth2_nx:%s", serial.number);
    hash16(seed, g_aid);
    return "the console's serial number";
  }
  AccountUid uid;
  if (console_user(&uid)) {
    snprintf(seed, sizeof seed, "labyrinth2_nx:user:%016llx%016llx", (unsigned long long)uid.uid[0],
             (unsigned long long)uid.uid[1]);
    hash16(seed, g_aid);
    return "the console's user (the serial number is hidden)";
  }
  uint64_t r[2];
  rnd(r, sizeof r);
  snprintf(g_aid, sizeof g_aid, "%016llx", (unsigned long long)(r[0] ^ (r[1] << 1)));
  return "a random number (no serial number or user to go by)";
}
#else
static const char *make_id(void) {
  uint64_t r;
  rnd(&r, sizeof r);
  snprintf(g_aid, sizeof g_aid, "%016llx", (unsigned long long)r);
  return "a random number (a PC)";
}
#endif

static int is_hex_id(const char *s, size_t lo, size_t hi) {
  size_t n = strlen(s);
  if (n < lo || n > hi)
    return 0;
  for (size_t i = 0; i < n; i++)
    if (!strchr("0123456789abcdefABCDEF", s[i]))
      return 0;
  return 1;
}

const char *lab_online_device_id(void) {
  LOCK();
  if (g_aid[0]) {
    UNLOCK();
    return g_aid;
  }
#ifdef __SWITCH__
  const char *cfg = dcr_config()->device_id;
#else
  const char *cfg = getenv("LAB_DEVICE_ID") ? getenv("LAB_DEVICE_ID") : "";
#endif
  if (cfg[0] && is_hex_id(cfg, 8, 16)) {
    snprintf(g_aid, sizeof g_aid, "%s", cfg);
    for (char *p = g_aid; *p; p++)
      if (*p >= 'A' && *p <= 'F')
        *p = (char)(*p - 'A' + 'a');
    OLOG("device id: set in config.ini\n");
    UNLOCK();
    return g_aid;
  }
  char path[320];
  kept_path(path, sizeof path);
  FILE *f = fopen(path, "r");
  if (f) {
    char line[64] = {0};
    int ok = fgets(line, sizeof line, f) != NULL;
    fclose(f);
    line[strcspn(line, "\r\n ")] = 0;
    if (ok && is_hex_id(line, 8, 16)) {
      snprintf(g_aid, sizeof g_aid, "%s", line);
      OLOG("device id: kept in data/device_id\n");
      UNLOCK();
      return g_aid;
    }
  }
  const char *from = make_id();
  f = fopen(path, "w");
  if (f) {
    fprintf(f, "%s\n", g_aid);
    fclose(f);
  }
  OLOG("device id: made from %s, kept in data/device_id\n", from);
  UNLOCK();
  return g_aid;
}

/* ------------------------------------------------------------ the account */
static char g_uid[2][32], g_pin[2][32]; /* the worker's, under the lock: each server's */
static int g_registered[2];               /* this run */
static int g_dev;                          /* the server new requests go to (main thread) */

void lab_online_set_ipad(int on) { g_dev = on != 0; }
int lab_online_ipad(void) { return g_dev; }
static char g_show_uid[32], g_show_pin[32]; /* the main thread's: the last known */

static void load_shown(void) {
  static int loaded;
  if (loaded)
    return;
  loaded = 1;
  const char *s = lab_reg_get_string("port-online-id"), *q = lab_reg_get_string("port-online-pin");
  if (s && !g_show_uid[0])
    snprintf(g_show_uid, sizeof g_show_uid, "%s", s);
  if (q && !g_show_pin[0])
    snprintf(g_show_pin, sizeof g_show_pin, "%s", q);
}

const char *lab_online_user_id(void) {
  load_shown();
  return g_show_uid;
}

const char *lab_online_pin(void) {
  load_shown();
  return g_show_pin;
}

int lab_online_enabled(void) {
#ifdef __SWITCH__
  return dcr_config()->online;
#else
  return 1;
#endif
}

/* ------------------------------------------------------------ JSON */
static void jstr(const LabJson *o, const char *k, char *out, size_t cap) {
  const LabJson *v = lab_json_get(o, k);
  const char *s = lab_json_str(v);
  if (s)
    snprintf(out, cap, "%s", s);
  else if (v && v->t == LAB_J_NUM)
    snprintf(out, cap, "%.0f", v->num);
  else
    out[0] = 0;
}

/* A name for the screen: no control characters, no broken UTF-8 (a name
 * cut by the fields' sizes), spaces trimmed. The old iPhone emoji
 * (SoftBank's private-use E001..E537) stay: lab_emoji.c draws them from an
 * emoji font on the SD card, or leaves them out. */
static void clean_text(char *s) {
  unsigned char *r = (unsigned char *)s, *w = r;
  while (*r) {
    int n = *r < 0x80 ? 1 : (*r & 0xe0) == 0xc0 ? 2 : (*r & 0xf0) == 0xe0 ? 3 : (*r & 0xf8) == 0xf0 ? 4 : 0;
    int ok = n > 0;
    for (int i = 1; i < n && ok; i++)
      ok = (r[i] & 0xc0) == 0x80;
    if (!ok) { /* a broken or cut sequence: dropped */
      r++;
      continue;
    }
    if (!(n == 1 && *r < 0x20)) {
      memmove(w, r, (size_t)n);
      w += n;
    }
    r += n;
  }
  *w = 0;
  while (w > (unsigned char *)s && w[-1] == ' ')
    *--w = 0;
  size_t lead = strspn(s, " ");
  if (lead)
    memmove(s, s + lead, strlen(s + lead) + 1);
}

int lab_online_pack_from_json(const LabJson *o, LabNetPack *p) {
  memset(p, 0, sizeof *p);
  if (!o || o->t != LAB_J_OBJ)
    return 0;
  jstr(o, "lid", p->lid, sizeof p->lid);
  jstr(o, "lname", p->name, sizeof p->name);
  jstr(o, "aname", p->author, sizeof p->author);
  clean_text(p->name);
  clean_text(p->author);
  jstr(o, "pdate", p->pdate, sizeof p->pdate);
  p->rev = (int)lab_json_num(lab_json_get(o, "rev"), 0);
  p->rqv = (int)lab_json_num(lab_json_get(o, "rqv"), 0);
  p->theme = (int)lab_json_num(lab_json_get(o, "thm"), 0);
  p->nlevels = (int)lab_json_num(lab_json_get(o, "nun"), 0);
  p->difficulty = (int)lab_json_num(lab_json_get(o, "dif"), -1);
  p->published = (int)lab_json_num(lab_json_get(o, "pst"), 0) == 1;
  p->rating = lab_json_num(lab_json_get(o, "rtg"), 0);
  /* de.a(JSONObject) also dropped every pack whose name has < > or ": the
   * engine's TinyXML could not read its info.xml. lab_files.c escapes that
   * text instead, so they are all listed. */
  return p->lid[0] != 0;
}

/* ------------------------------------------------------------ the queue */
typedef struct Job {
  struct Job *next;
  int kind;
  int ipad;       /* the iPad server */
  char path[600];
  LabOnDone cb;
  void *arg;
  LabOnResult r;
} Job;
static Job *g_todo, *g_todo_tail, *g_done, *g_done_tail;
static int g_busy, g_thread_up;

static void push(Job **head, Job **tail, Job *j) {
  j->next = NULL;
  if (*tail)
    (*tail)->next = j;
  else
    *head = j;
  *tail = j;
}

static Job *pop(Job **head, Job **tail) {
  Job *j = *head;
  if (j) {
    *head = j->next;
    if (!*head)
      *tail = NULL;
  }
  return j;
}

/* one request: the body, status in r */
static char *fetch(const char *pq, LabOnResult *r, size_t *len, int ipad) {
  char url[800];
  url_dev(url, sizeof url, pq, ipad);
  if (!lab_net_online()) {
    r->status = LAB_ON_OFFLINE;
    snprintf(r->msg, sizeof r->msg, "The console is not connected to the internet.");
    return NULL;
  }
  int http = 0;
  char *body = lab_http_fetch("GET", url, UA, NULL, len, &http);
  r->http = http;
  if (!body) {
    r->status = LAB_ON_FAILED;
    snprintf(r->msg, sizeof r->msg, "Couldn't reach the Labyrinth server. Please try later.");
    OLOG("%.60s: no answer\n", pq);
    return NULL;
  }
  if (http != 200) {
    r->status = LAB_ON_REFUSED;
    /* App Engine's error pages: "<title>400 400 BAD REQUEST - ... (why)</title>" */
    const char *t = strstr(body, "<title>");
    if (t) {
      t += 7;
      const char *e = strstr(t, "</title>");
      snprintf(r->msg, sizeof r->msg, "The server said: %.*s", e ? (int)(e - t) : 120, t);
    } else {
      snprintf(r->msg, sizeof r->msg, "The server refused (HTTP %d).", http);
    }
    OLOG("%.60s: HTTP %d\n", pq, http);
    free(body);
    return NULL;
  }
  r->status = LAB_ON_OK;
  return body;
}

static int do_register(LabOnResult *r, int ipad) {
  char did[48], pq[128];
  lab_online_did(lab_online_device_id(), did);
  snprintf(pq, sizeof pq, "/register?did=%s", did);
  size_t len = 0;
  char *body = fetch(pq, r, &len, ipad);
  if (!body)
    return 0;
  LabJson *j = lab_json_parse(body, len);
  char id[32] = "", pin[32] = "";
  if (j) {
    jstr(j, "id", id, sizeof id);
    jstr(j, "pin", pin, sizeof pin);
  }
  lab_json_free(j);
  free(body);
  if (!id[0]) {
    r->status = LAB_ON_FAILED;
    snprintf(r->msg, sizeof r->msg, "The server's answer to the registration could not be read.");
    return 0;
  }
  LOCK();
  int changed = strcmp(id, g_uid[ipad]) || strcmp(pin, g_pin[ipad]);
  snprintf(g_uid[ipad], sizeof g_uid[ipad], "%s", id);
  snprintf(g_pin[ipad], sizeof g_pin[ipad], "%s", pin);
  g_registered[ipad] = 1;
  UNLOCK();
  if (changed)
    OLOG("registered%s: user id %s\n", ipad ? " (the iPad server)" : "", id);
  return 1;
}

static void run(Job *j) {
  LabOnResult *r = &j->r;
  r->kind = j->kind;
  if (!lab_online_enabled()) {
    r->status = LAB_ON_OFFLINE;
    snprintf(r->msg, sizeof r->msg, "Online is off ([online] enabled in config.ini).");
    return;
  }
  if (!g_registered[j->ipad] && !do_register(r, j->ipad))
    return;
  if (j->kind == LAB_JOB_REGISTER)
    return;
  /* the uid is put in now: the registration may have just given it */
  char pq[700];
  const char *u = strstr(j->path, "{uid}");
  if (u)
    snprintf(pq, sizeof pq, "%.*s%s%s", (int)(u - j->path), j->path, g_uid[j->ipad], u + 5);
  else
    snprintf(pq, sizeof pq, "%s", j->path);
  size_t len = 0;
  char *body = fetch(pq, r, &len, j->ipad);
  if (!body)
    return;
  if (j->kind == LAB_JOB_GET) {
    r->data = (uint8_t *)body;
    r->len = len;
    if (len < 4 || memcmp(body, "PK\3\4", 4)) {
      r->status = LAB_ON_FAILED;
      snprintf(r->msg, sizeof r->msg, "The level pack that came back is not a zip file.");
      free(body);
      r->data = NULL;
    }
    return;
  }
  LabJson *root = lab_json_parse(body, len);
  if (j->kind == LAB_JOB_LIST) {
    if (!root || root->t != LAB_J_ARR) {
      r->status = LAB_ON_FAILED;
      snprintf(r->msg, sizeof r->msg, "The list that came back could not be read.");
    } else {
      /* 25 a page; a 26th means there is another (de.d) */
      r->more = root->n >= 26;
      int n = root->n < 25 ? root->n : 25;
      r->packs = calloc((size_t)(n ? n : 1), sizeof *r->packs);
      for (int i = 0; r->packs && i < n; i++)
        if (lab_online_pack_from_json(lab_json_at(root, i), &r->packs[r->npacks]))
          r->npacks++;
    }
  } else if (j->kind == LAB_JOB_PUBLISH) {
    if (!lab_online_pack_from_json(root, &r->pack)) {
      r->status = LAB_ON_FAILED;
      snprintf(r->msg, sizeof r->msg, "Level pack not accepted by server. You might need to add more elements.");
    }
  }
  lab_json_free(root);
  free(body);
}

#ifdef __SWITCH__
static Thread g_thread;
static void worker(void *arg)
#else
static pthread_t g_thread;
static void *worker(void *arg)
#endif
{
  for (;;) {
    LOCK();
    while (!g_todo)
      WAIT();
    Job *j = pop(&g_todo, &g_todo_tail);
    g_busy = 1;
    UNLOCK();
    run(j);
    LOCK();
    push(&g_done, &g_done_tail, j);
    g_busy = 0;
    UNLOCK();
  }
#ifndef __SWITCH__
  return NULL;
#endif
}

static void submit(int kind, const char *path, const char *lid, LabOnDone cb, void *arg) {
  Job *j = calloc(1, sizeof *j);
  if (!j) {
    OLOG("out of memory for a request\n");
    return;
  }
  j->kind = kind;
  j->ipad = g_dev;
  j->r.ipad = g_dev;
  snprintf(j->path, sizeof j->path, "%s", path ? path : "");
  snprintf(j->r.lid, sizeof j->r.lid, "%s", lid ? lid : "");
  j->cb = cb;
  j->arg = arg;
  LOCK();
  if (!g_thread_up) {
#ifdef __SWITCH__
    /* priority 0x2C, any core: under the game's main thread (0x2B..) and
     * the mixer (0x28), a network wait blocks nothing */
    if (R_SUCCEEDED(threadCreate(&g_thread, worker, NULL, NULL, 0x20000, 0x2C, -2)) &&
        R_SUCCEEDED(threadStart(&g_thread)))
      g_thread_up = 1;
#else
    g_thread_up = pthread_create(&g_thread, NULL, worker, NULL) == 0;
#endif
    if (!g_thread_up) {
      UNLOCK();
      j->r.kind = kind;
      j->r.status = LAB_ON_FAILED;
      snprintf(j->r.msg, sizeof j->r.msg, "Couldn't start the network thread.");
      LOCK();
      push(&g_done, &g_done_tail, j);
      UNLOCK();
      return;
    }
  }
  push(&g_todo, &g_todo_tail, j);
  WAKE();
  UNLOCK();
}

#ifndef __SWITCH__
/* tools/test_online.c: the public lists without an account; tools/preview.c:
 * an account's look without registering */
void lab_online_test_skip_register(void);
void lab_online_test_skip_register(void) { g_registered[0] = g_registered[1] = 1; }
void lab_online_test_account(const char *uid, const char *pin);
void lab_online_test_account(const char *uid, const char *pin) {
  snprintf(g_uid[0], sizeof g_uid[0], "%s", uid);
  snprintf(g_pin[0], sizeof g_pin[0], "%s", pin);
  snprintf(g_show_uid, sizeof g_show_uid, "%s", uid);
  snprintf(g_show_pin, sizeof g_show_pin, "%s", pin);
  g_registered[0] = g_registered[1] = 1;
}
#endif

void lab_online_register(LabOnDone cb, void *arg) { submit(LAB_JOB_REGISTER, "", NULL, cb, arg); }

void lab_online_list(const char *type, const char *uid, int page, LabOnDone cb, void *arg) {
  char p[256];
  if (!strcmp(type, "own"))
    snprintf(p, sizeof p, "/list?lt=own&uid={uid}&lp=%d", page);
  else if (uid)
    snprintf(p, sizeof p, "/list?lt=%s&uid=%s&lp=%d", type, uid, page);
  else
    snprintf(p, sizeof p, "/list?lt=%s&lp=%d", type, page);
  submit(LAB_JOB_LIST, p, NULL, cb, arg);
}

void lab_online_get(const char *lid, LabOnDone cb, void *arg) {
  char p[128];
  snprintf(p, sizeof p, "/get?lid=%s&uid={uid}", lid);
  submit(LAB_JOB_GET, p, lid, cb, arg);
}

void lab_online_update(const char *lid, int playcount, int myrating, int mydifficulty) {
  char p[200];
  int n = snprintf(p, sizeof p, "/update?uid={uid}&lid=%s&nup=%d", lid, playcount);
  if (myrating != -1)
    n += snprintf(p + n, sizeof p - (size_t)n, "&rtg=%d", myrating);
  if (mydifficulty != -1)
    snprintf(p + n, sizeof p - (size_t)n, "&dif=%d", mydifficulty);
  submit(LAB_JOB_UPDATE, p, lid, NULL, NULL);
}

void lab_online_publish(const char *lid, int revision, const char *times, LabOnDone cb, void *arg) {
  char did[48], p[600];
  lab_online_did(lab_online_device_id(), did);
  snprintf(p, sizeof p, "/publish?lid=%s&did=%s&drv=%d&tms=%s", lid, did, revision, times);
  submit(LAB_JOB_PUBLISH, p, lid, cb, arg);
}

int lab_online_busy(void) {
  LOCK();
  int b = g_busy || g_todo != NULL;
  UNLOCK();
  return b;
}

void lab_online_poll(void) {
  for (;;) {
    LOCK();
    Job *j = pop(&g_done, &g_done_tail);
    int reg = g_registered[0];
    char uid[32], pin[32];
    snprintf(uid, sizeof uid, "%s", g_uid[0]);
    snprintf(pin, sizeof pin, "%s", g_pin[0]);
    UNLOCK();
    if (!j)
      return;
    /* the account, kept for showing offline (the registry is the main
     * thread's) */
    if (reg && uid[0]) {
      load_shown();
      snprintf(g_show_uid, sizeof g_show_uid, "%s", uid);
      snprintf(g_show_pin, sizeof g_show_pin, "%s", pin);
      const char *ki = lab_reg_get_string("port-online-id"), *kp = lab_reg_get_string("port-online-pin");
      if (!ki || strcmp(ki, uid) || !kp || strcmp(kp, pin)) {
        lab_reg_set_string("port-online-id", uid);
        lab_reg_set_string("port-online-pin", pin);
        lab_reg_save();
      }
    }
    if (j->kind == LAB_JOB_UPDATE)
      OLOG("rating sent: %s\n", j->r.status == LAB_ON_OK ? "OK" : j->r.msg);
    if (j->cb)
      j->cb(&j->r, j->arg);
    free(j->r.packs);
    free(j->r.data);
    free(j);
  }
}
