# SegaSonic The Hedgehog — Nintendo 3DS port

The game's own C (`src/`) and System 32 board model (`runtime/`) compiled for the 3DS, with a thin
frontend in `main.c` (libctru: top-screen framebuffer, ndsp audio, pad input). No game data is in the
repository: bring your own ROM, as with the other ports.

## Build

Needs [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `3ds-dev` group
(devkitARM, libctru).

```
cd ports/3ds
make            # -> segasonic.3dsx
```

## Run

1. Copy `segasonic.3dsx` to `sdmc:/3ds/`.
2. Put the game's data in `sdmc:/3ds/segasonic/`, either
   - `segasonic.assets`, made on a PC with the repository's own build (it ends up in `build/bin/`): the
     reliable way, or
   - MAME's `sonic.zip` (set `sonic`, Japan rev. C; a `rom/` folder with its files works too): the first run
     then makes the pack itself, which needs about 50 MB of free memory for a moment.
3. For sound, `sdmc:/3ds/dspfirm.cdc` must exist (dump it with DSP1).
4. Start it from the Homebrew Launcher. The first run builds `segasonic.assets` from the ROM set
   (a while), later runs load it. The game's settings are kept in `sonic.eeprom` beside it.

Memory: about 24 MB of the game's data stays resident (the sprite ROM alone is 16 MB), taken from the
heap at start (`SONIC_DYNAMIC_DATA`; static arrays that size crash the Luma loader), so a New 3DS /
New 2DS is the target. Speed: the video library draws every frame pixel by pixel in software; when a
frame comes late the next picture is skipped (the game logic still runs at 60). The bottom screen shows
how many frames a second are drawn.

## Controls

| 3DS | Arcade |
|---|---|
| Circle pad | trackball (speed) |
| D-pad | trackball (steady speed) |
| A / B / X / Y | button |
| SELECT | coin 1 |
| START | start 1 |
| L | service (moves the cursor in the operator's menu) |
| R | test (picks in the operator's menu) |
| START + SELECT | quit |
