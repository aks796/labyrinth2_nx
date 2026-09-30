/* tools/test_online.c -- the port's level-server code (lab_online.c,
 * lab_net.c, lab_json.c) on a PC, against the real server:
 *   - the did derivation (df.b) and the request checksum, against Python
 *     copies of the Java (tools/test_online.sh passes their answers);
 *   - a public list (no account needed): signed, fetched, parsed.
 * With --register it also registers a device and downloads one pack; that
 * creates an account on the server, so it is left to you to ask for it. MIT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lab_net.h"
#include "lab_online.h"

static char g_root[512];
const char *dcr_game_root(void) { return g_root; }
static char g_id[64], g_pin[64];
const char *lab_reg_get_string(const char *k) {
  return !strcmp(k, "port-online-id") ? (g_id[0] ? g_id : NULL) : (g_pin[0] ? g_pin : NULL);
}
void lab_reg_set_string(const char *k, const char *v) {
  snprintf(!strcmp(k, "port-online-id") ? g_id : g_pin, 64, "%s", v);
}
void lab_reg_save(void) {}

static int g_done, g_fail;
static void on_list(const LabOnResult *r, void *arg) {
  printf("list %s: status %d http %d, %d packs, more %d%s%s\n", (const char *)arg, r->status, r->http,
         r->npacks, r->more, r->msg[0] ? ": " : "", r->msg);
  for (int i = 0; i < r->npacks && i < 5; i++)
    printf("  %-12s %-32.32s by %-16.16s dif %d, %2d levels, rating %.2f, theme %d\n", r->packs[i].lid,
           r->packs[i].name, r->packs[i].author, r->packs[i].difficulty, r->packs[i].nlevels, r->packs[i].rating,
           r->packs[i].theme);
  if (r->status != LAB_ON_OK || r->npacks < 1)
    g_fail++;
  g_done++;
}
static void on_get(const LabOnResult *r, void *arg) {
  printf("get: status %d http %d, %zu bytes%s%s\n", r->status, r->http, r->len, r->msg[0] ? ": " : "", r->msg);
  if (r->status != LAB_ON_OK)
    g_fail++;
  g_done++;
}
static void on_reg(const LabOnResult *r, void *arg) {
  printf("register: status %d, id %s\n", r->status, lab_online_user_id());
  if (r->status != LAB_ON_OK)
    g_fail++;
  g_done++;
}

static void wait_for(int n) {
  for (int i = 0; i < 400 && g_done < n; i++) {
    lab_online_poll();
    usleep(50000);
  }
}

void lab_sha256(const void *data, size_t n, uint8_t out[32]);
static void sha_hex(const char *s, char *out) {
  uint8_t d[32];
  lab_sha256(s, strlen(s), d);
  for (int i = 0; i < 32; i++)
    sprintf(out + i * 2, "%02x", d[i]);
}

int main(int argc, char **argv) {
  snprintf(g_root, sizeof g_root, "%s", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
  if (argc > 2 && !strcmp(argv[1], "sha256")) {
    char h[65];
    sha_hex(argv[2], h);
    printf("%s\n", h);
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "did")) { /* did <android id> */
    char did[48];
    lab_online_did(argv[2], did);
    printf("%s\n", did);
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "url")) { /* url <path?query> */
    char u[800];
    lab_online_url(u, sizeof u, argv[2]);
    printf("%s\n", u);
    return 0;
  }
  int reg = argc > 1 && !strcmp(argv[1], "--register");
  if (!reg) {
    /* the lists only: a request without registering first (the worker
     * would register; skip it by saying we are) */
    extern void lab_online_test_skip_register(void);
    lab_online_test_skip_register();
  }
  lab_online_list("new", NULL, 1, on_list, "new");
  lab_online_list("rtg", NULL, 1, on_list, "rtg");
  lab_online_list("all_hrd", NULL, 2, on_list, "all_hrd p2");
  wait_for(3);
  /* Labyrinth 2 HD's server: the iPad packs */
  lab_online_set_ipad(1);
  lab_online_list("new", NULL, 1, on_list, "iPad new");
  lab_online_list("rtg", NULL, 1, on_list, "iPad rtg");
  wait_for(5);
  lab_online_set_ipad(0);
  if (reg) {
    lab_online_register(on_reg, NULL);
    wait_for(4);
    lab_online_get("CHAX3VKF.01", on_get, NULL);
    wait_for(5);
  }
  printf(g_fail ? "%d FAILED\n" : "all OK\n", g_fail);
  return g_fail != 0;
}
