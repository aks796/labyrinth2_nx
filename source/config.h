/* config.h -- the Labyrinth 2 wrapper's own constants.
 *
 * Labyrinth 2 (se.illusionlabs.labyrinth2 1.29, versionCode 20), armeabi:
 * Illusion Labs' own C++ engine (GLES 1.x), one native library; the menus
 * were Android Views, rebuilt natively here (lab_ui.c). The settings the
 * android32 runtime reads (the folder, the package, the module region...)
 * are in port_config.h. MIT.
 */
#ifndef DCR_CONFIG_H
#define DCR_CONFIG_H

#include "rt_settings.h"

#define LAB_LIB_GAME     "liblabyrinthii.so"
#define LAB_ABI_DIR      PORT_ABI_DIR
#define LAB_VERSION      "1.29"
#define LAB_VERSION_CODE 20

#endif /* DCR_CONFIG_H */
