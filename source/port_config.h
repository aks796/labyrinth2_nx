/* port_config.h -- Labyrinth 2's settings for the android32 runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). What each setting does is next to its
 * default in the runtime; runtime/docs/ lists them all. MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE    "Labyrinth 2"
#define PORT_NAME     "labyrinth2_nx"
#define PORT_PACKAGE  "se.illusionlabs.labyrinth2"
#define PORT_BANNER   "labyrinth2_nx: Labyrinth 2 (Illusion Labs engine, armeabi)"
/* builds before 2026-09-30 kept everything in /switch/labyrinth2 */
#define PORT_OLD_ROOT_PATHS "/switch/labyrinth2"
#define PORT_ABI_DIR  "lib/armeabi/"
/* liblabyrinthii.so 1.29: highest p_vaddr + p_memsz = 0x4f448 (~317 KB); up
 * to four copies are loaded, each in its own reservation (lab_loader.c) */
#define PORT_SO_REGION_BYTES (4u * 1024 * 1024)

/* The APK: any *.apk that is Labyrinth 2's (by its manifest), game.apk first,
 * else the newest -- the runtime's default role. */
#define PORT_APK_DESC "Labyrinth 2 1.29 (se.illusionlabs.labyrinth2)"
#define PORT_LAUNCHER_START_NOTE "(the first start unpacks the game's library and levels from the APK)"

/* ------------------------------------------------------------------ libc */
#define RT_PROC_COMM "se.illusionlabs" /* /proc/self/stat's comm, as before */
/* The engine's frame clock is gettimeofday (+0x1b5ac / +0x1b5f6, read by
 * SurfaceView.render): REALTIME leaves out the time the process was frozen
 * (HOME, sleep); time() stays on the wall clock (a date in the registry).
 * 1 = RT_TIME_SHIFT_REALTIME (dcr_time.h). */
#define RT_TIME_SHIFT 1
/* above the main thread and the sound decoder (0x2C, core 0) */
#define RT_TIME_WATCH_PRIO 0x2B

/* ------------------------------------------------------------------ input */
#define RT_PAD_MAX_PLAYERS 2 /* local play: a board each */

/* ------------------------------------------------------------------ files */
/* The old folder's move, as this port's own did it: the top level only (a
 * folder the new one has already stays where it is), nothing recorded in a
 * file (the log says what moved), an APK moved whatever the new folder has. */
#define RT_MIGRATE_MERGE         0
#define RT_MIGRATE_RECORD        ""
#define RT_MIGRATE_SAME_SIZE_APK 0

#endif
