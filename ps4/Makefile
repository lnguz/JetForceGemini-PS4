# Jet Force Gemini Recompiled - PS4 build (OpenOrbis). Run from the repo root in WSL:
#   make -f ps4/Makefile -j12
# Needs RecompiledFuncs/ and rsp/aspMain.cpp generated from your ROM first (ps4/gen.sh).
OO       ?= $(HOME)/ps4dev/OpenOrbis/PS4Toolchain
BUILD    ?= build-ps4
VERSION  := $(shell head -1 version.txt)
RT       := lib/N64ModernRuntime

TARGET   := --target=x86_64-pc-freebsd12-elf
COMMON   := $(TARGET) -D__PS4__ -D__OPENORBIS__ -D__ORBIS__ -D_GNU_SOURCE -D_DEFAULT_SOURCE -fPIC -funwind-tables -DRMLUI_STATIC_LIB -DLUNASVG_BUILD_STATIC -DRMLUI_FONT_ENGINE_FREETYPE -DRMLUI_SVG_PLUGIN \
            -DRMLUI_VERSION='"6.0"' -DHLSL_CPU \
            -isysroot $(OO) -O2 -g0 -march=btver2 -fno-strict-aliasing -w -MMD -MP
INCS     := -Ips4/compat -Ips4/stubs -Ips4/third_party/SDL2/include -I$(RT)/ultramodern/include -I$(RT)/librecomp/include -I$(RT)/librecomp/include/librecomp \
            -I$(RT)/librecomp/src -I$(RT)/thirdparty -I$(RT)/thirdparty/concurrentqueue -I$(RT)/thirdparty/o1heap \
            -I$(RT)/thirdparty/miniz -Iinclude -Isrc -Ilib/N64Recomp/include -Ilib/N64Recomp/lib/fmt/include \
            -Ilib/N64Recomp/lib/rabbitizer/include -Ilib/N64Recomp/lib/rabbitizer/cplusplus/include \
            -Ilib/concurrentqueue -Ilib/GamepadMotionHelpers -Ilib/SlotMap/slot_map -Ilib/promptfont -Ilib/lua/src \
            -Ips4/stubs -Ilib/RmlUi/Include -Ilib/RmlUi/Backends -Ilib/lunasvg/include -Ilib/lunasvg/3rdparty/plutovg \
            -I$(HOME)/ps4dev/cse2/external/freetype/include -Ilib/rt64/src/contrib -Ilib/rt64/src/contrib/imgui
CFLAGS   := $(COMMON) $(INCS) -isystem $(OO)/include
CXXFLAGS := $(COMMON) $(INCS) -std=c++20 -fno-char8_t -fms-extensions -include ps4_compat.hpp -isystem $(OO)/include/c++/v1 -isystem $(OO)/include \
            -DJFG_VERSION_STRING='"$(VERSION)"'

# Port sources: everything but the RT64 renderer and what is tied to it. PS4 replacements live in ps4/src: main, gfx
# (window), audio (sceAudioOut), renderer. The menus' RT64 pieces (state, launcher, config, mod menu and installer, the
# UI and ImGui render backends) come back with their GNM render interface; until then ps4/src/ui_stubs.cpp answers.
SRC_EXCLUDE := src/renderer/% src/ui/ui_renderer.cpp src/ui/ui_state.cpp src/ui/ui_launcher.cpp src/ui/ui_config.cpp \
               src/ui/ui_mod_menu.cpp src/ui/ui_mod_installer.cpp src/ui/ui_elements.cpp src/ui/ui_mod_details_panel.cpp \
               src/ui/ui_config_sub_menu.cpp src/ui/ui_language_prompt.cpp \
               src/debug_ui/backend.cpp \
               src/main.cpp src/runtime/gfx.cpp src/runtime/audio.cpp
# N64Recomp's live recompiler (code mods): not built on PS4 (no JIT memory); kept for reference.
N64R      := lib/N64Recomp
N64R_SRC  := $(addprefix $(N64R)/src/,symbol_lists.cpp analysis.cpp operations.cpp cgenerator.cpp recompilation.cpp \
               mod_symbols.cpp) $(N64R)/LiveRecomp/live_generator.cpp $(N64R)/lib/sljit/sljit_src/sljitLir.c \
             $(wildcard $(N64R)/lib/rabbitizer/src/*/*.c $(N64R)/lib/rabbitizer/src/*/*/*.c \
               $(N64R)/lib/rabbitizer/cplusplus/src/*/*.cpp)
PORT_SRC  := $(filter-out $(SRC_EXCLUDE),$(wildcard src/*.cpp src/*/*.cpp src/*/*/*.cpp))
GAME_SRC  := $(wildcard RecompiledFuncs/*.c)
RT_SRC    := $(wildcard $(RT)/ultramodern/src/*.cpp $(RT)/librecomp/src/*.cpp) $(RT)/thirdparty/o1heap/o1heap/o1heap.c \
             $(wildcard $(RT)/thirdparty/miniz/*.c)
LUA_SRC   := $(filter-out lib/lua/src/lua.c lib/lua/src/luac.c,$(wildcard lib/lua/src/*.c))
IMGUI_SRC := $(filter-out %imgui_demo.cpp,$(wildcard lib/rt64/src/contrib/imgui/imgui*.cpp))
RML_SRC   := $(wildcard lib/RmlUi/Source/Core/*.cpp lib/RmlUi/Source/Core/*/*.cpp lib/RmlUi/Source/SVG/*.cpp \
             lib/RmlUi/Source/Debugger/*.cpp) lib/RmlUi/Backends/RmlUi_Platform_SDL.cpp \
             $(wildcard lib/lunasvg/source/*.cpp lib/lunasvg/3rdparty/plutovg/*.c)
PS4_SRC   := $(wildcard ps4/src/*.cpp ps4/src/*.c)
ALL_SRC   := $(GAME_SRC) rsp/aspMain.cpp $(RT_SRC) $(LUA_SRC) $(IMGUI_SRC) $(RML_SRC) $(PORT_SRC) $(PS4_SRC)
OBJS      := $(patsubst %,$(BUILD)/%.o,$(ALL_SRC))

all: $(BUILD)/objs.stamp
$(BUILD)/objs.stamp: $(OBJS)
	@touch $@

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	@clang $(CFLAGS) -c $< -o $@ 2> $@.err || { echo "FAIL $<"; head -c 1500 $@.err; false; }
$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	@clang++ $(CXXFLAGS) -c $< -o $@ 2> $@.err || { echo "FAIL $<"; head -c 1500 $@.err; false; }

-include $(OBJS:.o=.d)
.PHONY: all

# ---- link and package
FREETYPE := $(HOME)/ps4dev/cse2/build-ps4/external/freetype/libfreetype.a
# musl's sem_* (libc.a) use the Linux futex syscall: every one goes to ps4/src/sem_ps4.c instead.
SEM_WRAPS := $(foreach f,sem_init sem_destroy sem_wait sem_trywait sem_timedwait sem_post sem_getvalue,--wrap=$(f))
SCE_LIBS := -lSceVideoOut -lSceAudioOut -lScePad -lSceUserService -lSceSystemService -lSceSysmodule -lSceGnmDriver
ELF      := $(BUILD)/jfg.elf
EBOOT    := $(BUILD)/eboot.bin

eboot: $(EBOOT)
$(ELF): $(OBJS)
	@printf '%s\n' $(OBJS) > $(BUILD)/objs.rsp
	ld.lld --error-limit=0 --wrap=pthread_create --wrap=clock_gettime $(SEM_WRAPS) -m elf_x86_64 -pie --eh-frame-hdr --script $(OO)/link.x $(OO)/lib/crt1.o -L$(OO)/lib @$(BUILD)/objs.rsp \
	    $(FREETYPE) -lc -lkernel -lc++ $(SCE_LIBS) -o $@
$(EBOOT): $(ELF)
	OO_PS4_TOOLCHAIN=$(OO) $(OO)/bin/linux/create-fself -in=$< -out=$(BUILD)/jfg.oelf --eboot $@ --paid 0x3800000000000011
.PHONY: eboot
