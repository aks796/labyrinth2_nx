# romfs_extras.sh -- sourced by the runtime's launcher/build.sh (HERE, PORT,
# ROMFS set). The iPad game's files the port uses, into the romfs as
# ipad.ipa (../tools/make_ipad_assets.py; the game copies them out on its
# first start): from L2_IPA=<Labyrinth 2 HD .ipa>, else the first .ipa kept
# in ../SD_CARD/switch/labyrinth2_nx/ or beside the project. None: the NRO
# carries no iPad files (a player's own .ipa in the game folder still works).
IPA="${L2_IPA:-}"
if [ -z "$IPA" ]; then
  for f in "$PORT"/SD_CARD/switch/labyrinth2_nx/*.ipa "$PORT"/../*.ipa; do
    [ -f "$f" ] && { IPA="$f"; break; }
  done
fi
if [ -n "$IPA" ]; then
  python3 "$PORT/tools/make_ipad_assets.py" "$IPA" "$ROMFS/ipad.ipa.new" || exit 1
  if cmp -s "$ROMFS/ipad.ipa.new" "$ROMFS/ipad.ipa"; then
    rm -f "$ROMFS/ipad.ipa.new"
  else
    mv "$ROMFS/ipad.ipa.new" "$ROMFS/ipad.ipa"
    rm -f "$HERE/labyrinth2_nx.nro" # repacked with it
  fi
elif [ -f "$ROMFS/ipad.ipa" ]; then
  rm -f "$ROMFS/ipad.ipa" "$HERE/labyrinth2_nx.nro"
  echo "no Labyrinth 2 HD .ipa (set L2_IPA): the NRO carries no iPad files"
fi
