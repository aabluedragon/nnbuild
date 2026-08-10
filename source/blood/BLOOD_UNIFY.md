# Adding Blood (from NotBlood) to the vibebuild32 executable

Goal: a fourth `-game blood` in the one `vibebuild32` binary, sharing our engine,
sourced from NotBlood (`/Users/alonamir/dev/NotBlood`). Same merge pattern as SW
(see ../sw/UNIFY_WITH_VIBEBUILD32.md): compile Blood's game module against OUR
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
aliasing compiles the same way. Combined `WITH_SW=1 vibebuild32` links; Duke
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
2. Add the bounded engine gaps above to source/build (structs, mirror globals,
   enums, input shims, `_replace` hooks).
3. Compile Blood as a module (all 88 files + blood headers) → resolve the
   Blood-internal undeclared refs; reach 0 compile errors.
4. Build wiring: `blood_obj`, a `WITH_BLOOD` block mirroring `WITH_SW`
   (`ld -r` localize collisions vs Duke, drop Blood's startup-window objects),
   `blood_merged.o` into `vibebuild32_game_miscdeps`.
5. Rename Blood's `app_main`/`G_DefaultDefFile`/`faketimerhandler` → `Blood_*`;
   extend gameselect.cpp dispatch (`g_activeGame==2`), add `-game blood`, GRP/RFF
   autodetect (`blood/` subdir, blood.rff / BLOOD.RFF).
6. Link, localize collisions (nm intersect vs Duke+SW), reach a clean 3-game link.
7. Run `-game blood` and confirm it reaches Blood's title/menu.
8. Then port the vibebuild32 MP features onto Blood (mmulti transport is already
   shared; masterserver/advertise/punch/lobby/file-transfer re-home like SW).
