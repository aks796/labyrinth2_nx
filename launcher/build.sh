#!/bin/sh
# Build labyrinth2_nx.nro (the launcher) with the runtime's launcher build
# (devkitPro's 64-bit toolchain container). Build the wrapper first
# (../build.sh): the NRO carries ../labyrinth2_nx.nsp and ../labyrinth2_nx.build.
#
# The iPad game's files the port uses go in too (romfs_extras.sh), from
#   L2_IPA=/path/to/Labyrinth_2_HD_1.6.0.ipa launcher/build.sh
# else the first .ipa in ../SD_CARD/switch/labyrinth2_nx/ or beside the project.
HERE="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER_DIR="$HERE" PAYLOAD=labyrinth2_nx exec "$HERE/../runtime/launcher/build.sh" "$@"
