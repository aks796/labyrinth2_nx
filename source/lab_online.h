/* lab_online.h -- the Labyrinth 2 level server (lab_online.c). MIT. */
#ifndef LAB_ONLINE_H
#define LAB_ONLINE_H
#include <stddef.h>
#include <stdint.h>

/* A level pack as the server lists it (LevelPack(JSONObject)). */
typedef struct {
  char lid[24];       /* "E9W6TU56.06": its author's id, then a number */
  char name[128];     /* lname */
  char author[96];    /* aname */
  char pdate[40];     /* when it was published */
  int rev, rqv, theme, nlevels, difficulty, published;
  double rating;      /* 0..5 */
} LabNetPack;

enum {
  LAB_ON_OK = 0,
  LAB_ON_OFFLINE,    /* the console is not on the internet (or online is off) */
  LAB_ON_FAILED,     /* no answer, or not one that could be read */
  LAB_ON_REFUSED,    /* the server said no (HTTP 4xx / 5xx) */
};

enum { LAB_JOB_REGISTER, LAB_JOB_LIST, LAB_JOB_GET, LAB_JOB_UPDATE, LAB_JOB_PUBLISH };

typedef struct {
  int kind, status, http;
  /* LAB_JOB_LIST */
  LabNetPack *packs;
  int npacks, more;   /* more: another page exists */
  int page;
  /* LAB_JOB_GET: the pack's zip */
  uint8_t *data;
  size_t len;
  char lid[24];
  /* LAB_JOB_PUBLISH: the pack as published */
  LabNetPack pack;
  char msg[200];      /* what went wrong, for the screen */
  int ipad;           /* it went to the iPad server */
} LabOnResult;

typedef void (*LabOnDone)(const LabOnResult *r, void *arg);

/* The id the server knows this console by (a phone's ANDROID_ID: 16 hex
 * digits), made the first time and kept (data/device_id). */
const char *lab_online_device_id(void);
/* The account: the server's user id and PIN for this console (the web
 * editor's login), "" until the first registration; kept in the registry. */
const char *lab_online_user_id(void);
const char *lab_online_pin(void);
int lab_online_enabled(void);   /* [online] enabled */

/* Requests, answered on the main thread by lab_online_poll (cb may be NULL).
 * Each registers the console first, the first time in a run (as the Java). */
void lab_online_register(LabOnDone cb, void *arg);
/* type: all_esy all_med all_hrd new hot rnd rtg dls mpl own ath (own: uid
 * = ours; ath: uid = the author searched for); page from 1. */
void lab_online_list(const char *type, const char *uid, int page, LabOnDone cb, void *arg);
void lab_online_get(const char *lid, LabOnDone cb, void *arg);
/* after playing someone's pack: the play count, your rating and difficulty */
void lab_online_update(const char *lid, int playcount, int myrating, int mydifficulty);
/* your pack: its revision and your times (ms, comma separated) */
void lab_online_publish(const char *lid, int revision, const char *times, LabOnDone cb, void *arg);
/* The requests made from now on go to the iPad server (Labyrinth 2 HD's
 * packs: the same API, its own account for this console) or back. */
void lab_online_set_ipad(int on);
int lab_online_ipad(void);
/* main thread, once a frame */
void lab_online_poll(void);
int lab_online_busy(void);

/* The signed URL of a request (for the tools too). */
void lab_online_url(char *out, size_t cap, const char *path_and_query);
/* did = b(android id) (df.java) */
void lab_online_did(const char *android_id, char out[48]);
/* a JSON object of the server's -> pack; 0 if it is not one */
struct LabJson;
int lab_online_pack_from_json(const struct LabJson *o, LabNetPack *out);

#endif
