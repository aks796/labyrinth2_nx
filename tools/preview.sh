#!/bin/sh
# preview.sh -- the rebuilt screens as PNGs (tools/preview.c), on a Mac:
#   tools/preview.sh path/to/labyrinth2.apk [outdir]
# The Download screens show the level server's real lists (public: no
# account is made); Create and the how-to show a made-up account.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
APK="$1"
[ -f "$APK" ] || { echo "usage: $0 game.apk [outdir]" >&2; exit 2; }
OUT="${2:-$HERE/preview}"
WORK="${TMPDIR:-/tmp}/lab_preview"
rm -rf "$WORK" && mkdir -p "$WORK"
cc -std=gnu11 -O1 -g -w -DGL_SILENCE_DEPRECATION \
  -I"$HERE/tools/host" -I"$HERE/source" -I"$HERE/portlibs32/include" \
  "$HERE/tools/preview.c" "$HERE/source/lab_ui.c" "$HERE/source/lab_screens.c" "$HERE/source/lab_draw.c" \
  "$HERE/source/lab_font.c" "$HERE/source/lab_text.c" "$HERE/source/lab_levels.c" \
  "$HERE/source/lab_registry.c" "$HERE/source/lab_apk.c" "$HERE/source/lab_files.c" \
  "$HERE/source/lab_screens_online.c" "$HERE/source/lab_online.c" "$HERE/source/lab_net.c" \
  "$HERE/source/lab_json.c" "$HERE/source/lab_qr.c" "$HERE/source/lab_emoji.c" "$HERE/source/lab_versus.c" \
  "$HERE/source/lab_hd.c" "$HERE/source/lab_hd_packs.c" "$HERE/source/lab_hd_online.c" \
  "$HERE/tools/host/miniz_host.c" "$HERE/tools/host/host_gl.c" \
  -framework OpenGL -lz -lpthread -o "$WORK/preview"
# IPA=path/to/Labyrinth2HD.ipa: the iPad menus (hd*.png)
# EMOJI_FONT=path/to/a-colour-emoji-font: as the SD card's emoji.ttf/.ttc
mkdir -p "$WORK/root"
[ -n "$EMOJI_FONT" ] && ln -sf "$EMOJI_FONT" "$WORK/root/emoji.${EMOJI_FONT##*.}"
# UI_FONT=path/to/HelveticaNeue.ttc: as the SD card's Helvetica Neue
[ -n "$UI_FONT" ] && ln -sf "$UI_FONT" "$WORK/root/HelveticaNeue.${UI_FONT##*.}"
"$WORK/preview" "$APK" "$WORK/root" "$OUT" | grep -v "^\[setup\]\|^\[files\]\|^\[registry\]\|^\[apk\]"
