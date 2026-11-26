#
# EDuke32 Makefile for GNU Make
#

### Global Profiles
ifeq ($(FURY),1)
    APPBASENAME := fury
    APPNAME := Ion Fury
    NETCODE := 0
    POLYMER := 0
    RETAIL_MENU := 1
    STANDALONE := 1
    USE_LIBVPX := 0
    SDL_STATIC := 1
endif

### Platform and Toolchain Configuration
include Common.mak

# NNBuild is the multi-game binary.  A plain `make nnbuild` should
# include every linked game by default; callers can still override any one with
# WITH_SW=0, WITH_BLOOD=0, WITH_RR=0, or WITH_EXHUMED=0.
ifneq (,$(filter nnbuild,$(MAKECMDGOALS)))
    WITH_SW ?= 1
    WITH_BLOOD ?= 1
    WITH_RR ?= 1
    WITH_EXHUMED ?= 1
endif

### File Extensions
asm := nasm
o := o

### Directories
source := source
obj := obj

### Functions
# Some of this still needs work--"getfiltered" takes a list of files and strips the paths off of them,
# while the other functions end up expanding the result later by adding the paths back.
# This is inefficient, but it was a better use of time than reworking all of this at the moment.
define parent
$(word 1,$(subst _, ,$1))
endef
define expandobjs
$$(addprefix $$($$(call parent,$1)_obj)/,$$(addsuffix .$$o,$$(basename $$($1_objs) $$($1_rsrc_objs) $$($1_gen_objs))))
endef
define expandsrcs
$(addprefix $($(call parent,$1)_src)/,$($1_objs)) $(addprefix $($(call parent,$1)_rsrc)/,$($1_rsrc_objs)) $(addprefix $($(call parent,$1)_obj)/,$($1_gen_objs))
endef
define expanddeps
$(strip $1 $(foreach j,$1,$(call $0,$($j_deps))))
endef
define getdeps
$(call expanddeps,$1_$2 $(common_$2_deps) engine)
endef
define getfiltered
$(filter-out $(strip $($1_excl)), $(subst $($1_src)/, ,$(wildcard $($1_src)/$2)))
endef

##### External Library Definitions

#### discord-rpc

discordrpc := discordrpc

discordrpc_root := $(source)/$(discordrpc)
discordrpc_src := $(discordrpc_root)/src
discordrpc_inc := $(discordrpc_root)/include
discordrpc_obj := $(obj)/$(discordrpc)

discordrpc_objs := \
    discord_rpc.cpp \
    rpc_connection.cpp \
    serialization.cpp \

ifeq ($(PLATFORM),WINDOWS)
discordrpc_objs+=connection_win.cpp discord_register_win.cpp
else
discordrpc_objs+=connection_unix.cpp discord_register_linux.cpp
endif

discordrpc_cflags=-g -I$(discordrpc_inc) -Wno-class-memaccess

#### libxmp-lite

libxmplite := libxmp-lite

libxmplite_root := $(source)/$(libxmplite)
libxmplite_src := $(libxmplite_root)/src
libxmplite_inc := $(libxmplite_root)/include
libxmplite_obj := $(obj)/$(libxmplite)

libxmplite_excl :=
libxmplite_objs := $(call getfiltered,libxmplite,*.c)

libxmplite_cflags := -DHAVE_ROUND -DLIBXMP_CORE_PLAYER -DLIBXMP_NO_PROWIZARD -DLIBXMP_NO_DEPACKERS -DBUILDING_STATIC -I$(libxmplite_inc)/libxmp-lite -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -Wno-cast-qual


#### PhysicsFS

physfs := physfs

physfs_root := $(source)/$(physfs)
physfs_src := $(physfs_root)/src
physfs_inc := $(physfs_root)/include
physfs_obj := $(obj)/$(physfs)

physfs_excl :=

ifeq ($(PLATFORM),WINDOWS)
    physfs_excl += physfs_platform_unix.c
else
    physfs_excl += physfs_platform_windows.c
endif

ifneq (0,$(USE_PHYSFS))
    physfs_objs := $(call getfiltered,physfs,*.c)
else
    physfs_objs :=
endif

ifeq ($(PLATFORM),APPLE)
    physfs_objs += physfs_platform_apple.m
endif

physfs_cflags :=


#### glad

glad := glad

glad_root := $(source)/$(glad)
glad_src := $(glad_root)/src
glad_inc := $(glad_root)/include
glad_obj := $(obj)/$(glad)

glad_excl :=

ifneq ($(RENDERTYPE),WIN)
    glad_excl += glad_wgl.c
endif

ifneq (0,$(USE_OPENGL))
    glad_objs := $(call getfiltered,glad,*.c)
else
    glad_objs :=
endif

glad_cflags :=

#### quickjs (NNBuild JS scripting engine, quickjs-ng)

quickjs := quickjs

quickjs_root := $(source)/$(quickjs)
quickjs_src := $(quickjs_root)
quickjs_inc := $(quickjs_root)
quickjs_obj := $(obj)/$(quickjs)

quickjs_objs := $(call getfiltered,quickjs,*.c)

# Vendored C11 code. This tree compiles .c as C++ (COMPILER_C=COMPILER_CXX), but
# QuickJS is C-only, so force real C mode (-x c overrides the earlier -x c++),
# pin C11, and silence its warnings (it is not our code).
quickjs_cflags := -x c -std=gnu11 -w -DNDEBUG


#### mimalloc

mimalloc := mimalloc

mimalloc_root := $(source)/$(mimalloc)
mimalloc_src := $(mimalloc_root)/src
mimalloc_inc := $(mimalloc_root)/include
mimalloc_obj := $(obj)/$(mimalloc)

mimalloc_excl := \
    alloc-override.c \
    arena-abandon.c \
    free.c \
    page-queue.c \
    static.c \

mimalloc_objs := $(call getfiltered,mimalloc,*.c)
mimalloc_objs += prim/prim.c

mimalloc_cflags := -D_WIN32_WINNT=0x0600 -DMI_USE_RTLGENRANDOM -DMI_SHOW_ERRORS -fexceptions -Wno-cast-qual -Wno-unknown-pragmas -Wno-array-bounds -Wno-null-dereference -Wno-missing-field-initializers

ifeq (,$(filter 1 2 3 4 5 6 7,$(GCC_MAJOR)))
    mimalloc_cflags += -Wno-class-memaccess
endif


#### imgui

imgui := imgui

imgui_root := $(source)/$(imgui)
imgui_src := $(imgui_root)/src
imgui_inc := $(imgui_root)/include
imgui_obj := $(obj)/$(imgui)

imgui_excl :=

ifneq ($(RENDERTYPE),SDL)
    imgui_excl += imgui_impl_sdl2.cpp
endif
ifneq ($(RENDERTYPE),WIN)
    imgui_excl += imgui_impl_win32.cpp
endif

imgui_objs := $(call getfiltered,imgui,*.cpp)

imgui_cflags := -I$(imgui_inc) -Wno-cast-qual -Wno-cast-function-type -Wno-null-dereference -Wno-stringop-overflow


#### Voidwrap

voidwrap := voidwrap

voidwrap_root := $(source)/$(voidwrap)
voidwrap_src := $(voidwrap_root)/src
voidwrap_inc := $(voidwrap_root)/include
voidwrap_obj := $(obj)/$(voidwrap)

voidwrap_excl :=

ifneq ($(PLATFORM),WINDOWS)
    voidwrap_excl += dllmain.cpp
endif

voidwrap_objs := $(call getfiltered,voidwrap,*.cpp)

ifeq ($(IMPLICIT_ARCH),x86_64)
    ifeq ($(PLATFORM),WINDOWS)
        voidwrap_lib := voidwrap_steam_x64.dll
        steamworks_lib := win64/steam_api64.dll
    else
        voidwrap_lib := libvoidwrap_steam.so
        steamworks_lib := linux64/libsteam_api.so
    endif
else
    ifeq ($(PLATFORM),WINDOWS)
        voidwrap_lib := voidwrap_steam_x86.dll
        steamworks_lib := steam_api.dll
    else
        voidwrap_lib := libvoidwrap_steam.so
        steamworks_lib := linux32/libsteam_api.so
    endif
endif

voidwrap_cflags := -I$(voidwrap_root)/sdk/public/steam -fPIC -fvisibility=hidden -Wno-invalid-offsetof


##### Component Definitions

#### EBacktrace

ifndef ebacktrace_dll
    ebacktrace_dll := ebacktrace1.dll
    ifeq ($(findstring x86_64,$(COMPILERTARGET)),x86_64)
        ebacktrace_dll := ebacktrace1-64.dll
    endif
endif


#### BUILD Engine

engine := build

engine_root := $(source)/$(engine)
engine_src := $(engine_root)/src
engine_inc := $(engine_root)/include
engine_obj := $(obj)/$(engine)

engine_cflags :=

engine_deps :=

ifneq (1,$(SDL_TARGET))
    engine_deps += imgui
endif

ifneq (0,$(USE_PHYSFS))
    engine_deps += physfs
endif

ifneq (0,$(USE_MIMALLOC))
    engine_deps += mimalloc
endif

engine_editor_objs := \
    build.cpp \
    config.cpp \

engine_tools_objs := \
    colmatch.cpp \
    compat.cpp \
    crc32.cpp \
    klzw.cpp \
    kplib.cpp \
    loguru.cpp \
    lz4.cpp \
    pragmas.cpp \
    smmalloc.cpp \
    smmalloc_generic.cpp \
    smmalloc_tls.cpp \
    vfs.cpp \

engine_excl := \
    a-c.cpp \
    sdlayer12.cpp \
    sdlkeytrans.cpp \
    startgtk.editor.cpp \
    startwin.editor.cpp \
    $(engine_editor_objs) \

ifeq (1,$(USE_OPENGL))
    engine_deps += glad
else
    engine_excl += \
        glbuild.cpp \
        glsurface.cpp \
        mdsprite.cpp \
        polymer.cpp \
        polymost.cpp \
        tilepacker.cpp \
        voxmodel.cpp \

endif

ifeq ($(PLATFORM),WINDOWS)
    ifeq ($(STARTUP_WINDOW),1)
        engine_editor_objs += startwin.editor.cpp
    endif
else
    engine_excl += winbits.cpp
endif

ifeq ($(PLATFORM),WII)
    LINKERFLAGS += -Wl,-wrap,c_default_exceptionhandler
else
    engine_excl += wiibits.cpp
endif

ifneq ($(RENDERTYPE),SDL)
    engine_excl += sdlayer.cpp
endif
ifneq ($(RENDERTYPE),WIN)
    engine_excl += winlayer.cpp rawinput.cpp
endif

ifeq (1,$(HAVE_GTK2))
    ifeq ($(STARTUP_WINDOW),1)
        engine_editor_objs += startgtk.editor.cpp
    endif
else
    engine_excl += gtkbits.cpp dynamicgtk.cpp
endif

ifeq ($(USE_LIBVPX),0)
    engine_excl += animvpx.cpp
endif

engine_objs := \
    $(call getfiltered,engine,*.c) \
    $(call getfiltered,engine,*.cpp) \
    polymost1Frag.glsl \
    polymost1Vert.glsl \

ifeq (0,$(NOASM))
  engine_objs += a.nasm
else
  engine_objs += a-c.cpp
endif

ifeq ($(PLATFORM),DARWIN)
    engine_objs += osxbits.mm
    engine_tools_objs += osxbits.mm
    ifeq ($(STARTUP_WINDOW),1)
        engine_editor_objs += startosx.editor.mm
    endif
    ifeq ($(SDL_TARGET),1)
        ifneq ($(SDL_FRAMEWORK),0)
            engine_objs += SDLMain.mm
        endif
    endif
endif


#### mact

mact := mact

mact_root := $(source)/$(mact)
mact_src := $(mact_root)/src
mact_inc := $(mact_root)/include
mact_obj := $(obj)/$(mact)

mact_excl :=
mact_objs := $(call getfiltered,mact,*.cpp)

mact_cflags :=

#### AudioLib

audiolib := audiolib

audiolib_root := $(source)/$(audiolib)
audiolib_src := $(audiolib_root)/src
audiolib_inc := $(audiolib_root)/include
audiolib_obj := $(obj)/$(audiolib)

audiolib_deps :=

audiolib_excl := \
    music_external.cpp \

ifneq ($(PLATFORM),WINDOWS)
    audiolib_excl += driver_directsound.cpp driver_winmm.cpp
endif
ifneq ($(SUBPLATFORM),LINUX)
    audiolib_excl += driver_alsa.cpp
endif
ifneq ($(RENDERTYPE),SDL)
    audiolib_excl += driver_sdl.cpp
endif

audiolib_objs := $(call getfiltered,audiolib,*.cpp)

audiolib_cflags :=

ifneq (0,$(HAVE_XMP))
    audiolib_cflags += -I$(libxmplite_inc)
    audiolib_deps += libxmplite
endif


#### Tools

tools := tools

tools_objs := \
    compat_tools.cpp \

tools_root := $(source)/$(tools)
tools_src := $(tools_root)/src
tools_obj := $(obj)/$(tools)

tools_cflags := $(engine_cflags) -I$(engine_src)

tools_deps := engine_tools

ifneq (0,$(USE_MIMALLOC))
    tools_deps += mimalloc
endif

tools_targets := \
    arttool \
    bsuite \
    cacheinfo \
    generateicon \
    givedepth \
    ivfrate \
    kextract \
    kgroup \
    kmd2tool \
    map2stl \
    md2tool \
    mkpalette \
    transpal \
    unpackssi \
    wad2art \
    wad2map \

ifeq ($(PLATFORM),WINDOWS)
    tools_targets += enumdisplay getdxdidf
endif
ifeq ($(RENDERTYPE),SDL)
    tools_targets += makesdlkeytrans
endif


#### KenBuild (Test Game)

kenbuild := kenbuild

kenbuild_root := $(source)/$(kenbuild)
kenbuild_src := $(kenbuild_root)/src
kenbuild_rsrc := $(kenbuild_root)/rsrc
kenbuild_obj := $(obj)/$(kenbuild)

kenbuild_cflags := -I$(kenbuild_src)

kenbuild_game := ekenbuild
kenbuild_editor := ekenbuild-editor

kenbuild_game_deps := audiolib

kenbuild_game_proper := EKenBuild
kenbuild_editor_proper := EKenBuild-Editor

kenbuild_game_objs := \
    common.cpp \
    config.cpp \
    kdmeng.cpp \
    game.cpp \

kenbuild_editor_objs := \
    bstub.cpp \
    common.cpp \

kenbuild_game_rsrc_objs :=
kenbuild_editor_rsrc_objs :=
kenbuild_game_gen_objs :=
kenbuild_editor_rsrc_objs :=

ifeq (11,$(HAVE_GTK2)$(STARTUP_WINDOW))
    kenbuild_game_objs += startgtk.game.cpp
    kenbuild_game_gen_objs += game_banner.c
    kenbuild_editor_gen_objs += build_banner.c
endif
ifeq ($(RENDERTYPE),SDL)
    kenbuild_game_rsrc_objs += game_icon.c
    kenbuild_editor_rsrc_objs += build_icon.c
endif
ifeq ($(PLATFORM),WINDOWS)
    kenbuild_game_objs += startwin.game.cpp
    kenbuild_game_rsrc_objs += gameres.rc
    kenbuild_editor_rsrc_objs += buildres.rc
endif

ifeq ($(PLATFORM),DARWIN)
    ifeq ($(STARTUP_WINDOW),1)
        kenbuild_game_objs += StartupWinController.game.mm
    endif
endif


#### Duke Nukem 3D

duke3d := duke3d

duke3d_game_ldflags :=
duke3d_editor_ldflags :=

duke3d_game_stripflags :=
duke3d_editor_stripflags :=

duke3d_root := $(source)/$(duke3d)
duke3d_src := $(duke3d_root)/src
duke3d_rsrc := $(duke3d_root)/rsrc
duke3d_obj := $(obj)/$(duke3d)

ifneq (,$(APPBASENAME))
    ifeq ($(PLATFORM),WINDOWS)
        duke3d_rsrc := $(duke3d_root)/rsrc/$(APPBASENAME)
    endif
    duke3d_obj := $(obj)/$(APPBASENAME)
endif

duke3d_cflags :=

common_editor_deps := duke3d_common_editor engine_editor

duke3d_game_deps := audiolib mact
duke3d_editor_deps := audiolib

duke3d_game := eduke32
duke3d_editor := mapster32

ifneq (,$(APPBASENAME))
    duke3d_game := $(APPBASENAME)
endif

duke3d_game_proper := EDuke32
duke3d_editor_proper := Mapster32

duke3d_common_editor_objs := \
    m32common.cpp \
    m32def.cpp \
    m32exec.cpp \
    m32vars.cpp \

duke3d_editor_objs := \
    astub.cpp \
    common.cpp \
    grpscan.cpp \
    sounds_mapster32.cpp \

duke3d_excl := \
    in_android.cpp \
    m32structures.cpp \
    mdump.cpp \
    startgtk.game.cpp \
    startwin.game.cpp \
    $(duke3d_common_editor_objs) \
    $(duke3d_editor_objs) \

duke3d_game_objs := $(call getfiltered,duke3d,*.cpp) \
    common.cpp \
    grpscan.cpp \

duke3d_game_rsrc_objs :=
duke3d_editor_rsrc_objs :=
duke3d_game_gen_objs :=
duke3d_editor_gen_objs :=

duke3d_game_miscdeps :=
duke3d_editor_miscdeps :=
duke3d_game_orderonlydeps :=
duke3d_editor_orderonlydeps :=

# Only link FLAC when it is enabled, so HAVE_FLAC=0 builds do not need the library.
# FLAC_LIBS can be overridden, for example with "-l:libFLAC.a -l:libogg.a" to
# link it statically for a binary that runs on other distributions.
FLAC_LIBS ?= -lFLAC
ifeq ($(SUBPLATFORM),LINUX)
    LIBS += -lasound
    ifneq (0,$(HAVE_FLAC))
        LIBS += $(FLAC_LIBS)
    endif
endif

ifeq ($(PLATFORM),BSD)
    LIBS += -lexecinfo
    ifneq (0,$(HAVE_FLAC))
        LIBS += $(FLAC_LIBS)
    endif
endif

# Ogg Theora, for the PowerSlave remaster's movies (source/exhumed/src/
# kexmovie.cpp). Optional: without it the intro falls back to book.mov.
THEORA_CFLAGS := $(shell $(PKG_CONFIG) --cflags theoradec ogg 2>/dev/null)
THEORA_LIBS := $(shell $(PKG_CONFIG) --libs theoradec ogg 2>/dev/null)
ifneq ($(THEORA_LIBS),)
    COMPILERFLAGS += -DHAVE_THEORA $(THEORA_CFLAGS)
    LIBS += $(THEORA_LIBS)
endif

ifeq ($(PLATFORM),DARWIN)
    LIBS += -lFLAC \
            -lcurl \
            -Wl,-framework,Cocoa -Wl,-framework,Carbon \
            -Wl,-framework,CoreMIDI -Wl,-framework,AudioUnit \
            -Wl,-framework,AudioToolbox -Wl,-framework,IOKit
    ifneq (00,$(DARWIN9)$(DARWIN10))
        LIBS += -Wl,-framework,QuickTime
    endif

    ifeq ($(STARTUP_WINDOW),1)
        duke3d_game_objs += GrpFile.game.mm GameListSource.game.mm startosx.game.mm
    endif
endif

ifeq ($(PLATFORM),WINDOWS)
    ifneq (0,$(HAVE_FLAC))
        LIBS += -lFLAC
    endif
    LIBS += -ldsound
    duke3d_game_objs += winbits.cpp
    duke3d_game_rsrc_objs += gameres.rc
    duke3d_editor_rsrc_objs += buildres.rc
    ifeq ($(STARTUP_WINDOW),1)
        duke3d_game_objs += startwin.game.cpp
    endif
endif

ifeq (11,$(HAVE_GTK2)$(STARTUP_WINDOW))
    duke3d_game_objs += startgtk.game.cpp
    duke3d_game_gen_objs += game_banner.c
    duke3d_editor_gen_objs += build_banner.c
endif
ifeq ($(RENDERTYPE),SDL)
    duke3d_game_rsrc_objs += game_icon.c
    duke3d_editor_rsrc_objs += build_icon.c
endif


#### Shadow Warrior

sw := sw

sw_root := $(source)/$(sw)
sw_src := $(sw_root)/src
sw_rsrc := $(sw_root)/rsrc
sw_obj := $(obj)/$(sw)

sw_cflags := -I$(source)/glue

sw_game_deps := audiolib mact
sw_editor_deps := audiolib

sw_game := voidsw
sw_editor := wangulator

sw_game_proper := VoidSW
sw_editor_proper := Wangulator

sw_editor_objs := \
    bldscript.cpp \
    brooms.cpp \
    colormap.cpp \
    common.cpp \
    grpscan.cpp \
    jbhlp.cpp \
    jnstub.cpp \

sw_excl := \
    startgtk.game.cpp \
    startwin.game.cpp \
    $(sw_editor_objs) \

sw_game_objs := $(call getfiltered,sw,*.cpp) \
    colormap.cpp \
    common.cpp \
    grpscan.cpp \

sw_game_rsrc_objs :=
sw_editor_rsrc_objs :=
sw_game_gen_objs :=
sw_editor_gen_objs :=

ifeq (11,$(HAVE_GTK2)$(STARTUP_WINDOW))
    sw_game_objs += startgtk.game.cpp
    sw_game_gen_objs += game_banner.c
    sw_editor_gen_objs += build_banner.c
endif
ifeq ($(RENDERTYPE),SDL)
    sw_game_rsrc_objs += game_icon.c
    sw_editor_rsrc_objs += game_icon.c
endif
ifeq ($(PLATFORM),WINDOWS)
    sw_game_objs += startwin.game.cpp
    sw_game_rsrc_objs += gameres.rc
    sw_editor_rsrc_objs += buildres.rc
endif
ifeq ($(PLATFORM),DARWIN)
    ifeq ($(STARTUP_WINDOW),1)
        sw_game_objs += GrpFile.game.mm GameListSource.game.mm StartupWinController.game.mm
    endif
endif

#### NetDuke32

nnbuild := nnbuild

nnbuild_game_ldflags :=

nnbuild_game_stripflags :=

nnbuild_root := $(source)/$(nnbuild)
nnbuild_src := $(nnbuild_root)/src
nnbuild_rsrc := $(nnbuild_root)/rsrc
nnbuild_obj := $(obj)/$(nnbuild)

nnbuild_cflags := -I$(nnbuild_src) -I$(quickjs_inc) -I$(source)/glue -DNNBJS

nnbuild_game_deps := audiolib mact quickjs

ifeq ($(USE_DISCORDRPC), 1)
    nnbuild_game_deps += discordrpc
    nnbuild_cflags += -DDISCORD_RPC
endif

nnbuild_game := nnbuild

ifneq (,$(APPBASENAME))
    nnbuild_game := $(APPBASENAME)
endif

nnbuild_game_proper := NNBuild

nnbuild_game_objs := \
    gameselect.cpp \
    actors.cpp \
    anim.cpp \
    chatpipe.cpp \
    cmdline.cpp \
    config.cpp \
    common.cpp \
    crossplay_dukeai.cpp \
    discord.cpp \
    dnames.cpp \
    dukebot.cpp \
    editorboard.cpp \
    editorlive.cpp \
    editortools.cpp \
    editormcp.cpp \
    editorproject.cpp \
    editorprefab.cpp \
    game.cpp \
    gamedef.cpp \
    gameexec.cpp \
    gamestructures.cpp \
    gametypes.cpp \
    gamevars.cpp \
    global.cpp \
    grpscan.cpp \
    hud.cpp \
    mapeditor.cpp \
    nativeeditor.cpp \
    masterserver.cpp \
    menus.cpp \
    netcontent.cpp \
    net_predict.cpp \
    oldnet.cpp \
    osdcmds.cpp \
    osdfuncs.cpp \
    player.cpp \
    premap.cpp \
    projectlaunch.cpp \
    projectiles.cpp \
    rts.cpp \
    savegame.cpp \
    screens.cpp \
    sector.cpp \
    serverlevel.cpp \
    sounds.cpp \
    sync.cpp \
    utils.cpp \
    nnbjs.cpp \
    nnbjs_ts.cpp \
    nnbjs_duke.cpp \

nnbuild_game_rsrc_objs :=
nnbuild_game_gen_objs :=

nnbuild_game_miscdeps :=
nnbuild_game_orderonlydeps :=

ifeq (SDL2,$(RENDERTYPE)$(SDL_TARGET))
ifneq (,$(filter $(PLATFORM),LINUX WINDOWS DARWIN BSD))
    nnbuild_cflags += -DNNBUILD_NATIVE_EDITOR
    nnbuild_native_sources := $(addprefix $(engine_src)/,build.cpp config.cpp) \
        $(addprefix $(duke3d_src)/,$(duke3d_editor_objs) $(duke3d_common_editor_objs))
    nnbuild_game_miscdeps += $(addprefix $(nnbuild_obj)/native/,$(notdir $(nnbuild_native_sources:.cpp=.$o)))
endif
endif

ifeq ($(PLATFORM),DARWIN)
    ifeq ($(STARTUP_WINDOW),1)
        nnbuild_game_objs += GrpFile.game.mm GameListSource.game.mm startosx.game.mm
    endif
endif

ifeq ($(PLATFORM),WINDOWS)
    nnbuild_game_objs += winbits.cpp
    nnbuild_game_rsrc_objs += gameres.rc
    ifeq ($(STARTUP_WINDOW),1)
        nnbuild_game_objs += startwin.game.cpp
    endif
endif

ifeq (11,$(HAVE_GTK2)$(STARTUP_WINDOW))
    nnbuild_game_objs += startgtk.game.cpp
    nnbuild_game_gen_objs += game_banner.c
endif
ifeq ($(RENDERTYPE),SDL)
    nnbuild_game_rsrc_objs += game_icon.c
endif

#### Includes

COMPILERFLAGS += \
    -MP -MMD \
    -I$(engine_inc) \
    -I$(mact_inc) \
    -I$(audiolib_inc) \
    -I$(glad_inc) \
    -I$(voidwrap_inc) \
    -I$(imgui_inc) \
    -I$(discordrpc_inc) \

ifneq (0,$(USE_MIMALLOC))
    COMPILERFLAGS += -I$(mimalloc_inc)
endif

ifneq (0,$(USE_PHYSFS))
    COMPILERFLAGS += -I$(physfs_inc) -DUSE_PHYSFS
endif

ifneq (0,$(MICROPROFILE))
  COMPILERFLAGS += -DMICROPROFILE_ENABLED=1
endif

##### Recipes

games := \
    duke3d \
    kenbuild \
    sw \
    nnbuild \

libraries := \
    audiolib \
    engine \
    glad \
    imgui \
    libxmplite \
    mact \
    voidwrap \
    discordrpc \
    quickjs \

ifneq (0,$(USE_MIMALLOC))
    libraries += mimalloc
endif

ifneq (0,$(USE_PHYSFS))
    libraries += physfs
endif

components := \
    $(games) \
    $(libraries) \
    tools \

roles := \
    game \

# If blank string, substring not found.
ifeq (,$(findstring nnbuild,$(MAKECMDGOALS)))
    roles += editor
endif

ifeq ($(PRETTY_OUTPUT),1)
.SILENT:
endif
.PHONY: \
    $(addprefix clean,$(games) test utils tools) \
    $(engine_obj)/rev.$o \
    all \
    clang-tools \
    clean \
    printtools \
    printutils \
    rev \
    start \
    veryclean \

.SUFFIXES:
.SECONDEXPANSION:


#### Targets

all: duke3d

start:
	$(BUILD_STARTED)

tools: $(addsuffix $(EXESUFFIX),$(tools_targets)) | start
	@$(call LL,$^)

$(games): $$(foreach i,$(roles),$$($$@_$$i)$(EXESUFFIX)) | start
	@$(call LL,$^)

ebacktrace: $(ebacktrace_dll) | start
	@$(call LL,$^)

voidwrap: $(voidwrap_lib) | start
	@$(call LL,$^)

ifeq ($(PLATFORM),WII)
ifneq ($(ELF2DOL),)
%$(DOLSUFFIX): %$(EXESUFFIX)
endif
endif

# ---- WITH_SW: link Shadow Warrior (VoidSW) into the nnbuild binary --------
# The SW game objects are prelinked (ld -r) into one relocatable object with
# their game-internal symbols that also exist in Duke localized, so the two game
# modules no longer collide. SW's own startup window is dropped (Duke's serves
# both). See source/sw/UNIFY_WITH_NNBUILD.md. macOS (arm64) for now.
# $(call MERGE_RULE,<game>,<prior-merge-objs>): prelink <game>'s game objects
# ($(<game>merge_objs)) with every game-internal symbol that also exists in one of the
# already-linked games (<prior-merge-objs>) localized, so the coexisting copies do not
# collide in the combined binary. macOS/arm64: objcopy is unavailable, so this uses
# ld -r + -unexported_symbols_list. Instantiated once per extra game below.
MERGE_NM := $(CROSS)nm$(CROSS_SUFFIX)
MERGE_LD := $(CROSS)ld$(CROSS_SUFFIX)
MERGE_OBJCOPY := $(CROSS)objcopy$(CROSS_SUFFIX)

ifeq ($(PLATFORM),DARWIN)
# macOS: objcopy is unavailable, so this uses ld -r + -unexported_symbols_list.
MERGE_ARCH := $(shell $(CC) -dumpmachine | sed -e 's/-.*//' -e 's/aarch64/arm64/')
ifneq (,$(filter -arch,$(ARCH)))
    MERGE_ARCH := $(word 2,$(ARCH))
endif
define MERGE_RULE
$$(obj)/$(1)_merged.o: $$($(1)merge_objs) $(2)
	@echo "Merging $(1) objects (localizing game-internal collisions with prior games)..."
	@nm -g $(2) 2>/dev/null | awk '$$$$2 ~ /^[TDBS]$$$$/{print $$$$3}' | sort -u > $$(obj)/$(1)_prior_strong.txt
	@nm -g $$($(1)merge_objs) 2>/dev/null | awk '$$$$2 ~ /^[TDBS]$$$$/{print $$$$3}' | sort -u > $$(obj)/$(1)_strong.txt
	@comm -12 $$(obj)/$(1)_prior_strong.txt $$(obj)/$(1)_strong.txt > $$(obj)/$(1)_hide.txt
	ld -r -arch $(MERGE_ARCH) $$($(1)merge_objs) -o $$(obj)/$(1)_premerge.o
	ld -r -arch $(MERGE_ARCH) $$(obj)/$(1)_premerge.o -o $$@ -unexported_symbols_list $$(obj)/$(1)_hide.txt
endef
else
# GNU toolchains (Linux, BSD, MinGW): relocatable link, then objcopy turns every
# external definition that a prior game also defines into a local symbol.
ifeq ($(PLATFORM),WINDOWS)
# PE/COFF keeps virtual tables and (on x64) ".refptr" pointer stubs in link-once
# sections that the final link de-duplicates by section name. A symbol made
# local to one game must not lose its section to another game's same-named
# copy, so those sections are renamed first; see platform/pe-private-sections.sh.
MERGE_OBJDUMP := $(CROSS)objdump$(CROSS_SUFFIX)
define MERGE_RULE
$$(obj)/$(1)_merged.o: $$($(1)merge_objs) $(2) platform/pe-private-sections.sh
	@echo "Merging $(1) objects (localizing game-internal collisions with prior games)..."
	@$(MERGE_NM) -g --defined-only $(2) 2>/dev/null | awk 'NF == 3 {print $$$$3}' | sort -u > $$(obj)/$(1)_prior_strong.txt
	@$(MERGE_NM) -g --defined-only $$($(1)merge_objs) 2>/dev/null | awk 'NF == 3 {print $$$$3}' | sort -u > $$(obj)/$(1)_strong.txt
	@comm -12 $$(obj)/$(1)_prior_strong.txt $$(obj)/$(1)_strong.txt > $$(obj)/$(1)_hide.txt
	$(MERGE_LD) -r $$($(1)merge_objs) -o $$(obj)/$(1)_premerge.o
	sh platform/pe-private-sections.sh $(MERGE_OBJDUMP) $(1) $$(obj)/$(1)_hide.txt $$(obj)/$(1)_premerge.o > $$(obj)/$(1)_rename.txt
	$(MERGE_OBJCOPY) @$$(obj)/$(1)_rename.txt --localize-symbols=$$(obj)/$(1)_hide.txt $$(obj)/$(1)_premerge.o $$@
endef
else
# ELF: section groups are dissolved first so that an inline function or virtual
# table belonging to this game is not discarded in favour of a same-named one
# from another game. (Common.mak passes -fno-gnu-unique for this target, since
# objcopy cannot make a GNU "unique" symbol local.)
define MERGE_RULE
$$(obj)/$(1)_merged.o: $$($(1)merge_objs) $(2)
	@echo "Merging $(1) objects (localizing game-internal collisions with prior games)..."
	@$(MERGE_NM) -g --defined-only $(2) 2>/dev/null | awk 'NF == 3 {print $$$$3}' | sort -u > $$(obj)/$(1)_prior_strong.txt
	@$(MERGE_NM) -g --defined-only $$($(1)merge_objs) 2>/dev/null | awk 'NF == 3 {print $$$$3}' | sort -u > $$(obj)/$(1)_strong.txt
	@comm -12 $$(obj)/$(1)_prior_strong.txt $$(obj)/$(1)_strong.txt > $$(obj)/$(1)_hide.txt
	$(MERGE_LD) -r --force-group-allocation $$($(1)merge_objs) -o $$(obj)/$(1)_premerge.o
	$(MERGE_OBJCOPY) --localize-symbols=$$(obj)/$(1)_hide.txt $$(obj)/$(1)_premerge.o $$@
endef
endif
endif

ifeq ($(WITH_SW),1)
nnbuild_cflags += -DWITH_SW
swmerge_objs := $(filter-out %GameListSource.game.$o %GrpFile.game.$o %StartupWinController.game.$o %startosx.game.$o %startwin.game.$o %startgtk.game.$o,$(addprefix $(sw_obj)/,$(addsuffix .$o,$(basename $(sw_game_objs)))))
dukemerge_objs := $(addprefix $(nnbuild_obj)/,$(addsuffix .$o,$(basename $(nnbuild_game_objs))))

$(eval $(call MERGE_RULE,sw,$(dukemerge_objs)))

nnbuild_game_miscdeps += $(obj)/sw_merged.o
endif

# ---- WITH_BLOOD: link Blood (from NotBlood) into the nnbuild binary -------
# Same pattern as WITH_SW. Blood's game objects are prelinked (ld -r) with the
# game-internal symbols that also exist in Duke or SW localized. Blood's own
# Blood uses its native enet netcode (enet is built into our engine: build/src/enet.cpp).
# Excludes: mapedit (editor), credits (smacker cutscenes), nnext* (modern-map
# conditions), and Blood's startup window (Duke's serves all). Build with
# WITH_SW=1 WITH_BLOOD=1. See source/blood/BLOOD_UNIFY.md. macOS (arm64) for now.
ifeq ($(WITH_BLOOD),1)
nnbuild_cflags += -DWITH_BLOOD
blood := blood
blood_root := $(source)/blood
blood_src := $(blood_root)/src
blood_rsrc := $(blood_root)/rsrc
blood_obj := $(obj)/blood
blood_cflags := -I$(source)/glue -I$(blood_src) -Wno-c++11-narrowing -Wno-narrowing
blood_game_deps := audiolib mact
blood_game_objs := \
    blood.cpp actor.cpp ai.cpp aibat.cpp aibeast.cpp aiboneel.cpp aiburn.cpp \
    aicaleb.cpp aicerber.cpp aicult.cpp aigarg.cpp aighost.cpp aigilbst.cpp \
    aihand.cpp aihound.cpp aiinnoc.cpp aipod.cpp airat.cpp aispid.cpp \
    aitchern.cpp aizomba.cpp aizombf.cpp asound.cpp barf.cpp callback.cpp \
    chatpipe.cpp choke.cpp common.cpp config.cpp controls.cpp db.cpp demo.cpp \
    dude.cpp endgame.cpp eventq.cpp fire.cpp fx.cpp gamemenu.cpp gameutil.cpp \
    getopt.cpp gfx.cpp gib.cpp globals.cpp gui.cpp inifile.cpp iob.cpp \
    levels.cpp loadsave.cpp map2d.cpp menu.cpp messages.cpp mirrors.cpp \
    misc.cpp network.cpp osdcmd.cpp player.cpp qav.cpp qheap.cpp replace.cpp \
    resource.cpp screen.cpp sectorfx.cpp seq.cpp sfx.cpp sound.cpp tile.cpp \
    trig.cpp triggers.cpp view.cpp warp.cpp weapon.cpp weather.cpp \
    nnb_compat.cpp \

components += blood
dukemerge_objs := $(addprefix $(nnbuild_obj)/,$(addsuffix .$o,$(basename $(nnbuild_game_objs))))
bloodmerge_objs := $(addprefix $(blood_obj)/,$(addsuffix .$o,$(basename $(blood_game_objs))))

$(eval $(call MERGE_RULE,blood,$(dukemerge_objs) $(swmerge_objs)))

nnbuild_game_miscdeps += $(obj)/blood_merged.o
endif

# ---- WITH_RR: link Redneck Rampage (from Rednukem/NBlood) into nnbuild ----
# RR is a Duke-engine variant (eduke32 lineage, like our Duke code), so it compiles
# against our engine cleanly; it collides heavily with our Duke code, so the ld -r
# merge localizes ALL of the game-internal collisions (vs Duke/SW/Blood). Excludes
# the Mapster32 editor (astub/m32*/sounds_mapster32), platform-only files
# (in_android/mdump/winbits) and the startup window (Duke's serves all). Build with
# WITH_SW=1 WITH_BLOOD=1 WITH_RR=1. See source/rr/RR_UNIFY.md. macOS (arm64) for now.
ifeq ($(WITH_RR),1)
nnbuild_cflags += -DWITH_RR
rr := rr
rr_root := $(source)/rr
rr_src := $(rr_root)/src
rr_obj := $(obj)/rr
rr_cflags := -I$(source)/glue -I$(rr_src) -Wno-c++11-narrowing -Wno-narrowing
rr_game_deps := audiolib mact
rr_game_objs := \
    actors.cpp anim.cpp cheats.cpp cmdline.cpp common.cpp config.cpp demo.cpp \
    filestream.cpp game.cpp gamedef.cpp gameexec.cpp gamestructures.cpp gamevars.cpp \
    global.cpp grpscan.cpp input.cpp menus.cpp namesdyn.cpp net.cpp osdcmds.cpp \
    osdfuncs.cpp player.cpp playmve.cpp premap.cpp rrdh.cpp rts.cpp savegame.cpp \
    sbar.cpp screens.cpp screentext.cpp sector.cpp sounds.cpp soundsdyn.cpp \
    n64/reality_stubs.cpp \

components += rr
dukemerge_objs := $(addprefix $(nnbuild_obj)/,$(addsuffix .$o,$(basename $(nnbuild_game_objs))))
rrmerge_objs := $(addprefix $(rr_obj)/,$(addsuffix .$o,$(basename $(rr_game_objs))))

$(eval $(call MERGE_RULE,rr,$(dukemerge_objs) $(swmerge_objs) $(bloodmerge_objs)))

nnbuild_game_miscdeps += $(obj)/rr_merged.o
endif

# ---- WITH_EXHUMED: link PCExhumed into nnbuild ---------------------------
# PCExhumed comes from NBlood's source/exhumed tree. It is a separate Build-game
# runtime, so it is prelinked exactly like SW/Blood/RR and its internal symbol
# collisions are localized against the games already linked into NNBuild.
# Startup-window objects are excluded; the shared launcher/selector owns startup.
ifeq ($(WITH_EXHUMED),1)
nnbuild_cflags += -DWITH_EXHUMED
exhumed := exhumed
exhumed_root := $(source)/exhumed
exhumed_src := $(exhumed_root)/src
exhumed_obj := $(obj)/exhumed
exhumed_cflags := -I$(source)/glue -I$(exhumed_src) -Wno-c++11-narrowing -Wno-narrowing
exhumed_game_objs := \
    aistuff.cpp anims.cpp anubis.cpp bubbles.cpp bullet.cpp cd.cpp \
    common.cpp config.cpp enginesubs.cpp exhumed.cpp exscript.cpp fish.cpp \
    grenade.cpp grpscan.cpp gun.cpp init.cpp input.cpp items.cpp lavadude.cpp \
    light.cpp lighting.cpp lion.cpp map.cpp memorystream.cpp menu.cpp mono.cpp \
    move.cpp movie.cpp mummy.cpp network.cpp object.cpp osdcmds.cpp player.cpp \
    queen.cpp ra.cpp ramses.cpp random.cpp rat.cpp record.cpp rex.cpp roach.cpp \
    runlist.cpp save.cpp scorp.cpp sequence.cpp serial.cpp set.cpp snake.cpp \
    sound.cpp spider.cpp status.cpp stream.cpp switch.cpp text2.cpp timer.cpp \
    trigdat.cpp nnbnet.cpp version.cpp view.cpp wasp.cpp kexmenu.cpp kexbreak.cpp kexmovie.cpp kexinv.cpp kexvlight.cpp kextactile.cpp \

components += exhumed
dukemerge_objs := $(addprefix $(nnbuild_obj)/,$(addsuffix .$o,$(basename $(nnbuild_game_objs))))
exhumedmerge_objs := $(addprefix $(exhumed_obj)/,$(addsuffix .$o,$(basename $(exhumed_game_objs))))

$(eval $(call MERGE_RULE,exhumed,$(dukemerge_objs) $(swmerge_objs) $(bloodmerge_objs) $(rrmerge_objs)))

nnbuild_game_miscdeps += $(obj)/exhumed_merged.o
endif


define BUILDRULE

$$($1_$2)$$(EXESUFFIX): $$(foreach i,$(call getdeps,$1,$2),$$(call expandobjs,$$i)) $$($1_$2_miscdeps) | $$($1_$2_orderonlydeps)
	$$(LINK_STATUS)
	$$(call MKDIR,"$$(obj)/$$($1_$2)_dump")
	$$(RECIPE_IF) $$(LINKER) $$(call LF,$$(obj)/$$($1_$2)_dump) -o $$@ $$^ $$(GUI_LIBS) $$($1_$2_ldflags) $$(LIBDIRS) $$(LIBS) $$(RECIPE_RESULT_LINK)
ifeq ($$(PLATFORM),WII)
ifneq ($$(ELF2DOL),)
	$$(ELF2DOL) $$@ $$($1_$2)$$(DOLSUFFIX)
endif
endif
ifneq ($$(STRIP),)
	$$(STRIP) $$@ $$($1_$2_stripflags)
endif
ifeq ($$(PLATFORM),DARWIN)
	cp -RPf "platform/Apple/bundles/$$($1_$2_proper).app" "./"
	$(call MKDIR,"$$($1_$2_proper).app/Contents/MacOS")
	cp -f "$$($1_$2)$$(EXESUFFIX)" "$$($1_$2_proper).app/Contents/MacOS/"
endif
	$$(call RMDIR,"$$(obj)/$$($1_$2)_dump")

endef

$(foreach i,$(games),$(foreach j,$(roles),$(eval $(call BUILDRULE,$i,$j))))


#### Rules

$(ebacktrace_dll): platform/Windows/src/backtrace.c
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(CC) $(CONLYFLAGS) -O2 -ggdb -shared -Wall -Wextra -static-libgcc -I$(engine_inc) -o $@ $^ -lbfd -liberty -limagehlp $(RECIPE_RESULT_COMPILE)

libklzw$(DLLSUFFIX): $(engine_src)/klzw.cpp
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER_C) -shared -fPIC $< -o $@ $(RECIPE_RESULT_COMPILE)

# to debug the tools link phase, make a copy of this rule explicitly replacing % with the name of a tool, such as kextract
%$(EXESUFFIX): $(tools_obj)/%.$o $(foreach i,tools $(tools_deps),$(call expandobjs,$i))
	$(LINK_STATUS)
	$(call MKDIR,"$(tools_obj)/$*")
	$(RECIPE_IF) $(LINKER) $(call LF,$(tools_obj)/$*) -o $@ $^ $(LIBDIRS) $(LIBS) $(RECIPE_RESULT_LINK)
ifneq ($(STRIP),)
	$(STRIP) $@
endif
	$(call RMDIR,"$(tools_obj)/$*")


### Voidwrap

$(voidwrap_lib): $(foreach i,$(voidwrap),$(call expandobjs,$i))
	$(LINK_STATUS)
	$(RECIPE_IF) $(LINKER) $(call LF,$(voidwrap_obj)) -shared -Wl,-soname,$@ -o $@ $^ $(LIBDIRS) $(voidwrap_root)/sdk/redistributable_bin/$(steamworks_lib) $(RECIPE_RESULT_LINK)
ifneq ($(STRIP),)
	$(STRIP) $@
endif


### Main Rules

define OBJECTRULES

include $(wildcard $($1_obj)/*.d)

$$($1_obj)/%.$$o: $$($1_src)/%.nasm | $$($1_obj)
	$$(COMPILE_STATUS)
	$$(RECIPE_IF) $$(AS) $$(ASFLAGS) $$< -o $$@ $$(RECIPE_RESULT_COMPILE)

$$($1_obj)/%.$$o: $$($1_src)/%.yasm | $$($1_obj)
	$$(COMPILE_STATUS)
	$$(RECIPE_IF) $$(AS) $$(ASFLAGS) $$< -o $$@ $$(RECIPE_RESULT_COMPILE)

$$($1_obj)/%.$$o: $$($1_src)/%.c | $$($1_obj)
	$$(COMPILE_STATUS)
	$$(call MKDIR,$$(dir $$@))
	$$(RECIPE_IF) $$(COMPILER_C) $$($1_cflags) -c $$< -o $$@ $$(RECIPE_RESULT_COMPILE)

$$($1_obj)/%.$$o: $$($1_src)/%.cpp | $$($1_obj)
	$$(COMPILE_STATUS)
	$$(call MKDIR,$$(dir $$@))
	$$(RECIPE_IF) $$(COMPILER_CXX) $$($1_cflags) -c $$< -o $$@ $$(RECIPE_RESULT_COMPILE)

$$($1_obj)/%.$$o: $$($1_src)/%.m | $$($1_obj)
	$$(COMPILE_STATUS)
	$$(call MKDIR,$$(dir $$@))
	$$(RECIPE_IF) $$(COMPILER_OBJC) $$($1_cflags) -c $$< -o $$@ $$(RECIPE_RESULT_COMPILE)

$$($1_obj)/%.$$o: $$($1_src)/%.mm | $$($1_obj)
	$$(COMPILE_STATUS)
	$$(call MKDIR,$$(dir $$@))
	$$(RECIPE_IF) $$(COMPILER_OBJCXX) $$($1_cflags) -c $$< -o $$@ $$(RECIPE_RESULT_COMPILE)

$$($1_obj)/%.$$o: $$($1_obj)/%.c | $$($1_obj)
	$$(COMPILE_STATUS)
	$$(call MKDIR,$$(dir $$@))
	$$(RECIPE_IF) $$(COMPILER_C) $$($1_cflags) -c $$< -o $$@ $$(RECIPE_RESULT_COMPILE)

$$($1_obj)/%.$$o: $$($1_src)/%.glsl | $$($1_obj)
	@echo Creating $$($1_obj)/$$(<F).cpp from $$<
	@$$(call RAW_ECHO,extern char const *$$(basename $$(<F));) > $$($1_obj)/$$(<F).cpp
	@$$(call RAW_ECHO,char const *$$(basename $$(<F)) = R"shader$$(paren_open)) >> $$($1_obj)/$$(<F).cpp
	@$$(call CAT,$$<) >> $$($1_obj)/$$(<F).cpp
	@$$(call RAW_ECHO,$$(paren_close)shader";) >> $$($1_obj)/$$(<F).cpp
	$$(COMPILE_STATUS)
	$$(call MKDIR,$$(dir $$@))
	$$(RECIPE_IF) $$(COMPILER_CXX) $$($1_cflags) -c $$($1_obj)/$$(<F).cpp -o $$@ $$(RECIPE_RESULT_COMPILE)

## Cosmetic stuff

$$($1_obj)/%.$$o: $$($1_rsrc)/%.rc | $$($1_obj)
	$$(COMPILE_STATUS)
	$$(call MKDIR,$$(dir $$@))
	$$(RECIPE_IF) $$(RC) -i $$< -o $$@ --include-dir=$$(engine_inc) --include-dir=$$($1_src) --include-dir=$$($1_rsrc) -DPOLYMER=$$(POLYMER) $(REVFLAG) $$(RECIPE_RESULT_COMPILE)

$$($1_obj)/%.$$o: $$($1_rsrc)/%.c | $$($1_obj)
	$$(COMPILE_STATUS)
	$$(call MKDIR,$$(dir $$@))
	$$(RECIPE_IF) $$(COMPILER_C) $$($1_cflags) -c $$< -o $$@ $$(RECIPE_RESULT_COMPILE)

$$($1_obj)/%_banner.c: $$($1_rsrc)/%.bmp | $$($1_obj)
	echo "#include \"gtkpixdata_shim.h\"" > $$@
	gdk-pixbuf-csource --extern --struct --raw --name=startbanner_pixdata $$^ | sed 's/load_inc//' >> $$@

endef

$(foreach i,$(components),$(eval $(call OBJECTRULES,$i)))

ifeq ($(PLATFORM),WINDOWS)
$(nnbuild_obj)/gameres.$o: $(nnbuild_rsrc)/game_icon.ico
endif

define NATIVEEDITORRULE
$(nnbuild_obj)/native/$(notdir $(1:.cpp=.$o)): $(1) $(nnbuild_src)/nativeeditorprefix.h
	$$(COMPILE_STATUS)
	$$(call MKDIR,$$(dir $$@))
	$$(RECIPE_IF) $$(COMPILER_CXX) -fno-lto -USTARTUP_WINDOW -DNNBUILD_NATIVE_EDITOR -include $$(nnbuild_src)/nativeeditorprefix.h -c $$< -o $$@ $$(RECIPE_RESULT_COMPILE)
endef

include $(wildcard $(nnbuild_obj)/native/*.d)
$(foreach file,$(nnbuild_native_sources),$(eval $(call NATIVEEDITORRULE,$(file))))


### Other special cases

# Comment out the following rule to debug a-c.o
$(engine_obj)/a-c.$o: $(engine_src)/a-c.cpp | $(engine_obj)
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(subst -O$(OPTLEVEL),-O2,$(subst $(ASAN_FLAGS),,$(COMPILER_CXX))) $(engine_cflags) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(engine_obj)/rev.$o: $(engine_src)/rev.cpp | $(engine_obj)
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER_CXX) $(engine_cflags) $(REVFLAG) -c $< -o $@ $(RECIPE_RESULT_COMPILE)


### Directories

$(foreach i,$(components),$($i_obj)):
	-$(call MKDIR,$@)

### Phonies

clang-tools: $(filter %.c %.cpp,$(foreach i,$(call getdeps,duke3d,game),$(call expandsrcs,$i)))
	echo $^ -- -x c++ $(CXXONLYFLAGS) $(COMPILERFLAGS) $(CWARNS) $(foreach i,$(components),$($i_cflags))

$(addprefix clean,$(games)):
	-$(call RM,$(foreach i,$(roles),$($(subst clean,,$@)_$i)$(EXESUFFIX)))
	-$(call RMDIR,$($(subst clean,,$@)_obj))
ifeq ($(PLATFORM),DARWIN)
	-$(call RMDIR,$(foreach i,$(roles),"$($(subst clean,,$@)_$i_proper).app"))
endif

cleantools:
	-$(call RM,$(addsuffix $(EXESUFFIX),$($(subst clean,,$@)_targets)))
	-$(call RMDIR,$($(subst clean,,$@)_obj))

clean: cleanduke3d cleansw cleannnbuild cleantools
	-$(call RMDIR,$(obj))
	-$(call RM,$(ebacktrace_dll))
	-$(call RM,$(voidwrap_lib))

printtools:
	echo "$(addsuffix $(EXESUFFIX),$(tools_targets))"

rev: $(engine_obj)/rev.$o


### Compatibility

cleantest: cleankenbuild
cleanutils: cleantools
printutils: printtools
test: kenbuild
utils: tools
veryclean: clean
