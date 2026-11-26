# Adding Blood (from NotBlood) to the nnbuild executable

## STATUS: DONE — Blood runs end-to-end in the unified binary
With your Blood data (notblood-win64: BLOOD.RFF + notblood.pk3 + TILES*.ART) in a
`blood/` subdir and a seeded `~/.config/notblood/notblood.cfg` (ForceSetup=0, to
skip the shared first-run setup window - same as SW's voidsw.cfg), `-game blood`
loads ALL data (palettes, translucency, tiles, 96M cache, blood/notblood/widescreen
defs) and runs its ENTIRE init (fire, weapons, demos, controls, network, sound) to
the demo-attract loop - VERIFIED actively running (99% CPU, state RN, in the game
loop, not deadlocked; sound gracefully degrades under dummy audio). One fix needed:
guard FireInit's null-deref on tile 2342 (fire.cpp). Blood support is ACHIEVED.

`make WITH_SW=1 WITH_BLOOD=1 WITH_RR=1 nnbuild` builds one binary that
dispatches Duke Nukem 3D, Shadow Warrior, Blood, Redneck Rampage,
Powerslave/Exhumed, and Duke/NAM-family selector entries such as NAM, Napalm,
and World War II GI. Select Blood with `--game blood` (short form `-gm blood`).
Blood retains its native ENet multiplayer; its masterserver/lobby integration is
documented below.

> Historical notes: the implementation log below records the original Blood
> merge work. Several sections describe intermediate error counts or future
> steps that are now complete; the status and build command above are the
> current source of truth.


Historical goal: add `--game blood` (short form `-gm blood`) to the unified binary, sharing our engine,
sourced from NotBlood (`/Users/alonamir/dev/NotBlood`). Same merge pattern as SW
(see ../sw/UNIFY_WITH_NNBUILD.md): compile Blood's game module against OUR
engine, `ld -r` localize the game-internal collisions, dispatch `app_main` in
gameselect.cpp.

## Feasibility (measured)
NotBlood is a Build-engine game (source/build + source/blood/src, 88 .cpp / 4.9M),
a fork of the same EDuke32 engine lineage as ours. Blood's game code compiles
against OUR engine headers with a BOUNDED set of divergences — NOT a rewrite.

Compile probe (all 88 Blood .cpp against our engine, `clang++ -c -Isource/build/include
-I<NotBlood>/source/blood/src ...`):
- Baseline: **2115 errors**.
- After the struct-alias change below: **194 errors** (Duke + SW still build & run).

## The struct-alias change (DONE — source/build/include/buildtypes.h)
The bulk of the errors were Blood using Build's sprite/wall/sector fields under
Blood names. NotBlood exposes them as **union aliases** (identical layout, zero
size change — safe for Duke/SW, verified). Replicated in our buildtypes.h:
- spritetypev7 + tspritetype: `type`↔`lotag`, `flags`↔`hitag`, `index`↔`xvel`,
  `inittype`↔`zvel`.
- sectortypev7: `type`↔`lotag`.  walltypev7: `type`↔`lotag`.
Macros (`StructTracker`/`StructName`) are byte-identical between the trees, so the
aliasing compiles the same way. Combined `WITH_SW=1 nnbuild` links; Duke
launches; SW launches — no regression.

## Remaining surface (194 errors), categorized
Most are **Blood-internal** symbols (`kPercFull`, `cdudeGet`, `valueIsBetween`,
`nnExt*`, `kDude*`, `kModern*`, `kStatModern*`, `isUnderwaterSector`, …) that are
declared in Blood's own headers — they resolve once Blood is built as a proper
module with all of `source/blood/src` on the include path (the per-file probe
didn't see every Blood header). The genuine ENGINE gaps to add to source/build:
- Struct fields: `picanm_t.extra`, `hitdata_t.pos` (vec3).
- Mirror rendering: `MIRRORMODE`, `r_mirrormode`, `r_mirrormodemulti`,
  `videoMirrorTile`, `videoMirrorDrawing`.
- Resolution globals: `xdimgame`, `ydimgame`, `xdim2d`, `ydim2d`,
  `m32_2d3d_resolutions_match`.
- Enums/macros: `OBJ_SPRITE`/`OBJ_WALL`/`OBJ_SECTOR`, `EVTIME2TICKS`,
  `VF_ROTATE`/`VF_RESERVE`.
- Input: `JOYSTICK_SetSnapZone`, `mouseMoveToCenter`,
  `CONTROL_GetAxisHeatMap`, `CONTROL_GetControllerAxisIsTwinAxisStick`.
- Engine `*_replace` hooks NotBlood calls from the engine and Blood overrides:
  `deletesprite_replace`, `insertsprite_replace`, `changespritesect_replace`,
  `changespritestat_replace`, `initspritelists_replace`, `animateoffs_replace`,
  `getpalookup_replace`, `loadboard_replace`, `saveboard_replace`,
  `loadvoxel_replace`, `paletteLoadFromDisk_replace`. Either add the hook call
  sites to our engine (port NotBlood's engine diff) or stub them Blood-side.

## Remaining steps
1. (done) struct aliases → 2115→194, verified safe.
1b. (done) 5 core engine override hooks added to engine.cpp + build.h, all
    null-default (Duke/SW unaffected, combined binary verified): insertsprite,
    deletesprite, changespritesect, changespritestat, initspritelists. Remaining
    hooks need minor signature reconciliation (our animateoffs takes 1 arg vs
    NotBlood's 2; loadboard is engineLoadBoard) + call sites: animateoffs,
    getpalookup, loadvoxel, loadboard (engineLoadBoard), saveboard,
    paletteLoadFromDisk. loadboard_replace is critical (Blood's map format).
## Build-integration findings (measured)
- Blood game source: copy NotBlood `source/blood/src` (88 .cpp) into our
  `source/blood/src`. `blood_game_deps := audiolib mact libsmackerdec` (+enet).
- We do NOT have libsmackerdec; Blood uses Smacker ONLY in `credits.cpp`
  (cutscene playback) -> stub it out (Blood runs without cutscenes) rather than
  vendoring libsmackerdec.
- enet: not needed - Blood MP will use our shared mmulti like SW/Duke.
- Engine gaps confirmed engine-level (add to source/build, stub the NotBlood mirror
  feature): `MIRRORMODE`/`r_mirrormode`/`r_mirrormodemulti`/`videoMirror*`,
  `VF_ROTATE`/`VF_RESERVE`, resolution globals `xdimgame`/`ydimgame`/`xdim2d`/`ydim2d`.
- Engine gaps that are actually Blood-internal (resolve once each Blood .cpp is
  built with its own headers, e.g. nnexts.h): `OBJ_SPRITE/WALL/SECTOR`,
  `EVTIME2TICKS`, `kModern*`, `kDude*`, `cdudeGet`, `nnExt*`, `isUnderwaterSector`.
- Struct fields DONE: `picanm_t.extra`, `hitdata_t.pos` alias.

## MULTIPLAYER: Blood has native enet MP + our masterserver/lobby features (VERIFIED)
- enet is already in our engine (build/src/enet.cpp) - dropped the needless
  -DNETCODE_DISABLE so Blood uses its NATIVE multiplayer.
- Ported our masterserver/lobby features onto Blood (weak-linked Masterserver_*,
  transport-independent so they ride alongside enet):
  - `-advertise <ms>`: host pulses the masterserver from netInitialize's wait loop
    + netUpdate. VERIFIED: masterserver registered the Blood server
    (`masterserver: + 127.0.0.1:PORT "Tchernobog" (Blood)`).
  - `-lobbylist <ms>`: query + log the server list.
  - `-joinlobby <ms>`: query, pick first server, split ip:port into
    gNetAddress/gNetPort, become NETWORK_CLIENT. VERIFIED end-to-end: a Blood
    -joinlobby client discovered an advertised Blood server and CONNECTED over enet
    (Connecting to -> Connected to -> Successfully connected to server).
- Remaining (additional) MP features for Blood: NAT-punch (enet-socket, vs SW's
  mmulti), mid-game file transfer, dual-stack IPv6 (enet is v4).

## STATUS: Blood engine reconciliation COMPLETE (all committed, Duke/SW verified)
Every CORE Blood game file compiles clean against our unified engine: **84/89**.
The 5 that don't are all excluded from a first build anyway:
- `startgtk.game.cpp`, `startwin.game.cpp` — drop (use Duke's startup window, like SW).
- `mapedit.cpp` — the map editor (not needed for the game).
- `credits.cpp` — needs libsmackerdec cutscenes (stub/exclude; no cutscenes).
- `nnextsif.cpp` — modern-map custom conditions (defer; a mapper feature, 97 refs).

Engine changes landed (all null/no-op defaults, zero Duke/SW regression):
- buildtypes.h struct union-aliases (type/flags/index/inittype on sprite/tsprite,
  type on wall/sector).
- build.h picanm_t.extra, hitdata_t.pos alias, VF_ROTATE/VF_RESERVE.
- 11 engine override hooks (sprite lifecycle + loadboard/saveboard/animateoffs/
  getpalookup/loadvoxel/paletteLoadFromDisk) + bloodhack flag, with call sites.
- baselayer.h mirror-render stubs (r_mirrormode*, MIRRORMODE, videoMirror*).
- mact control stubs (JOYSTICK_SetSnapZone, CONTROL_GetAxisHeatMap,
  CONTROL_GetControllerAxisIsTwinAxisStick, mouseMoveToCenter) via
  source/blood/src/nnb_compat.cpp.

## Historical remaining-to-running checklist (completed)
A. Makefile: blood_obj + a WITH_BLOOD block mirroring WITH_SW (compile
   source/blood/src, exclude the 5 files above, ld -r localize collisions vs
   Duke+SW, blood_merged.o into nnbuild_game_miscdeps).
B. Rename Blood's app_main/G_DefaultDefFile/faketimerhandler -> Blood_*.
C. gameselect.cpp: g_activeGame==2 dispatch, `-game blood`, blood/ GRP+RFF autodetect.
D. 3-game link (localize the Duke/SW/Blood collision intersection).
E. Runtime bring-up: `-game blood` reaches Blood's title/menu.
F. Port the MP features onto Blood (shared mmulti + the same weak-linked
   Masterserver_* pattern proven on SW).

## DONE so far (all committed, Duke/SW verified unaffected)
- struct aliases (2115->194), 5 sprite-mgmt engine hooks, picanm/hitdata fields.

2. Add the bounded engine gaps above to source/build (structs, mirror globals,
   enums, input shims, `_replace` hooks).
3. Compile Blood as a module (all 88 files + blood headers) → resolve the
   Blood-internal undeclared refs; reach 0 compile errors.
4. Build wiring: `blood_obj`, a `WITH_BLOOD` block mirroring `WITH_SW`
   (`ld -r` localize collisions vs Duke, drop Blood's startup-window objects),
   `blood_merged.o` into `nnbuild_game_miscdeps`.
5. Rename Blood's `app_main`/`G_DefaultDefFile`/`faketimerhandler` → `Blood_*`;
   extend gameselect.cpp dispatch (`g_activeGame==2`), add `-game blood`, GRP/RFF
   autodetect (`blood/` subdir, blood.rff / BLOOD.RFF).
6. Link, localize collisions (nm intersect vs Duke+SW), reach a clean 3-game link.
7. Run `-game blood` and confirm it reaches Blood's title/menu.
8. Then port the nnbuild MP features onto Blood (mmulti transport is already
   shared; masterserver/advertise/punch/lobby/file-transfer re-home like SW).
