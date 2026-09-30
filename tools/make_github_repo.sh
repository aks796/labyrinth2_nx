#!/bin/sh
# make_github_repo.sh -- lay out github_repo/: what goes on GitHub.
#
#   tools/make_github_repo.sh
#
# Copies the source, the build files, the launcher's source and icon, the
# tools, README.md, NOTES.md and LICENSE. Leaves out everything else: build
# outputs (build/, *.nsp, *.nro, *.elf, SD_CARD*), the prebuilt 32-bit Mesa
# in portlibs32/ (from mesa32), debug logs, backups and anything from the
# game (no APK or .ipa content is ever in the project's source).
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HERE/github_repo"
cd "$HERE"
rm -rf "$OUT"
mkdir -p "$OUT/launcher" "$OUT/portlibs32"

cp README.md NOTES.md LICENSE icon.png Makefile build.sh dcr32.ld dcr32.specs labyrinth2_nx.json "$OUT/"
cp -R source tools "$OUT/"
cp launcher/Makefile launcher/build.sh launcher/icon.jpg "$OUT/launcher/"
cp -R launcher/source "$OUT/launcher/"

cat > "$OUT/portlibs32/README.md" <<'EOF'
# portlibs32

The 32-bit (AArch32) Mesa, libGLESv1_CM and libdrm_nouveau the wrapper links
against: copy `include/` and `lib/` from
[mesa32](https://github.com/aks796/mesa32) (its `prefix/` after `./build.sh`,
or its release tarball) here. Without them
the Makefile builds the null renderer (the game runs, nothing is drawn).
EOF

cat > "$OUT/.gitignore" <<'EOF'
build/
launcher/build/
launcher/romfs/
*.nsp
*.nro
*.nacp
*.elf
*.build
SD_CARD/
SD_CARD.zip
portlibs32/include/
portlibs32/lib/
.DS_Store
EOF

find "$OUT" -name .DS_Store -delete
echo "github_repo/: $(find "$OUT" -type f | wc -l | tr -d ' ') files"
