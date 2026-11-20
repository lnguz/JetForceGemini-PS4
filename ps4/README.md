# Jet Force Gemini: Recompiled for PS4

A PS4 (OpenOrbis / GoldHEN) port of [Jet Force Gemini: Recompiled](https://github.com/VibinToast/JetForceGemini-Recompiled):
the N64 game statically recompiled with N64Recomp, running on the console with native video, audio and DualShock 4 input.

**Status: work in progress.** The game boots and runs on a real PS4 with its music. There is no picture yet: the
renderer (a native GNM renderer for the game's F3DJFG microcode and the N64 RDP) is the next stage. See
[PLAN.md](PLAN.md).

No ROM, game code or game assets are included. You need your own US ROM
(`Jet Force Gemini (USA).z64`, SHA-1 `493ced9008dbe932d6e91179b68e8630cf23a023`).

## Build (WSL / Linux)

Requirements: the [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) in
`~/ps4dev/OpenOrbis/PS4Toolchain`, clang, lld, cmake, ninja, python3, and a FreeType built for the PS4
(`ps4/Makefile`, `FREETYPE`).

```sh
git submodule update --init --recursive
cp /path/to/your/rom.z64 baserom.us.z64
bash ps4/gen.sh      # translates the game code from your ROM (RecompiledFuncs/, rsp/aspMain.cpp)
bash ps4/build.sh    # applies ps4/patches, compiles, links, builds build-ps4/JetForceGemini-PS4-<version>.pkg
```

## On the console

1. Install the pkg with GoldHEN.
2. Copy your ROM to `/data/jfg/baserom.us.z64`.
3. Settings, saves and `debug.log` are written to `/data/jfg`.

## Layout

| Path | What |
|---|---|
| `ps4/src/ps4_platform.c` | video out, audio out, DualShock 4, log |
| `ps4/src/sdl_ps4.cpp` | the part of SDL2 the port uses, on scePad (events, game controllers, gyro, rumble) |
| `ps4/src/main_ps4.cpp`, `gfx_ps4.cpp`, `audio_ps4.cpp` | replacements for the PC entry point, window and SDL audio |
| `ps4/src/renderer_ps4.cpp` | the renderer context (stage 1: status screen and hang watchdog) |
| `ps4/src/sem_ps4.c`, `crash_ps4.c` | semaphores, clock ids, thread stacks and crash reports that work on the console |
| `ps4/src/ui_stubs.cpp` | what the PC-only parts (RT64, RmlUi menus, ImGui backend, live recompiler) define |
| `ps4/compat/` | libc++ 11 and OpenOrbis compatibility headers, mutex-based queues |
| `ps4/patches/` | PS4 changes to N64ModernRuntime |

## Console pitfalls found on the way

OpenOrbis' headers and libc are Linux/musl flavoured, while the PS4 kernel is FreeBSD:

- `CLOCK_MONOTONIC` is 1 in the headers, which is FreeBSD's `CLOCK_VIRTUAL`: `std::chrono::steady_clock` stood still.
  `clock_gettime` is wrapped to map Linux clock ids to FreeBSD's.
- musl's `sem_*` (in `libc.a`) wait with the Linux futex syscall: they are wrapped onto libkernel's mutexes.
- Signal numbers are Linux's (`SIGBUS` 7, `SIGUSR1` 10); FreeBSD's are 10 and 30.
- `getcwd` reports nothing; anonymous `mmap` draws on a small budget (the 512 MB RDRAM uses direct memory in a
  reserved range); the default thread stack is too small for recompiled code.

## Credits

Jet Force Gemini: Recompiled by VibinToast and its contributors; N64Recomp and N64ModernRuntime by Wiseguy and
contributors; the Jet Force Gemini decompilation project. Licensed under the GPL-3.0 like the base project.
