#!/usr/bin/env python3
"""make_ipad_assets.py -- the iPad game's files the port uses, as a small .ipa.

    tools/make_ipad_assets.py <Labyrinth 2 HD .ipa> <out.ipa> [source dir]

launcher/build.sh puts the result in the NRO's romfs as ipad.ipa; the game
copies it to data/ipad.ipa on its first start (source/dcr_setup.c). It keeps
the .ipa's own paths (Payload/<app>.app/...), so source/lab_apk.c reads it as
it reads a whole one. What goes in:

  - every picture the source names (string literals under menugraphics/,
    menugraphics-ipad/, textures-ipad/, textures-ipad-full/, and the splash
    and cover flow pictures), with the suffixes and families the menus build
    at run time (_selected, _pressed, the balls, the award icons...); of each,
    only the one file lab_draw.c's loader opens (@2x first, then .png, .jpg,
    ~ipad, .pvr);
  - the iPad level packs (officiallevelsipad/) and each theme's floor and
    walls (textures-ipad/<theme>/), which source/lab_files.c unpacks;
  - Info.plist (lab_apk.c checks it is an app).
"""
import os
import re
import sys
import zipfile

LITERAL = re.compile(r'"((?:menugraphics|menugraphics-ipad|textures-ipad|textures-ipad-full)/[A-Za-z0-9_\-/]*[A-Za-z0-9_\-]'
                     r'|Default-Landscape|CoverFlow_Empty)"')
SUFFIXES = ('_selected', '_pressed', '_down', '_up', '_highlight', '_unachived')
FAMILIES = [re.compile(p) for p in (
    r'menugraphics/award-icon-[A-Za-z0-9\-]+',       # the awards (lab_hd.c: the phone's names, dashed)
    r'menugraphics/ball_[a-z]+(_highlight|_unachived)?',
    r'menugraphics/icon_download_[a-z]+',
    r'menugraphics-ipad/icon_[a-z_]+',               # the packs' big icons (lab_hd_packs.c)
)]
THEMES = ('theme-classic', 'theme-metal', 'theme-plastic')
PICTURE = re.compile(r'^(.*?)(@2x)?(~ipad)?\.(png|jpg|pvr)$')


def loader_order(name):
    """The files lab_draw.c's ipa_picture() tries for a name, in order."""
    two = [f'{name}@2x.png', f'{name}@2x.jpg', f'{name}@2x~ipad.png', f'{name}@2x~ipad.jpg', f'{name}@2x.pvr']
    one = [f'{name}.png', f'{name}.jpg', f'{name}~ipad.png', f'{name}~ipad.jpg', f'{name}.pvr']
    return two + one


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ipa, out = sys.argv[1], sys.argv[2]
    src = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'source')

    z = zipfile.ZipFile(ipa)
    app, files = None, {}
    for info in z.infolist():
        m = re.match(r'^Payload/[^/]+\.app/', info.filename)
        if m and not info.filename.endswith('/'):
            app = m.group(0)
            files[info.filename[len(app):]] = info
    if 'Info.plist' not in files:
        sys.exit(f'{ipa}: not an iPhone / iPad app')

    # the picture names
    text = ''
    for n in sorted(os.listdir(src)):
        if n.endswith('.c'):
            with open(os.path.join(src, n), errors='replace') as f:
                text += f.read()
    wanted = set(LITERAL.findall(text))
    wanted |= {w + s for w in list(wanted) for s in SUFFIXES}
    bases = set()
    for r in files:
        m = PICTURE.match(r)
        if m:
            bases.add(m.group(1))
    wanted |= {b for b in bases if any(f.fullmatch(b) for f in FAMILIES)}

    keep = ['Info.plist']
    for name in sorted(wanted):
        for cand in loader_order(name):
            if cand in files:
                keep.append(cand)
                break
    # read by name, not as pictures
    keep += sorted(r for r in files if r.startswith('officiallevelsipad/'))
    for t in THEMES:
        keep += [r for r in (f'textures-ipad/{t}/floor-1024x1024.jpg', f'textures-ipad/{t}/wall-1024x1024.pvr')
                 if r in files]

    keep = list(dict.fromkeys(keep))
    tmp = out + '.part'
    total = 0
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as o:
        for r in keep:
            info = files[r]
            data = z.read(info)
            total += len(data)
            zi = zipfile.ZipInfo(app + r, date_time=info.date_time)
            zi.compress_type = zipfile.ZIP_DEFLATED
            o.writestr(zi, data)
    os.replace(tmp, out)
    print(f'{out}: {len(keep)} of the .ipa\'s {len(files)} files, {total / 1e6:.1f} MB '
          f'({os.path.getsize(out) / 1e6:.1f} MB packed)')


if __name__ == '__main__':
    main()
