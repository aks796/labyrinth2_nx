#!/bin/sh
# test_host.sh -- run tools/test_host.c on this computer against your APK:
#   tools/test_host.sh path/to/labyrinth2.apk
# Two passes over one scratch game folder: the first launch (setup, the level
# table), then a second start (what was saved comes back; a level pack from
# the SD card, and a copy of an official one, are imported).
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
APK="$1"
[ -f "$APK" ] || { echo "usage: $0 game.apk" >&2; exit 2; }
OUT="${TMPDIR:-/tmp}/lab_host_test"
rm -rf "$OUT" && mkdir -p "$OUT"
cc -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-sign-compare -Wno-unused-but-set-variable \
  -Wno-unknown-warning-option -Wno-misleading-indentation -Wno-unused-value \
  -I"$HERE/tools/host" -I"$HERE/source" -I"$HERE/runtime/source" -I"$HERE/portlibs32/include" \
  "$HERE/tools/test_host.c" "$HERE/source/lab_apk.c" "$HERE/source/lab_files.c" \
  "$HERE/source/lab_levels.c" "$HERE/source/lab_registry.c" "$HERE/source/lab_text.c" \
  "$HERE/tools/host/miniz_host.c" -lz -o "$OUT/test_host"
ROOT="$OUT/root"
"$OUT/test_host" "$APK" "$ROOT" 1
# an SD card pack (the server's format), and a copy of an official pack
mkdir -p "$ROOT/levelpacks"
python3 - "$ROOT/levelpacks" "$APK" <<'EOF'
import sys, zipfile, io
d = sys.argv[1]
with zipfile.ZipFile(d + "/my_pack.zip", "w") as z:
    z.writestr("info.xml", "<?xml version=\"1.0\"?>\n<levelpack>\n <levelid>UTEST.01</levelid>\n"
               " <levelname>Test Pack</levelname>\n <authorid>U1</authorid>\n <authorname>Tester</authorname>\n"
               " <nbrunits>2</nbrunits>\n <difficulty>1</difficulty>\n <theme>0</theme>\n"
               " <times>1000,2000,</times>\n</levelpack>\n")
    z.writestr("level0.xml", "<labyrinth/>")
    z.writestr("level1.xml", "<labyrinth/>")
# a zip of packs whose entry is not named after its level id
inner = io.BytesIO()
with zipfile.ZipFile(inner, "w") as z:
    z.writestr("info.xml", "<levelpack><levelid>UTEST.02</levelid><levelname>Second</levelname>"
               "<authorname>Tester</authorname><nbrunits>1</nbrunits><difficulty>2</difficulty>"
               "<theme>1</theme><times>500,</times></levelpack>")
    z.writestr("level0.xml", "<labyrinth/>")
with zipfile.ZipFile(d + "/collection.zip", "w") as z:
    z.writestr("some name.zip", inner.getvalue())
    z.writestr("../evil.zip", inner.getvalue())
with zipfile.ZipFile(sys.argv[2]) as apk:
    outer = zipfile.ZipFile(io.BytesIO(apk.read("assets/officiallevels/1.zip")))
    open(d + "/copy.zip", "wb").write(outer.read("Z0000000.05"))
EOF
# level packs as the level server sends them: names the engine's XML
# parser could not read as they are
mkdir -p "$ROOT/server"
python3 - "$ROOT/server" <<'EOF'
import sys, zipfile
d = sys.argv[1]
def pack(file, lid, name, author="Tester"):
    with zipfile.ZipFile(d + "/" + file, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("info.xml", '<?xml version="1.0" encoding="UTF-8"?>\n\n<levelpack>\n'
                   "  <levelid>%s</levelid>\n  <levelname>%s</levelname>\n  <authorid>ATEST001</authorid>\n"
                   "  <authorname>%s</authorname>\n  <nbrunits>2</nbrunits>\n  <difficulty>0</difficulty>\n"
                   "  <theme>0</theme>\n  <reqver>16809984</reqver>\n  <times>1000,2000,</times>\n"
                   "</levelpack>\n" % (lid, name, author))
        z.writestr("level0.xml", "<labyrinth>" + "x" * 3000 + "</labyrinth>")
        z.writestr("level1.xml", "<labyrinth/>")
pack("escape.zip", "ATEST001.01", "</T\\> Hold up! </T\\>", "Tom & Jerry")
pack("markup.zip", "ATEST001.02", "<b>x</b>")
pack("plain.zip", "ATEST001.03", "Plain &amp; simple")
open(d + "/notzip.zip", "w").write("<html>not a zip</html>")
# yours, not published: as the server sends it (stored, no info.xml)
with zipfile.ZipFile(d + "/unpublished.zip", "w", zipfile.ZIP_STORED) as z:
    z.writestr("level0.xml", '<labyrinth><ball x="50" y="50" id="ball1"/><goal x="267" y="427" id="goal1"/></labyrinth>')
    z.writestr("dirtybits.txt", "0")
EOF
"$OUT/test_host" "$APK" "$ROOT" 2
# what the engine will open: a zip any reader takes, an info.xml any XML
# parser reads back to the names as typed, the levels as they were
python3 - "$ROOT/data/files/zipfiles" <<'EOF'
import sys, zipfile, xml.etree.ElementTree as ET
d = sys.argv[1]
want = {"ATEST001.01": ("</T\\> Hold up! </T\\>", "Tom & Jerry"), "ATEST001.02": ("<b>x</b>", "Tester"),
        "ATEST002.01": ("Test <3 & more", "AK")}
bad = 0
for lid, (name, author) in want.items():
    z = zipfile.ZipFile(d + "/" + lid)
    if z.testzip() is not None: print(lid, "bad zip"); bad += 1
    root = ET.fromstring(z.read("info.xml"))
    got = (root.find("levelname").text, root.find("authorname").text)
    if got != (name, author): print(lid, "read back as", got); bad += 1
    if lid != "ATEST002.01" and z.read("level0.xml") != b"<labyrinth>" + b"x" * 3000 + b"</labyrinth>": print(lid, "level0 changed"); bad += 1
    if lid == "ATEST002.01":
        if root.find("nbrunits").text != "1" or root.find("times").text != "0,": print(lid, "info", ET.tostring(root)); bad += 1
        if sorted(z.namelist()) != ["dirtybits.txt", "info.xml", "level0.xml"]: print(lid, z.namelist()); bad += 1
print("rewritten packs: %s" % ("OK" if not bad else "%d FAILED" % bad))
sys.exit(1 if bad else 0)
EOF
