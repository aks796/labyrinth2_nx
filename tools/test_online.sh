#!/bin/sh
# test_online.sh -- the port's level-server code on this computer:
#   tools/test_online.sh              # signing + public lists (no account)
#   tools/test_online.sh --register   # also registers a device and downloads a
#                                     # pack: that makes an account on the server
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/lab_online_test"
mkdir -p "$OUT/data"
cc -std=gnu11 -O1 -g -Wall -Wno-unused-function -I"$HERE/source" "$HERE/tools/test_online.c" \
  "$HERE/source/lab_online.c" "$HERE/source/lab_net.c" "$HERE/source/lab_json.c" -lpthread -o "$OUT/test_online"
# the Java's df.b and df.a(byte[]), in Python, against the C
python3 - "$OUT/test_online" <<'PY'
import subprocess, sys, random
exe = sys.argv[1]
def java_b(s):
    while len(s) < 16: s = "0" + s
    n = len(s); sb = s
    i = int(s[0], 16)
    for _ in range(12): sb += s[i % n]; i += 1
    i3 = int(s[1], 16)
    while len(sb) < 40: sb += s[15 - (i3 % n)]; i3 += 1
    return sb
def java_sum(q):
    j = j2 = 0
    for ch in q.encode():
        j3 = (j << 9) & 0xFFFFFFFF
        j = (j3 | (j3 >> 23)) ^ ch
        j2 += j
    return format(j2 ^ 44665651, 'X').rjust(8, '0')
bad = 0
for aid in ["9774d56d682e549c", "0123456789abcdef", "fedcba9876543210", "abc", "7f3a"]:
    c = subprocess.check_output([exe, "did", aid]).decode().strip()
    if c != java_b(aid): print("did mismatch", aid, c, java_b(aid)); bad += 1
for q in ["lt=new&lp=1", "did=" + java_b("9774d56d682e549c"), "lid=CHAX3VKF.01&uid=ABCDEFGH"]:
    u = subprocess.check_output([exe, "url", "/x?" + q]).decode().strip()
    got = u.split("&c=")[1][8:]
    if got != java_sum(q): print("checksum mismatch", q, got, java_sum(q)); bad += 1
import hashlib
for s in ["", "abc", "labyrinth2_nx:XAW10012345678", "labyrinth2_nx:user:" + "0123456789abcdef" * 2, "x" * 55, "y" * 56, "z" * 64, "q" * 119, "w" * 200]:
    c = subprocess.check_output([exe, "sha256", s] if s else [exe, "sha256", ""]).decode().strip()
    if c != hashlib.sha256(s.encode()).hexdigest(): print("sha256 mismatch", repr(s[:20]), c); bad += 1
print("did + checksum + sha256: %s" % ("OK" if not bad else "%d FAILED" % bad))
sys.exit(1 if bad else 0)
PY
TMPDIR="$OUT" "$OUT/test_online" "$@"
