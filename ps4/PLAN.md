# Jet Force Gemini Recompiled → PS4

Base: VibinToast/JetForceGemini-Recompiled 1.3.2 (N64Recomp + N64ModernRuntime; RT64 on PC).
Objetivo: 1080p nativo (y más con supersampling) a 60 fps, widescreen, DS4 completo, menús del port.

## Decisiones

- **Código del juego**: el de N64Recomp tal cual (x86-64 → x86-64, compila limpio con OpenOrbis).
- **Renderizador**: propio en GNM (`ps4/src/gfx_*`), no RT64 (necesita Vulkan/D3D12) ni GLideN64 (GLES vía Piglet es lento y
  limitado). El RSP gráfico se interpreta en la CPU (F3DJFG = F3DDKR revisado por Rare; referencia: `lib/rt64/src/gbi/
  rt64_gbi_f3ddkr.cpp`, `rt64_gbi_rdp.cpp`, `hle/rt64_rdp_tmem.cpp`) y la GPU dibuja con un uber-shader del combiner/blender
  del RDP. Resolución interna independiente de la del N64 → 1080p o 4K-downsample.
- **Frontera**: `ultramodern::renderer::RendererContext` (`send_dl`, `update_screen`). `WindowHandle` en PS4 = handle de
  sceVideoOut.
- **C++**: libc++ 11 de OpenOrbis. `ps4/compat/ps4_compat.hpp` (forzado) + `-fno-char8_t` (u8string = string).
- **SDL2 2.0.9** de OpenOrbis: sirve para audio; la entrada va nativa con scePad (input_ps4.cpp conserva los ids de SDL que
  usa el sistema de mapeo del port).
- **Menús**: RmlUi compila para PS4 (FreeType de cse2). Falta su RenderInterface en GNM (reemplazo de `ui_renderer.cpp`).
- **Mods**: LiveRecomp (JIT) desactivado en la primera versión.

## Estado

1. [x] Generar código desde la ROM en WSL (`ps4/gen.sh`): 57 .c + rsp/aspMain.cpp.
2. [x] Compilar para PS4: juego, runtime, Lua, ImGui, RmlUi, lunasvg y casi todo `src/` (`ps4/Makefile`).
3. [x] Capa PS4 (`ps4/src`): `ps4_platform.c` (video, audio, DS4, log), `sdl_ps4.cpp` (mini-SDL 2.30 sobre scePad:
   eventos, GameController, giroscopio, vibración), `audio_ps4.cpp`, `gfx_ps4.cpp`, `main_ps4.cpp`, `renderer_ps4.cpp`
   (etapa 1: sin dibujo), `ui_stubs.cpp`. Headers de SDL 2.30 en `ps4/third_party/SDL2`.
4. [x] Enlaza → `eboot.bin` → `JetForceGemini-PS4-01.00.pkg` (`make -f ps4/Makefile eboot`, `ps4/make_pkg.sh`).
   Mods de código desactivados (parche `#ifdef __PS4__` en librecomp/mods.cpp). Sin probar en consola todavía.
4b. [x] 01.08 en consola: el juego corre con música, 30 DL/s, VI 60 Hz. Arreglos de plataforma: carpeta /data/jfg,
   RDRAM en memoria directa, páginas KSEG1 con memoria directa, `sem_*` propios (musl usa futex), `clock_gettime`
   traducido (los ids de OpenOrbis son de Linux: CLOCK_MONOTONIC=1 es CLOCK_VIRTUAL en la PS4), pilas de 2 MB,
   reporte de crash con `sceKernelInstallExceptionHandler`.
5. [ ] Renderizador: intérprete F3DJFG + RDP + TMEM en CPU → lista de triángulos; dibujo GNM con uber-shader.
6. [ ] Framebuffer effects (copias de FB, mira del francotirador, pausa), widescreen, 60 fps (interpolación del port).
7. [ ] UI RmlUi sobre GNM, audio, guardado en /data/jfg, pkg.

## Compilar

```sh
# WSL, espejo en ~/jfg (rsync desde Desktop\Work\JFG-ps4)
bash ps4/gen.sh                     # una vez por ROM/configuración
make -f ps4/Makefile -j12
```
