# Hollow Pines — PlayStation 2

C port of the Hollow Pines browser FPS for the PS2 (PS2SDK + gsKit). **No sound or music at all.**
The game is just: title menu (START / OPTIONS) → gameplay. Pause and game-over screens exist so you can't get stuck.

Removed from the web version: monster customiser, cheats/MLG mode, multiplayer, touch controls, all audio,
and the cosmetic round-arms toggle.

## Build (needs PS2DEV)
    docker run -it --rm -v "$PWD":/src ps2dev/ps2dev:latest sh -c "cd /src && make"
(if `make` is missing in the image: `apk add make` first). Output: `hollow_pines.elf`

Run it in PCSX2 (File > Run ELF, needs a PS2 BIOS), or on a console via uLaunchELF / OPL / wLaunchELF.
If the build stops with "implicit declaration of gsKit_prim_triangle_gouraud_3d", your gsKit is older:
`make EE_CFLAGS+=-DGSKIT_OLD_GORAUD`

## Controls (DualShock 2, analog mode)
| Input | Action |
|---|---|
| Left stick | Move |
| Right stick | Look |
| R2 | Fire (AK-47 is automatic, Glock is semi-auto) |
| L2 (hold) | Aim down sights |
| X | Jump |
| O | Reload |
| Square | Eat apple (look at one, within reach) |
| Triangle | Switch weapon |
| R1 | Punch an incoming arrow back at the archer |
| L1 or L3 | Sprint (forward only) |
| Start | Pause |
Menus: D-pad up/down, left/right to change values, X confirm, O back.

## Options screen
Difficulty (Easy–Nightmare), Night mode, Field of view, Look speed, Invert Y.

## Layout
- `src/game.c` – all gameplay (forest generation, weapons, monsters, arrows, eating, menus). Platform independent.
- `src/render.c` – CPU 3D transform/clip/fog + HUD/menus, emits screen-space gouraud triangles. Platform independent.
- `src/gfx_ps2.c` – gsKit backend (640x448 interlaced, 24-bit colour, 16-bit Z).
- `src/main_ps2.c` – IOP reset, pad setup, main loop.
- `host/` + `build_host_test.sh` – desktop test harness with a software rasteriser (used to produce `screenshots/`).
  `./build_host_test.sh && ./host_test` writes screenshots; `./host_test soak 5000` runs a random-input stress test.

## Honest status
The PS2-specific files (`gfx_ps2.c`, `main_ps2.c`, `Makefile`) could not be compiled or run where this was written
(no PS2 toolchain available). Everything else was compiled and run on a desktop with sanitizers. If the PS2 build
hits an API-name mismatch with your SDK version it will be in those three small files.
