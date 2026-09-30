/* tools/host/switch.h -- the few libnx names the port's files, levels,
 * registry and text code use, on a PC (tools/test_host.sh). Single-threaded
 * tests: the locks do nothing. MIT. */
#ifndef LAB_HOST_SWITCH_H
#define LAB_HOST_SWITCH_H
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;
typedef u32 Result;
typedef u32 Handle;
#define R_FAILED(r) ((r) != 0)
#define R_SUCCEEDED(r) ((r) == 0)
#define BIT(n) (1u << (n))

typedef struct VirtmemReservation VirtmemReservation;
typedef struct { int x; } Mutex;
typedef struct { int x; } RMutex;
static inline void mutexLock(Mutex *m) { (void)m; }
static inline void mutexUnlock(Mutex *m) { (void)m; }
static inline void rmutexLock(RMutex *m) { (void)m; }
static inline void rmutexUnlock(RMutex *m) { (void)m; }

/* ticks are nanoseconds here */
static inline u64 armGetSystemTick(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}
static inline u64 armTicksToNs(u64 t) { return t; }
static inline u64 armNsToTicks(u64 t) { return t; }
static inline void svcSleepThread(s64 ns) {
  struct timespec ts = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
  nanosleep(&ts, NULL);
}

/* hid: the buttons (the values are libnx's) */
#define BITL(n) (1ull << (n))
enum {
  HidNpadButton_A = BITL(0), HidNpadButton_B = BITL(1), HidNpadButton_X = BITL(2), HidNpadButton_Y = BITL(3),
  HidNpadButton_StickL = BITL(4), HidNpadButton_StickR = BITL(5), HidNpadButton_L = BITL(6),
  HidNpadButton_R = BITL(7), HidNpadButton_ZL = BITL(8), HidNpadButton_ZR = BITL(9),
  HidNpadButton_Plus = BITL(10), HidNpadButton_Minus = BITL(11), HidNpadButton_Left = BITL(12),
  HidNpadButton_Up = BITL(13), HidNpadButton_Right = BITL(14), HidNpadButton_Down = BITL(15),
  HidNpadButton_StickLLeft = BITL(16), HidNpadButton_StickLUp = BITL(17), HidNpadButton_StickLRight = BITL(18),
  HidNpadButton_StickLDown = BITL(19),
};

/* pl: the shared fonts (tools/preview.c serves one from a file) */
typedef enum { PlServiceType_User = 0 } PlServiceType;
typedef enum {
  PlSharedFontType_Standard = 0, PlSharedFontType_ChineseSimplified = 1, PlSharedFontType_ExtChineseSimplified = 2,
  PlSharedFontType_ChineseTraditional = 3, PlSharedFontType_KO = 4, PlSharedFontType_NintendoExt = 5,
} PlSharedFontType;
typedef struct {
  u32 type, offset, size;
  void *address;
} PlFontData;
Result plInitialize(PlServiceType t);
Result plGetSharedFontByType(PlFontData *out, PlSharedFontType t);

#endif
