# Redneck Rampage in the VibeBuild32 unified binary

Redneck Rampage (and RRRA / NAM / WW2GI, which share its executable) is linked
into the single `vibebuild32` binary as a fourth game, alongside Duke Nukem 3D,
Shadow Warrior and Blood. Build it with:

    make WITH_SW=1 WITH_BLOOD=1 WITH_RR=1 vibebuild32

## How it fits together

`source/rr/` is an eduke32-lineage fork of the Duke code (imported from
Rednukem/NBlood), so unlike Blood it compiles cleanly against our modern
`source/build` engine. Because it *is* Duke code, almost every game-internal
symbol collides with our Duke3D game code.

The merge follows the same `ld -r` + `-unexported_symbols_list` pattern as
WITH_SW / WITH_BLOOD (see the `$(obj)/rr_merged.o` rule in `GNUmakefile`):

1. Compile the RR game objects into `obj/rr/`.
2. Collect the strong symbols defined by Duke + SW + Blood.
3. Localize (hide) every RR symbol that also exists in one of those, so the RR
   copies never satisfy references from the other games and vice-versa.

The three engine callbacks that every game defines are renamed in the RR source
and dispatched from `source/vibebuild32/src/gameselect.cpp` on `g_activeGame == 3`:

| canonical (engine)   | RR symbol              | file                    |
|----------------------|------------------------|-------------------------|
| `app_main`           | `RR_app_main`          | `src/game.cpp`          |
| `G_DefaultDefFile`   | `RR_G_DefaultDefFile`  | `src/common.cpp`        |
| `faketimerhandler`   | `RR_faketimerhandler`  | `src/net.cpp`           |

## Selection

`REDNECK.GRP` (or `redneck.grp`) in the working directory, or in an `rr/` subdir,
makes Redneck Rampage available. Pick it with `--game rr` (short form `-gm rr`; aliases: `redneck`,
`redneckrampage`), or from the console game picker when more than one game's data
is present. RR uses the engine's native ENet netcode (like Blood), so the
multiplayer feature set is ported onto that transport.

## Duke Nukem 64 ("Reality") is intentionally excluded

Rednukem bundles a Duke Nukem 64 total conversion in `src/n64/`. Its renderer
(`reality_render.cpp`) depends on a large set of DN64-specific Build-engine
patches (the `DAMETH_N64*` texture-cache methods, N64 GLSL shaders, a custom tile
decompressor) that our engine does not carry, and DN64 needs its own ROM data.
It is not Redneck Rampage, so it is not built. The RR game code still references
the DN64 entry points behind `if (REALITY)` (never true for Redneck Rampage);
`src/n64/reality_stubs.cpp` provides no-op definitions so that dead code links.
Two harmless, default-off engine hooks (`duke64`, `rt_tileload_callback`) were
added so the tile path can host a future DN64 decompressor. To actually enable
DN64, drop `reality_stubs.cpp`, build the real `n64/reality*.cpp`, and port the
upstream `DAMETH_N64` / `PTH_N64` / `polymost_setuptexture` engine patches.
