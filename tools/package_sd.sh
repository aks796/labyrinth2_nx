#!/bin/sh
# package_sd.sh -- assemble SD_CARD/ (and SD_CARD.zip) for a test on a Switch.
#
#   tools/package_sd.sh [path/to/your/labyrinth2.apk]
#
# Builds the wrapper and the launcher, then lays out what goes on the card:
#   SD_CARD/switch/labyrinth2_nx/labyrinth2_nx.nro
#   SD_CARD/switch/labyrinth2_nx/<the APK>   (only if an APK path is given: your own copy)
#   SD_CARD/switch/labyrinth2_nx/levelpacks/README.txt  (where extra level packs go)
#   (no emoji font is made: one the player put in SD_CARD/switch/labyrinth2_nx
#   themselves, *.ttf/.ttc/.otf, and their iPad game's .ipa, are kept across rebuilds but left out of
#   SD_CARD.zip, so the zip stays theirs to share)
#   SD_CARD/README_FIRST.txt
# The ExeFS override is not included: the launcher writes it for whichever
# sphaira forwarder it is started from.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
./build.sh
launcher/build.sh
if [ -n "$1" ]; then
  tools/test_host.sh "$1" | tail -1
fi
KEEP="$(mktemp -d)"
# (SD_CARD/switch/labyrinth2/: the folder before 2026-09-30)
for d in SD_CARD/switch/labyrinth2_nx SD_CARD/switch/labyrinth2; do
  for f in "$d"/*.ttf "$d"/*.ttc "$d"/*.otf "$d"/*.ipa; do
    [ -f "$f" ] && mv "$f" "$KEEP/"
  done
done
rm -rf SD_CARD SD_CARD.zip
mkdir -p SD_CARD/switch/labyrinth2_nx/levelpacks
for f in "$KEEP"/*; do
  [ -f "$f" ] && mv "$f" SD_CARD/switch/labyrinth2_nx/ && echo "kept your file: SD_CARD/switch/labyrinth2_nx/${f##*/} (not in SD_CARD.zip)"
done
rmdir "$KEEP"
cp launcher/labyrinth2_nx.nro SD_CARD/switch/labyrinth2_nx/
cp tools/README_FIRST.txt SD_CARD/
cat > SD_CARD/switch/labyrinth2_nx/levelpacks/README.txt <<'EOF'
Labyrinth 2 level packs (.zip files as the Labyrinth 2 server sent them: an
info.xml and level0.xml, level1.xml...) put here are added when the game
starts, or with Download levels > By ID > Look for new packs. They are
listed under Downloaded in Play game. (Download levels gets them from the
Labyrinth 2 server itself.)
EOF
if [ -n "$1" ]; then
  cp -p "$1" SD_CARD/switch/labyrinth2_nx/
fi
(cd SD_CARD && zip -qr ../SD_CARD.zip . -x '*.ttf' '*.ttc' '*.otf' '*.ipa' '*.apk' '*.DS_Store')
echo "build $(cat labyrinth2_nx.build): SD_CARD/ and SD_CARD.zip ready"
