# The pc platform (#1093, docs/platforms.md): Crash Bandicoot XS built
# for the host, a 64-bit Linux with a stock g++/gcc, by
# `make PLATFORM=pc`. The Makefile includes this file instead of the GBA's
# link and compile rules (compile.mk); everything before that is shared:
# the object lists, the generated data and headers (graphics.mk,
# levels.mk, text.mk, cutscenes.mk, the sound) and their dependencies,
# all under build/pc/crashbandicootxs/.
#
# The game compiles and links into a host executable, with the platform's
# functions in plain C (platform/pc/src/): the frame loop, a software PPU,
# an SDL2 window with the keyboard and gamepads, and GAX2's sound
# (docs/platforms.md).

HOST_CC      ?= cc
HOST_CXX     ?= c++
HOST_NM      ?= nm
HOST_CXXFILT ?= c++filt
HOST_OBJCOPY ?= objcopy

# Windows: a cross (or native) MinGW-w64 compiler, e.g.
#   make PLATFORM=pc OBJ_DIR=build/win/crashbandicootxs HOST_CC=x86_64-w64-mingw32-gcc HOST_CXX=x86_64-w64-mingw32-g++ \
#        HOST_NM=x86_64-w64-mingw32-nm HOST_CXXFILT=x86_64-w64-mingw32-c++filt \
#        HOST_OBJCOPY=x86_64-w64-mingw32-objcopy SDL2_PREFIX=path/to/SDL2/x86_64-w64-mingw32
# makes one self-contained .exe: SDL2, libgcc and libstdc++ linked in
# statically, a GUI program (no console window). HOST_WINDOWS is found
# from the compiler's target.
HOST_WINDOWS ?= $(if $(findstring mingw,$(shell $(HOST_CC) -dumpmachine 2>/dev/null)),1,0)

# Android (platform/android/): the game's core for the app, which runs it
# with --stream (src/stream.c). A static executable (an ARM64 or x86-64
# Linux one: no SDL, no shared libraries, so Android's own C library isn't
# needed), named as a native library so that the APK installs it where an
# app may run it:
#   make PLATFORM=pc HOST_ANDROID=1 HOST_SDL=0 OBJ_DIR=build/android-arm64/crashbandicootxs \
#        HOST_CC=aarch64-linux-gnu-gcc HOST_CXX=aarch64-linux-gnu-g++ HOST_NM=aarch64-linux-gnu-nm \
#        HOST_CXXFILT=aarch64-linux-gnu-c++filt HOST_OBJCOPY=aarch64-linux-gnu-objcopy
HOST_ANDROID ?= 0

# The port's version (host_platform.h's HOST_PORT_VERSION, the one place
# it is written): in the Windows executable's name and version resource.
HOST_PORT_VERSION := $(shell sed -n 's/^\#define HOST_PORT_VERSION "\(.*\)"/\1/p' $(PLATFORM_DIR)/src/host_platform.h)
# As numbers for the resource's FILEVERSION: v0.06 -> 0,6.
HOST_PORT_VERSION_NUM := $(shell echo $(patsubst v%,%,$(HOST_PORT_VERSION)) | awk -F. '{ printf "%d,%d", $$1, $$2 }')

ifeq ($(HOST_ANDROID),1)
HOST_EXE := $(patsubst %/,%,$(dir $(OBJ_DIR)))/libcrashxs_core.so
else ifeq ($(HOST_WINDOWS),1)
HOST_EXE := $(patsubst %/,%,$(dir $(OBJ_DIR)))/CrashBandicootXS-$(HOST_PORT_VERSION).exe
else
HOST_EXE := build/$(PLATFORM)/crashbandicootxs_$(PLATFORM)
endif

# The window (src/sdl.c) needs SDL2, found by pkg-config; without it (or
# with HOST_SDL=0) the executable runs headless only, which is all the
# tests need. Not finding it is said (a warning), since the executable
# then has no window; HOST_SDL=0 on the command line is taken as meant.
HOST_PKG_CONFIG ?= pkg-config
ifeq ($(HOST_WINDOWS),1)
# SDL2's MinGW development files (SDL2-devel-*-mingw's x86_64-w64-mingw32/),
# linked statically: libSDL2.a and the Windows libraries it uses.
SDL2_PREFIX ?=
ifneq ($(SDL2_PREFIX),)
HOST_SDL ?= 1
HOST_SDL_CFLAGS := -DHOST_SDL -I$(SDL2_PREFIX)/include/SDL2 -I$(SDL2_PREFIX)/include
HOST_SDL_LIBS := $(SDL2_PREFIX)/lib/libSDL2.a -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 \
	-loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8
else
HOST_SDL ?= 0
endif
else
HOST_SDL ?= $(shell $(HOST_PKG_CONFIG) --exists sdl2 2>/dev/null && echo 1 || echo 0)
endif
ifeq ($(HOST_WINDOWS),1)
else ifeq ($(HOST_SDL),1)
HOST_SDL_CFLAGS := -DHOST_SDL $(shell $(HOST_PKG_CONFIG) --cflags sdl2)
HOST_SDL_LIBS := $(shell $(HOST_PKG_CONFIG) --libs sdl2)
else ifneq ($(origin HOST_SDL),command line)
# Said once, not again when make restarts for the generated makefiles.
ifeq ($(MAKE_RESTARTS),)
$(warning $(HOST_PKG_CONFIG) finds no sdl2: building $(HOST_EXE) without a window (headless only))
endif
endif

.PHONY: all
all: $(HOST_EXE)

# The flags. The include path and defines are the GBA build's (CPPFLAGS,
# with this platform's include directory in the GBA's place), without
# agbcc's own headers: the host's C library is used. The game is g++ 2.9
# C++, built here as GNU C++98 with the GBA's no-RTTI/no-exceptions:
# - -funsigned-char: `char` is unsigned on the ARM, signed on x86.
# - -fno-strict-aliasing, -fwrapv: the code was written for (and matched
#   against) gcc 2.9, which assumed neither.
# - -fno-builtin: the game has its own strcpy, strlen, strcat, sprintf
#   (string.cpp, printf.cpp), with u8 * signatures.
# - -fno-toplevel-reorder: the data in a file's order, as agbcc emits it
#   and as the game reads some of it: a cutscene picture's bitmap right
#   after its palette (slideshow.cpp reads it at +0x200), which are two
#   definitions (cutscene_pictures.cpp). A modern g++ otherwise emits them
#   in reverse order. (Both are aligned(4), so the host's alignment of a
#   large array to 16 or 32 bytes leaves no gap either.)
# No -Werror: the host compiler's warnings aren't the build's (#577's
# warnings are checked on the GBA build).
HOST_CPPFLAGS = $(subst -I tools/agbcc/include,,$(filter-out -nostdinc -undef,$(CPPFLAGS)))
HOST_FLAGS := -g -femit-class-debug-always -O1 -funsigned-char -fno-strict-aliasing -fwrapv -fno-builtin -fno-toplevel-reorder
# MinGW lays bitfields out as MSVC does by default (-mms-bitfields), not
# as the GBA's (and Linux's) gcc does: the hardware structs (OAM's) need
# the latter.
ifeq ($(HOST_WINDOWS),1)
HOST_FLAGS += -mno-ms-bitfields
endif
HOST_CXXFLAGS := $(HOST_FLAGS) -std=gnu++98 -fno-rtti -fno-exceptions
HOST_CFLAGS := $(HOST_FLAGS) -std=gnu99

# The sanitizers: `make PLATFORM=pc SANITIZE=undefined` (or address, or
# both: SANITIZE=address,undefined; docs/platforms.md, "Sanitizers").
# -fsanitize goes on every compile and on the link, which brings in the
# sanitizers' runtimes (libubsan, libasan): without it the objects'
# __ubsan_handle_* and __asan_* calls are undefined. SANITIZE_FLAGS adds
# to them (-fno-sanitize=..., -fsanitize-recover=...). The objects are
# built again when either changes (the flags file, below).
SANITIZE ?=
SANITIZE_FLAGS ?=
ifneq ($(SANITIZE),)
HOST_SANITIZE_FLAGS := -fsanitize=$(SANITIZE) -fno-omit-frame-pointer $(SANITIZE_FLAGS)
HOST_CXXFLAGS += $(HOST_SANITIZE_FLAGS)
HOST_CFLAGS += $(HOST_SANITIZE_FLAGS)
endif

# ASan puts a redzone after each global, but some data files are read
# across their definitions, back to back as in the ROM (and as
# -fno-toplevel-reorder keeps them): a cutscene picture's bitmap right
# after its palette (+0x200, slideshow.cpp), a boss picture's frames
# after its palette (+0x204 on, Airship::ConvertTiles and the
# hovercraft's), the sprite tile pool, 56 arrays read from bank 0's
# (tileBase + a frame's offset), the three empty sprite frames' box,
# read past their header (in the ROM the next frame's header; on the
# host padding, which src/sprite_boxes.c checks), the polar boxes read
# as Aku Aku's palette (polar_palettes_boxes.cpp) and Dingodile's motion
# records read past their table (boss_motion_tables.cpp), both aligned(4)
# there for the host's g++. With redzones those
# reads are overflows, and the data isn't where it is read. Their
# globals aren't instrumented (--param asan-globals=0: no redzones, so
# nothing between them); every other global is.
ifneq ($(findstring address,$(SANITIZE)),)
$(C_BUILDDIR)/game/ui/data/cutscene_pictures.o $(C_BUILDDIR)/game/bosses/data/boss_pictures.o \
	$(C_BUILDDIR)/engine/gfx/data/sprite_tiles.o $(C_BUILDDIR)/engine/gfx/data/sprite_banks_00_09.o \
	$(C_BUILDDIR)/engine/gfx/data/sprite_banks_39_55.o \
	$(C_BUILDDIR)/game/vehicles/polar/data/polar_palettes_boxes.o \
	$(C_BUILDDIR)/game/bosses/data/boss_motion_tables.o: HOST_CXXFLAGS += --param asan-globals=0
endif

# entity.o is the one key-method object where g++ 2.9 (`#pragma
# implementation`) emitted out-of-line copies of its class's inline
# methods that the ROM uses (compile.mk, NO_IMPLEMENT_INLINES_OBJS):
# other files call SetEntityPos, SetEntityPixelPos (entity.hpp) and the
# constructor out of line. A modern g++ emits only the copies a file
# uses itself, so entity.o keeps them all (g++ only: clang has no such
# option, and with it the link misses those methods).
$(C_BUILDDIR)/game/entities/entity.o: HOST_CXXFLAGS += -fkeep-inline-functions

# Units: a file platform/pc/units/<path>.cpp is built in place of
# engine/src/<path>.cpp, into the same object (so the shared dependencies and
# flags of that object still apply). For a unit the host can't compile
# as it is: bios_util.cpp, whose DivMod is an inline ARM `svc`.
PLATFORM_UNITS := $(shell find $(PLATFORM_DIR)/units -name '*.cpp')
PLATFORM_UNIT_OBJS := $(patsubst $(PLATFORM_DIR)/units/%.cpp,$(C_BUILDDIR)/%.o,$(PLATFORM_UNITS))

# The platform library (platform/pc/src/): the host memory, DMA, the BIOS
# calls, startup and IRQ dispatch, the audio output and GAX2's ARM
# routines in C, AgbEeprom's API (which replaces lib/agb_eeprom),
# lib/libgcc's division helpers the game and GAX2 call by name (the rest
# of libgcc is the host compiler's), and the sound blocks of games/xs/data/data.s.
PLATFORM_SRCS := $(wildcard $(PLATFORM_DIR)/src/*.c $(PLATFORM_DIR)/src/*.S $(PLATFORM_DIR)/src/*.cpp)
PLATFORM_OBJS := $(patsubst $(PLATFORM_DIR)/src/%,$(PLATFORM_BUILDDIR)/src/%.o,$(basename $(PLATFORM_SRCS)))

# The settings menu's art (src/ui.c): the game's HUD fonts and pause
# screen, as C arrays (tools/ui_assets.py).
HOST_UI_ASSETS := $(PLATFORM_BUILDDIR)/ui_assets.c
$(HOST_UI_ASSETS): $(PLATFORM_DIR)/tools/ui_assets.py engine/src/game/ui/data/hud_font_tables.cpp \
		games/xs/graphics/fonts/small.png games/xs/graphics/fonts/large.png games/xs/graphics/menus/pause.png \
		games/xs/data/levels/levels.json $(wildcard games/xs/graphics/level_select/pictures/*.png) \
		$(wildcard games/xs/graphics/sprites/bank3[2378]_*/f*.png) $(wildcard games/xs/graphics/sprites/palettes/*.pal)
	@mkdir -p $(dir $@)
	python3 $(PLATFORM_DIR)/tools/ui_assets.py --fonts-cpp engine/src/game/ui/data/hud_font_tables.cpp \
		--small games/xs/graphics/fonts/small.png --large games/xs/graphics/fonts/large.png \
		--pause games/xs/graphics/menus/pause.png --levels games/xs/data/levels/levels.json \
		--graphics games/xs/graphics --sprites games/xs/graphics/sprites -o $@
$(HOST_UI_ASSETS:.c=.o): $(HOST_UI_ASSETS)
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<

# GAX2 (lib/gax, docs/audio.md): its matched C, built for the host, with
# a unit of platform/pc/units/lib/gax/ in place of each file that can't
# run there as it is (the ARM routines' calls and the resampler's patches,
# the work RAM carved by the GBA's sizes, the DMA and SWI asm; gax_host.h).
# The song data's structures with pointers are generated again as C
# (tools/gax_host_data.py, from tools/gax_audio.py's .s files), laid out
# by the host compiler; the rest is read in place from the built blocks.
HOST_GAX_SRCS := $(wildcard lib/gax/src/*.c lib/gax/data/*.c)
HOST_GAX_OBJS := $(patsubst lib/%.c,$(LIB_BUILDDIR)/%.o,$(HOST_GAX_SRCS))
HOST_GAX_UNITS := $(shell find $(PLATFORM_DIR)/units/lib -name '*.c')
HOST_GAX_UNIT_OBJS := $(patsubst $(PLATFORM_DIR)/units/lib/%.c,$(LIB_BUILDDIR)/%.o,$(HOST_GAX_UNITS))
HOST_GAX_DATA := $(PLATFORM_BUILDDIR)/gax_host_data.c
HOST_GAX_FLAGS = -iquote lib/gax/src -iquote $(PLATFORM_DIR)/src

# The inline methods of the headers with `#pragma interface` (many of
# engine/include's classes, docs/cplusplus.md "Emitting the vtables"):
# g++ emits no out-of-line copy of them in the files that include such a
# header. On the GBA no call to one is left out of line, and a modern g++
# inlines them all at -O1 too, but not always with -fsanitize, whose
# checks make the methods bigger (with UBSan: LinkRing::Push and Pop,
# CrateList::Append, Detach and ResetGrid), and then nothing defines
# them. So a generated file, inlines.cpp, includes each such header after
# a `#pragma implementation` of its name, which has g++ emit the copies
# there (weak, as any inline function's), called or not.
HOST_INTERFACE_HEADERS := $(shell grep -rl '^.pragma interface' engine/include | sort)
HOST_INLINES := $(PLATFORM_BUILDDIR)/inlines.cpp

$(HOST_INLINES): $(HOST_INTERFACE_HEADERS) $(PLATFORM_DIR)/platform.mk
	@mkdir -p $(dir $@)
	@{ echo '// Generated by platform/pc/platform.mk: the headers with #pragma interface.'; \
	  $(foreach h,$(HOST_INTERFACE_HEADERS),echo '#pragma implementation "$(notdir $(h))"';) \
	  $(foreach h,$(HOST_INTERFACE_HEADERS),echo '#include "$(patsubst engine/include/%,%,$(h))"';) } > $@

$(HOST_INLINES:.cpp=.o): $(HOST_INLINES) $(HOST_RENAMES) | $(GENERATED_HEADERS)
	$(HOST_CXX) $(HOST_CPPFLAGS) $(HOST_CXXFLAGS) $(DEPFLAGS) -c -o $@ $<
	$(HOST_OBJCOPY) --redefine-syms=$(HOST_RENAMES) $@

# xBRZ (platform/pc/xbrz/, the "cartoon" filters): modern C++, built on
# its own, not with the game's flags.
# (Only with the window: without SDL, as the Android core, no filters.)
HOST_XBRZ_OBJS :=
ifeq ($(HOST_SDL),1)
HOST_XBRZ_OBJS := $(PLATFORM_BUILDDIR)/xbrz/xbrz.o $(PLATFORM_BUILDDIR)/xbrz/xbrz_c.o
endif
HOST_OBJS := $(HOST_XBRZ_OBJS) $(C_OBJS) $(CXX_OBJS) $(PLATFORM_OBJS) $(HOST_GAX_OBJS) $(HOST_GAX_DATA:.c=.o) $(HOST_INLINES:.cpp=.o) \
	$(HOST_UI_ASSETS:.c=.o)

$(PLATFORM_OBJS) $(HOST_GAX_OBJS): | $(GENERATED_HEADERS)

# The link. host_link.py writes a linker script of symbol assignments,
# the host's version of what ldscript.txt and the objcopy renames do on
# the GBA: the linker-placed IWRAM and EWRAM globals (sym_iwram.txt,
# sym_ewram.txt, which have no C definitions) inside the host's work
# RAMs, and the C names the code uses for C++ symbols (cxx_symbols.txt,
# the asm("CName") labels) bound to the host compiler's mangled names.
CXX_SYMBOLS := cxx_symbols.txt
SYM_IWRAM := $(dir $(LDSCRIPT))sym_iwram.txt
SYM_EWRAM := $(dir $(LDSCRIPT))sym_ewram.txt
HOST_LINK := $(OBJ_DIR)/host_link.ld

#
# The game's own strcpy, strlen, rand, sprintf, ... and its global
# operator new and delete (libc_names.txt) are renamed in its objects as
# they are built, so that they don't stand in for the C library's and
# libstdc++'s in the executable; host_link.py checks that no other name
# clashes with the host's libraries (HOST_LIBS).
HOST_RENAMES := $(PLATFORM_DIR)/libc_names.txt
ifeq ($(HOST_WINDOWS),1)
# 64-bit Windows's size_t is unsigned long long: operator new's names
# there end in y, not m. (Without these, every C++ library's `new` came
# from the game's small heap: xBRZ's table, the cartoon filters.)
HOST_RENAMES := $(patsubst %/,%,$(dir $(OBJ_DIR)))/libc_names_win.txt
$(HOST_RENAMES): $(PLATFORM_DIR)/libc_names.txt
	@mkdir -p $(dir $@)
	sed -e 's/^_Znwm /_Znwy /' -e 's/^_Znam /_Znay /' $< > $@
endif
HOST_LIBS := $(wildcard $(shell $(HOST_CC) -print-file-name=libc.so.6) $(shell $(HOST_CC) -print-file-name=libm.so.6) \
	$(shell $(HOST_CXX) -print-file-name=libstdc++.so.6))

$(HOST_LINK): $(HOST_OBJS) $(CXX_SYMBOLS) $(SYM_IWRAM) $(SYM_EWRAM) $(HOST_RENAMES) $(PLATFORM_DIR)/tools/host_link.py
	python3 $(PLATFORM_DIR)/tools/host_link.py --nm '$(HOST_NM)' --cxxfilt '$(HOST_CXXFILT)' \
		--cxx-symbols $(CXX_SYMBOLS) --iwram $(SYM_IWRAM) --ewram $(SYM_EWRAM) --libs $(HOST_LIBS) \
		--renames $(HOST_RENAMES) -o $@ $(HOST_OBJS)

# --level and --room (src/level_pick.c) wrap the two functions that pick
# the level and its room.
comma := ,
HOST_WRAPS := _ZN4Game2Ui11LevelSelect3RunEPi _ZN4Game5Level13LevelProgress10SelectRoomEv
# The themed borders (src/borders.c) know the level being played.
HOST_WRAPS += _ZN4Game5Level13LevelProgress8PlayRoomEv
# Rumble (src/rumble.c) follows the game's sound effects.
HOST_WRAPS += _ZN6Engine5Audio12AudioContext7PlaySfxEjj
# The HUD setting (src/hud_hook.cpp) wraps the HUD's update.
HOST_WRAPS += _ZN4Game2Ui3Hud6UpdateEv
# Skip intro (src/intro_skip.c) wraps the boot's steps after the legal notice.
HOST_WRAPS += _ZN4Game5Level10LevelState16ShowCompanyLogosEv _ZN4Game5Level10LevelState17PlayIntroCutsceneEv \
	OpenLanguageSelect RunLanguageSelect CloseLanguageSelect
# Auto save (src/autosave_hooks.cpp) follows the save menu and the title.
HOST_WRAPS += _ZN4Game2Ui11TitleScreen3RunEv _ZN4Game2Ui8SaveMenu3RunEjj
# --cutscene (src/cutscene_pick.c) wraps the boot's cutscene.
HOST_WRAPS += _ZN4Game5Level10LevelState16PlayBootCutsceneEv
# --gax-log (src/gax_trace.c) wraps GAX2's API.
HOST_WRAPS += GAX2_init GAX_fx_ex GAX_fx GAX_fx_note GAX_stop_fx GAX_set_music_volume GAX_set_fx_volume \
	GAX_stop GAX_pause GAX_resume GAX_play GAX_irq GAX2_jingle
# The frame interpolation (src/interp_tags.cpp) wraps the sprite renderer's
# entry points and the OAM buffer's, to know whose each OAM entry is.
HOST_WRAPS += _ZN6Engine3Gfx9OamBuffer3AddEPKv _ZN6Engine3Gfx9OamBuffer6AppendEPvi _ZN6Engine3Gfx9OamBuffer6CommitEv \
	_ZN6Engine3Gfx14SpriteRenderer4DrawEPN4Game8Entities6SpriteE \
	_ZN6Engine3Gfx14SpriteRenderer6DrawAtEPN4Game8Entities6SpriteEii \
	_ZN6Engine3Gfx14SpriteRenderer10DrawPiecesEPN4Game8Entities6SpriteEPi \
	_ZN6Engine3Gfx14SpriteRenderer16DrawAffinePiecesEPN4Game8Entities6SpriteEPi \
	_ZN6Engine3Gfx14SpriteOamQueue3AddEjti _ZN6Engine3Gfx14SpriteOamQueue5FlushEv

# Windows: everything static, one file; a GUI program; the icon and
# version. The host memory's section (memory.S, .gbamem) must start on a
# 1 MB boundary, as the ELF build's .bss alignment puts it, so that its
# regions, 1 MB apart inside it, are each on one: every section of the
# image is aligned so (in memory only: the file's alignment is unchanged),
# without ASLR, whose 64 KB steps would undo it. And a few methods are
# defined both inline in a header and out of line in a .cpp (the ROM's
# out-of-line copies, ActorSelf::~ActorSelf, Sprite::SetFlipX/Y), with the
# same body: ELF links the strong one, PE's COMDAT rules call it a
# duplicate.
ifeq ($(HOST_WINDOWS),1)
HOST_WINDRES ?= $(patsubst %gcc,%windres,$(HOST_CC))
HOST_WIN_RES := $(PLATFORM_BUILDDIR)/win_resource.o
$(HOST_WIN_RES): $(PLATFORM_DIR)/win/resource.rc $(wildcard $(PLATFORM_DIR)/win/*.ico) $(PLATFORM_DIR)/src/host_platform.h
	@mkdir -p $(dir $@)
	$(HOST_WINDRES) -I $(PLATFORM_DIR)/win -D 'HOST_VERSION_STR=\"$(HOST_PORT_VERSION)\"' \
		-D 'HOST_EXE_NAME=\"CrashBandicootXS-$(HOST_PORT_VERSION).exe\"' \
		-D HOST_VERSION_NUM=$(HOST_PORT_VERSION_NUM) -o $@ $<
HOST_LINK_EXTRA := $(HOST_WIN_RES)
HOST_LINK_FLAGS := -static -static-libgcc -static-libstdc++ -Wl,--disable-dynamicbase \
	-Wl,--section-alignment,0x100000 -Wl,--allow-multiple-definition
ifeq ($(HOST_SDL),1)
HOST_LINK_FLAGS += -mwindows
endif
endif

ifeq ($(HOST_ANDROID),1)
HOST_LINK_FLAGS := -static
# A phone's CPU draws the picture: the PPU optimised.
$(PLATFORM_BUILDDIR)/src/ppu.o: HOST_CFLAGS += -O2
endif

$(HOST_EXE): $(HOST_OBJS) $(HOST_LINK) $(HOST_LINK_EXTRA)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_SANITIZE_FLAGS) $(HOST_LINK_FLAGS) -o $@ $(HOST_OBJS) $(HOST_LINK) $(HOST_LINK_EXTRA) \
		$(addprefix -Wl$(comma)--wrap=,$(HOST_WRAPS)) $(HOST_SDL_LIBS) -lm

$(PLATFORM_UNIT_OBJS): $(C_BUILDDIR)/%.o: $(PLATFORM_DIR)/units/%.cpp $(HOST_RENAMES)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_CPPFLAGS) $(HOST_CXXFLAGS) $(DEPFLAGS) -c -o $@ $<
	$(HOST_OBJCOPY) --redefine-syms=$(HOST_RENAMES) $@

$(C_BUILDDIR)/%.o: $(C_SUBDIR)/%.cpp $(HOST_RENAMES)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_CPPFLAGS) $(HOST_CXXFLAGS) $(DEPFLAGS) -c -o $@ $<
	$(HOST_OBJCOPY) --redefine-syms=$(HOST_RENAMES) $@

$(C_BUILDDIR)/%.o: $(C_SUBDIR)/%.c $(HOST_RENAMES)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) $(DEPFLAGS) -c -o $@ $<
	$(HOST_OBJCOPY) --redefine-syms=$(HOST_RENAMES) $@

$(HOST_GAX_UNIT_OBJS): $(LIB_BUILDDIR)/%.o: $(PLATFORM_DIR)/units/lib/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_GAX_FLAGS) $(HOST_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(LIB_BUILDDIR)/gax/%.o: lib/gax/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_GAX_FLAGS) $(HOST_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(HOST_GAX_DATA): $(SOUND_BUILT) $(SOUND_BUILDDIR)/gax_songs.h $(PLATFORM_DIR)/tools/gax_host_data.py
	@mkdir -p $(dir $@)
	python3 $(PLATFORM_DIR)/tools/gax_host_data.py --music $(SOUND_BUILDDIR)/gax_audio_data.s \
		--sfx $(SOUND_BUILDDIR)/gax_sfx_data.s --default $(SOUND_BUILDDIR)/gax_default_layout.s \
		--songs $(SOUND_BUILDDIR)/gax_songs.h -o $@

$(HOST_GAX_DATA:.c=.o): $(HOST_GAX_DATA) | $(GENERATED_HEADERS)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_GAX_FLAGS) $(HOST_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(PLATFORM_BUILDDIR)/src/%.o: $(PLATFORM_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(PLATFORM_BUILDDIR)/src/%.o: $(PLATFORM_DIR)/src/%.cpp $(HOST_RENAMES)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_CPPFLAGS) -iquote $(PLATFORM_DIR)/src $(HOST_CXXFLAGS) $(DEPFLAGS) -c -o $@ $<
	$(HOST_OBJCOPY) --redefine-syms=$(HOST_RENAMES) $@

$(PLATFORM_BUILDDIR)/xbrz/%.o: $(PLATFORM_DIR)/xbrz/%.cpp $(PLATFORM_DIR)/xbrz/xbrz.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 -O2 -fno-exceptions -fno-rtti -w -c -o $@ $<

$(PLATFORM_BUILDDIR)/src/%.o: $(PLATFORM_DIR)/src/%.S
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CPPFLAGS) $(DEPFLAGS) -c -o $@ $<

# The interpolation's PPU draws up to 240 pictures a second at 4x: optimised.
$(PLATFORM_BUILDDIR)/src/ppu_hd.o: HOST_CFLAGS += -O2

# sdl.o is rebuilt when HOST_SDL changes (the flags file).
HOST_SDL_OBJS := $(addprefix $(PLATFORM_BUILDDIR)/src/,sdl.o config.o borders.o discord.o timer.o rumble.o)
$(HOST_SDL_OBJS): HOST_CFLAGS += $(HOST_SDL_CFLAGS)
$(HOST_SDL_OBJS): $(PLATFORM_BUILDDIR)/sdl_flags
$(PLATFORM_BUILDDIR)/sdl_flags: FORCE
	@mkdir -p $(dir $@)
	@echo '$(HOST_SDL_CFLAGS) $(HOST_SDL_LIBS)' | cmp -s - $@ || echo '$(HOST_SDL_CFLAGS) $(HOST_SDL_LIBS)' > $@

# Every object and the executable are rebuilt when SANITIZE or
# SANITIZE_FLAGS changes.
$(HOST_OBJS) $(HOST_EXE): $(PLATFORM_BUILDDIR)/sanitize_flags
$(PLATFORM_BUILDDIR)/sanitize_flags: FORCE
	@mkdir -p $(dir $@)
	@echo '$(HOST_SANITIZE_FLAGS)' | cmp -s - $@ || echo '$(HOST_SANITIZE_FLAGS)' > $@

.PHONY: FORCE
FORCE:

$(PLATFORM_BUILDDIR)/src/sound_data.o: $(SOUND_BUILT)
$(PLATFORM_BUILDDIR)/src/sound_data.o: HOST_CPPFLAGS += -D 'SFX_TABLE_BIN="$(SOUND_BUILDDIR)/sfx_table.bin"' \
	-D 'GAX_MUSIC_BIN="$(SOUND_BUILDDIR)/gax_audio_data.bin"' -D 'GAX_SFX_BIN="$(SOUND_BUILDDIR)/gax_sfx_data.bin"'

# The platform's own GAX2 objects find lib/gax's internal header and
# gax_host.h.
$(PLATFORM_BUILDDIR)/src/gax_dsp.o $(PLATFORM_BUILDDIR)/src/audio.o $(PLATFORM_BUILDDIR)/src/gax_trace.o: \
	HOST_CFLAGS += $(HOST_GAX_FLAGS)

-include $(PLATFORM_OBJS:.o=.d) $(HOST_GAX_OBJS:.o=.d) $(HOST_GAX_DATA:.c=.d) $(HOST_INLINES:.cpp=.d)
