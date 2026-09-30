/* lab_bionic.c -- the imports of liblabyrinthii.so the shared runtime had no
 * shim for: NDK r8's libstdc++ (operator new/delete, the static-local
 * guards, the pure-virtual trap) and zlib's get_crc_table.
 *
 * Android's libstdc++ (the "system" STL of NDK r8) is tiny: operator new is
 * malloc and never throws (a NULL comes back; the engine checks nothing, as
 * on a phone), and the guards follow the ARM C++ ABI: a 32-bit word whose
 * bit 0 says "constructed". One recursive lock serialises first-time
 * constructions, as bionic's does. MIT.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "util.h"

/* ------------------------------------------------------ new / delete */
void *b__Znwj(size_t n) { return malloc(n ? n : 1); }
void *b__Znaj(size_t n) { return malloc(n ? n : 1); }
void b__ZdlPv(void *p) { free(p); }
void b__ZdaPv(void *p) { free(p); }

/* ------------------------------------------------ static-local guards */
static RMutex g_guard_lock;

int b___cxa_guard_acquire(volatile int32_t *g) {
  if (__atomic_load_n(g, __ATOMIC_ACQUIRE) & 1)
    return 0;
  rmutexLock(&g_guard_lock);
  if (*g & 1) {
    rmutexUnlock(&g_guard_lock);
    return 0;
  }
  return 1; /* the caller constructs, then releases */
}

void b___cxa_guard_release(volatile int32_t *g) {
  __atomic_store_n(g, 1, __ATOMIC_RELEASE);
  rmutexUnlock(&g_guard_lock);
}

void b___cxa_guard_abort(volatile int32_t *g) { rmutexUnlock(&g_guard_lock); }

void b___cxa_pure_virtual(void) {
  debugPrintf("[bionic] pure virtual function called\n");
  log_flush_ring();
  abort();
}

/* ------------------------------------------------------------- zlib */
/* get_crc_table(): zlib's CRC-32 table (polynomial 0xedb88320). minizip
 * uses it with its own crypt code only, but it must be the real table. */
const uint32_t *b_get_crc_table(void) {
  static uint32_t t[256];
  static volatile int made;
  if (!made) {
    for (uint32_t n = 0; n < 256; n++) {
      uint32_t c = n;
      for (int k = 0; k < 8; k++)
        c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    made = 1;
  }
  return t;
}
