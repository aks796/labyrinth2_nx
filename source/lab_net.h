/* lab_net.h -- the network, for the level server: the console's sockets and
 * a small HTTP/1.1 client (lab_net.c), a JSON reader (lab_json.c). Builds on
 * a PC too (POSIX sockets), for testing the requests. MIT. */
#ifndef LAB_NET_H
#define LAB_NET_H
#include <stddef.h>
#include <stdint.h>

/* --------------------------------------------------------------- lab_net.c */
/* Starts the sockets the first time; 1 when the console is on the internet. */
int lab_net_online(void);

typedef struct LabHttp LabHttp;
/* A request (GET or POST), redirects followed. extra_headers: "Name: value\r\n"
 * lines, or NULL. Returns NULL on a connection failure; *status is the HTTP
 * status (or 0). */
LabHttp *lab_http_open(const char *method, const char *url, const char *extra_headers, const void *body,
                       size_t body_len, int *status);
int64_t lab_http_length(const LabHttp *h); /* Content-Length, -1 if not known */
/* The body: bytes read (0 at its end, < 0 on an error). */
int lab_http_read(LabHttp *h, void *buf, int n);
void lab_http_close(LabHttp *h);
/* Breaks off every request in progress (their reads fail at once). */
void lab_net_abort_all(void);
/* The whole body of a request into a malloc()ed, 0-terminated buffer. */
char *lab_http_fetch(const char *method, const char *url, const char *extra_headers, const char *body,
                     size_t *len, int *status);

/* -------------------------------------------------------------- lab_json.c */
enum { LAB_J_NULL, LAB_J_BOOL, LAB_J_NUM, LAB_J_STR, LAB_J_ARR, LAB_J_OBJ };
typedef struct LabJson {
  int t;
  double num;
  char *s;
  int n, cap;
  struct LabJson **items;
  char **keys; /* objects */
} LabJson;
LabJson *lab_json_parse(const char *text, size_t len);
void lab_json_free(LabJson *v);
LabJson *lab_json_get(const LabJson *v, const char *key);
LabJson *lab_json_at(const LabJson *v, int i);
const char *lab_json_str(const LabJson *v);
double lab_json_num(const LabJson *v, double def); /* numbers, and numbers in strings */

#endif
