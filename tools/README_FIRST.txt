Labyrinth 2 for Nintendo Switch (32-bit wrapper)
================================================

This runs the Android game Labyrinth 2 by Illusion Labs on a Switch with
Atmosphere. It contains no game code and no game data: you supply the APK of
a copy you own.

1. Copy switch/labyrinth2_nx/labyrinth2_nx.nro to the same place on your SD
   card.
2. Copy your own APK of Labyrinth 2 1.29 (se.illusionlabs.labyrinth2) into
       sd:/switch/labyrinth2_nx/
   under any name ending in .apk.
3. In sphaira: Homebrew > Labyrinth 2 > Install Forwarder.
4. Launch the new Labyrinth 2 icon on the HOME menu. The first start
   installs the game program for that icon, restarts, and unpacks the game's
   library, files and level packs from the APK (once, with a progress bar).

Coming from a build before 2026-09-30 (sd:/switch/labyrinth2/Labyrinth2.nro):
put labyrinth2_nx.nro in sd:/switch/labyrinth2_nx/ and make a forwarder for
it (step 3). Its first start moves everything from sd:/switch/labyrinth2/
(the APK, the .ipa, fonts, config.ini, your saves in data/) to the new
folder; debug.log says so. Then delete sd:/switch/labyrinth2/Labyrinth2.nro
and the old Labyrinth 2 icon.

The NRO carries the iPad game's menus and level packs (from Labyrinth 2 HD
1.6.0): the menus are the iPad's, in landscape, with its iPad levels: the
official iPad packs and Labyrinth 2 HD's own level server (Download levels,
iPad levels tab; ZL / ZR switch iPad / iPhone levels). iPad levels are shown
4:3 in the middle of the screen. The first start copies those files to
data/ipad.ipa. [display] menus = android in config.ini keeps the phone's
menus. In the iPad's lists: A goes into the list, then up / down
move through it (held, faster and faster), resting on a pack shows it, A
plays it, right goes to its Play and its levels' pictures (left / right
choose, A plays), B leaves the list. The pictures are drawn once, while you
press nothing, and kept in data/thumbs/.

The game is portrait: it is shown upright in the middle of the screen (or
turned to fill it, for a console held upright: config.ini, [display] layout).

Controls
  Menus
    D-pad / left stick   move between the buttons      A  press    B  back
    L / R                Official / Downloaded; the tabs of Download levels
    ZR                   Download levels: the next list (easy / medium...)
    X                    a level pack's info
    Y                    Download levels > By ID: type an ID; Create: refresh
    +                    Create: how to make levels
    Right stick          scroll a list
  In a level
    Left stick           tilt the board (the D-pad tilts all the way)
    +  or  B             pause / back (the game's own pause screen)
    On the game's screens (pause, level end, rating): D-pad / left stick
    choose a button, A presses it; the right stick moves a pointer, A taps
    -                    motion tilt: the way you hold it now is level
    ZL                   motion (gyro) tilt on / off
    ZR                   turn the view: full screen or upright
  A level fills the screen, turned (the stick and motion turn with it); it
  stays turned when paused and at its end, the game's screens turned to read
  the right way up. The menus are upright. The pause screen's Settings open
  over the level; B goes back to the pause screen (the level is not
  restarted).
  Touchscreen: works as on a phone, always.

Multi player (a fourth bar on the main menu): two players on this console,
a board each, side by side, racing through a level pack. Choose a pack, then
Start (the console's controller screen comes up if player 2 has no
controller: a Joy-Con each held sideways, or two controllers). The first
ball in the goal wins the level; both go on to the next. Each player tilts
with their own left stick (and their own controller, with motion on); + or
- (either player) pauses. A lone Joy-Con held sideways: its stick turned
with it, its buttons A B X Y by where they sit, SL / SR as L / R. The
iPhone's Wi-Fi / Bluetooth play isn't possible: the Android game has no
network play. A race doesn't change your saves or best times. With the
iPad's .ipa, Multi player has the iPad levels too: two iPad boards.
Paused, Calibrate player 1 / 2 (or that player's ZL: a Joy-Con's stick
click) makes the way that player holds their controller level.

Motion controls: ZL in a level (or Settings > Tilt with) tilts the board with
the controller, or the console in handheld: hold it like the board and tilt
it. The position it is held in when a level starts is level; Calibrate (in
Settings) or - sets it again.

Online (the Labyrinth 2 level server is still up):
  Download levels   thousands of players' level packs: New, All levels,
                    Top 25, By ID. The right side of a row downloads a pack;
                    the row (or X) shows its levels first.
  Create            the level editor is a web page for a computer:
                        http://www.labyrinth2.com/editor.html
                    Log in there with the ID and pin code shown on Create
                    (its how-to, +, also has a QR code of the address and
                    can open the Switch's browser). Refresh (Y) brings your
                    packs to the console; play every level, then publish
                    from the pack's info.
  Your ID and pin code belong to this console: they come from
  sd:/switch/labyrinth2_nx/data/device_id, made the first time (each console,
  emuNAND with a blanked serial included, gets its own). Keep that file;
  copy it to keep your ID on another console. config.ini [online]:
  enabled = false keeps the game offline.

Emoji in level pack names (many were typed on iPhones) are drawn from an
emoji font you copy to sd:/switch/labyrinth2_nx/emoji.ttf (or any font file
with "emoji" in its name, in that folder) -- Apple Color Emoji
(sbix, e.g. from an iOS 6 device) or a CBDT font such as Noto Color Emoji or
an Android copy of the iPhone's emoji. None comes with the port; without one
the emoji are simply left out.

The text is drawn in Helvetica Neue, the iPhone game's font, if you copy it
to sd:/switch/labyrinth2_nx/ (any .ttc/.ttf with "helvetica" in its name, e.g.
HelveticaNeue.ttc from a Mac's /System/Library/Fonts). None comes with the
port; without it the console's own font is used. Fonts you put in
SD_CARD/switch/labyrinth2_nx/ are kept when the package is rebuilt, and are
never put in SD_CARD.zip.

Level packs as .zip files (from the server, or made in its editor) can also
go in sd:/switch/labyrinth2_nx/levelpacks/ -- they show up under Downloaded.

Settings: sd:/switch/labyrinth2_nx/config.ini (written at the first start).
Saves:    sd:/switch/labyrinth2_nx/data/ (registry.txt, levels.txt, device_id, files/)
Problems: sd:/switch/labyrinth2_nx/debug.log and crash.log
