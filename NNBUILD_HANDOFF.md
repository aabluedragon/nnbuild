# NNBuild handoff

This file is written for a fresh agent starting from the repository root with
no prior chat context. It captures the current operational state, the important
commands, and the known unfinished work.

## Current repository state

As of 2026-08-16, the workspace is intentionally not clean. Before changing
code, run:

```sh
git status --short
```

Important current observations:

- There are many modified files under `source/nnbuild/`, `source/sw/`,
  `source/build/`, `source/blood/`, `source/exhumed/`, and `source/rr/`.
- `autojs/my-mod/` currently shows deleted generated project files in the
  worktree. Do not restore or remove them unless the user explicitly asks.
- There are untracked generated/runtime artifacts: `__autojs/`, `egamlj.sav`,
  and `obj-win32/`.
- Preserve unrelated user changes. Do not use destructive git commands.

Recent committed history before this dirty state:

```text
de54ee49c Fix Ion Fury deathmatch late-join sync
6d4563e4d Fix Ion Fury menu, sound, and late join issues
4a29cc8e7 Match Fury setup menu font metrics
b47266d25 Fix Ion Fury setup menu text scaling
431bce9d8 Fix VibeBuild32 weapon fire timing
4fa2edb21 Complete Ion Fury CON compatibility
1c6f9336d Support Ion Fury and World Tour CON startup
09d62bb3b Support Ion Fury and World Tour CON scripts
```

## Build command

Use the normal target:

```sh
make -j8 nnbuild
```

The last recorded build after the current Shadow-Warrior-in-Duke bridge edits
linked successfully with warnings only. Rebuild before making claims about the
current checkout because the worktree is dirty.

## Public command-line syntax

NNBuild public options follow this rule:

- Long/full options use `--`.
- Short options use `-`.
- Values can be attached to the option name or separated by one or more spaces.

Examples that should be accepted:

```sh
-l1
-l 1
--level1
--level 1
-v1
-v 1
--episode1
--episode 1
-g/path/to/file.grp
-g /path/to/file.grp
--grp/path/to/file.grp
--grp /path/to/file.grp
```

Important public options:

```text
-g, --grp <file.grp>                 Use a group/data file; can infer the game.
-gm, --game <name>                   Runtime: duke, worldtour, ionfury, sw,
                                     blood, rr, nam, napalm, ww2gi, exhumed.
-gm.lvls, --game.levels <name>       Crossplay map/level domain.
-gm.its, --game.items <name>         Crossplay item domain.
-gm.wpns, --game.weapons <name>      Crossplay weapon domain.
-gjs, --gen-js [name]                Generate autojs/<name>/index.js project.
-gts, --gen-ts [name]                Generate autojs/<name>/index.ts project.
-js, --js <file.js|file.ts>          Load an explicit script.
-skn, --skin <name|#>                Player skin/profile.
-sm, --skip-menu                     Skip game picker/native startup window.
-m, --menu                           Force native startup window.
-sc, --skip-cinema                   Skip startup cinematics/logo movies.
-clr, --color <name|#rrggbb>         Player color; supports brown, gray,
                                     purple, red, yellow, olive, green, blue.
-wnd, --window                       Force windowed mode.
-fscr, --fullscreen                  Force fullscreen mode.
-res, --resolution <WIDTHxHEIGHT>    Force resolution.
-dbg, --debug [port]                 Enable JS debugger endpoint.
-dt, --devtools                      Open JS DevTools; implies debug/windowed.
-mnst, --monsters <n>                Monster/skill control.
-e, --episode <n>                    Alias for -v, --volume.
-l, --level <n>                      Level warp.
-map <file.map>                      Load a map file.
-nosound                             Disable sound effects.
-nomusic                             Disable music.
```

`-dbg` with no value uses port `8501`. `-dbg`, `--debug`, `-dt`, and
`--devtools` imply `-sm`.

The help text still contains one legacy Duke line saying `-m Disable monsters`.
The intended public NNBuild syntax is `-m`/`--menu` for the native menu and
`-mnst`/`--monsters` for monsters.

## Common launch commands

Use `-nosound -nomusic` while testing unless sound/music behavior is the thing
being tested.

Duke:

```sh
./nnbuild -gm duke -g ~/games/duke3da/duke3d.grp -wnd -res1280x720 -sc -sm
```

Shadow Warrior:

```sh
./nnbuild -gm sw -g "/Users/alonamir/games/Shadow Warrior Complete/Sw.grp" -wnd -res1280x720 -sc -sm
```

Ion Fury:

```sh
./nnbuild -gm ionfury --game-dir ~/games/Ion\ Fury -wnd -res1280x720 -sc -sm
```

Shadow Warrior runtime playing Duke E1L1:

```sh
./nnbuild -gm sw -g "/Users/alonamir/games/Shadow Warrior Complete/Sw.grp" -gm.lvls duke -g ~/games/duke3da/duke3d.grp -map E1L1.MAP -wnd -res1280x720 -sc -sm
```

Shadow Warrior runtime playing Duke E3L6 / Rabid Transit:

```sh
./nnbuild -gm sw -g "/Users/alonamir/games/Shadow Warrior Complete/Sw.grp" -gm.lvls duke -g ~/games/duke3da/duke3d.grp -map E3L6.MAP -wnd -res1280x720 -sc -sm
```

## JavaScript/TypeScript mods

Primary doc: `source/glue/VBJS.md`.

Generation commands:

```sh
./nnbuild --gen-ts my-mod
./nnbuild --gen-js my-mod
```

Generated projects auto-load only:

```text
autojs/<project>/index.ts
autojs/<project>/index.js
```

If both exist, `index.ts` wins. Do not generate `main.ts`.

VS Code debugging:

- Generated `.vscode/launch.json` starts the game with only `-dbg`.
- The generated adapter reads top-of-file `/// vbd.params="..."` from
  `index.ts` or `index.js` and appends those args only for debug/devtools
  launches.
- `/// vbd.params="..."` is the implemented top-of-file launch directive.
- `-dbg` with no port means engine debugger port `8501`; the DAP adapter uses
  its own local port, currently `4714` in generated projects.

Supported embedded modules are intentionally Node-like where implemented:

- `vb`
- `fs` and `node:fs`
- `fs/promises` and `node:fs/promises`
- `dgram` and `node:dgram`
- `net` and `node:net`
- `cvar`
- global `fetch`

Do not add JS globals for APIs that are meant to be imported. The public pattern
is:

```ts
import { on, log } from 'vb';
```

## Shadow Warrior playing Duke maps

Primary bridge doc: `source/sw/DUKE_CROSSPLAY_BRIDGE.md`.

The user explicitly wants a broad, principled bridge rather than per-map hacks.
The intended behavior is: when the SW runtime plays a Duke-family level, the
Duke level should behave like a Duke level while the SW player/weapons remain
playable.

Duke enemies now run Duke's own CON/actor code inside the SW session rather
than an approximation of it; see the bridge doc for the architecture. The
bridge has an automated self-test:

```sh
VB_CP_SELFTEST=1 ./nnbuild -gm sw -g "/path/Sw.grp" -gm.lvls duke \
    -g /path/duke3d.grp -map E1L1.MAP -wnd -res640x480 -sc -sm -nosound -nomusic
```

Current status:

- `make -j8 nnbuild` passes.
- E1L1 through E1L8 and E3L6 all report `fail=0`, run per phase group
  (`VB_CP_SELFTEST=actors|world|destruct|tripmines`); 36 group-runs, 0 failures.
  Re-run over E1L1, E1L2, E1L3, E1L4, E1L7 and E3L6 after the damage delegation:
  24 group-runs, 0 failures.
- The E3L6 startup `SIGSEGV` no longer reproduces, and leaving a Duke level no
  longer crashes.
- Duke enemy sounds play: Duke's `A_PlaySound()` is routed to the host, which
  owns the audio device.

Duke props no longer have a bridge-written copy of Duke's damage code: a hit on
one is queued and Duke's own `A_DamageObject_Internal()` runs on it at the top of
the next Duke tic, so its broken frame, its sound and whatever it spawns are
Duke's.  Explosions reach the host's props from both sides -- Duke's
`A_RadiusDamage()` through a hook, and the host's own detonations -- and Duke's
earthquake drives the host's `SpawnQuake()`.  See the bridge doc.

### Duke mods and user maps

A Duke mod GRP is layered on the base game and both are handed over as foreign
data, newest wins:

```sh
./nnbuild -gm sw -g "/path/Sw.grp" -gm.lvls duke \
    -g /path/duke3d.grp -g /path/vacation.grp -map VACA1.MAP -wnd -res1280x720 -sc -sm
```

The mod supplies its own CONs (so its actors, sounds, level names and music are
its own), its own ART layered over the base game's, its own episodes and levels,
and its own music and sound files.  See the bridge doc for how each of those is
resolved; the short version is that the mod GRP comes first in every lookup and
falls through to the base game for anything it does not replace.

A `.pk3`/`.zip` mod works the same way -- its files live in folders inside the
archive and the lookups reduce both sides to a basename.  Verified with
teamduke-0.92.pk3 (three episodes, 63 levels, its own art) and
duke64_nd32_beta2.pk3 (four episodes, 34 levels).

A **user map** is a loose `.MAP` file; play it with `-map` plus `-j <dir>` if it
is not in the working directory:

```sh
./nnbuild -gm sw -g "/path/Sw.grp" -gm.lvls duke \
    -g /path/duke3d.grp -j /path/with/the/map -map MYLEVEL.MAP -wnd -res1280x720
```

A mod's sounds and music are its own as well: sounds are given slots from a
reserved block filled in from the mod's own CON and read by the name that CON
gave (path and all), and the level's track comes from the same `definelevelname`
/ `music` lines that name the level.  `VB_CP_SOUND_PROBE=1` reports how many of
a mod's sound files are actually present in its data.

Art a mod declares through a `.def` (`tilefromtexture` with PNG files) is
imported too: the bridge enumerates the container for `.def` files, reads the
`tilefromtexture` declarations out of them, and decodes the image on demand when
a level references a tile that no ART file in the stack carries.  Whatever is
still missing after that is counted in the "remapped N ... ; missing M" line at
level load, so a shortfall is visible rather than silent.

Remaining items to verify in-game or via automation:

- Duke E1L1 pipe/shaft teleporter works.
- Duke E1L1 spinning/rotating doors work.
- Rabid Transit train carries the player smoothly when standing and jumping.
- Rabid Transit teleporters do not ping-pong indefinitely.
- Duke actors in Duke maps under SW runtime move, shoot, take damage, and play
  correct Duke sounds: trooper, pigcop, turret, tripmine, projectiles.
- Duke breakables and explosives work with SW hitscan and projectile weapons:
  vending machines, trash cans, glass, gas bottles, invisible explosive sources.
- Duke map sounds/music are selected for Duke maps.
- Duke map palette/art stays correct when mixed with SW runtime art.

## World Tour developer commentary

Enabled with the `wt_commentary` cvar (or Options -> Developer commentary, which
only appears under World Tour).  The markers are `DEVELOPERCOMMENTARY` sprites,
tile 5294; each one's `lotag` picks a line out of
`def/developer_commentary.def`, which World Tour ships with 236 entries.  When
the cvar is off the markers are drawn at zero size rather than removed.

Three things the port has to get right, all in `source/nnbuild/src/game.cpp`:

- **The use key starts a line.**  `G_WorldTourMaybeActivateCommentary()` is
  called from `P_CheckSectors()` and finds the marker either by neartag or by
  the distance/angle/`cansee` sweep in `G_WorldTourFindCommentaryMarker()`.
- **The use key stops it again.**  Pressing use on a marker that is still
  speaking calls `S_StopEnvSound()` on it.  This used to be swallowed -- the
  line ran to its end whatever the player did.
- **The marker lights up while it speaks.**  Tile 5295 is the same bubble in
  green; the white 5294 is the idle frame.  The swap happens in
  `G_AnimateSprites()` on the tsprite only and is never written back to
  `sprite[].picnum`: whether the local player is listening is a client cvar, and
  a sprite's picnum is simulation state that has to match on every machine.
  `G_WorldTourCommentaryIsSpeaking()` asks the sound whether it is still
  playing, so a line that runs out reverts the marker on its own with nothing
  timing it.

Only one line plays at a time -- starting another marker cuts the one still
speaking -- and `G_EnterLevel()` calls `G_WorldTourResetCommentary()`, because
the sprite index the last line was talking through belongs to something else in
the next level.

Verified against World Tour's own E1L1 (35 markers) by driving the player to
marker 753, pressing use, and reading the state back: the sound plays and the
drawn tile becomes 5295; a second press logs `Stopping World Tour commentary 3`
and the tile goes back; left alone, the marker reverts by itself when the clip
ends (~18 s).  Disabling the stop branch put the second press back to restarting
the line, which is the bug this fixes.  Note that a test run needs sound on --
the repo's `nnbuild_settings.cfg` has `snd_enabled "0"`, so copy it aside and
launch with `-cfg`, never edit the user's own config:

```sh
./nnbuild -gm worldtour -g "/path/World Tour/DUKE3D.GRP" \
    -j /path/with/WT/maps -map E1L1.MAP -cfg /tmp/wt.cfg -wnd -res640x480 -nomusic
```

## Documentation maintenance rule

When changing behavior, update the relevant Markdown in the same patch:

- `NNBUILD_HANDOFF.md` for restart/agent handoff facts.
- `source/glue/VBJS.md` for JS/TS API and debugging.
- `source/sw/DUKE_CROSSPLAY_BRIDGE.md` for SW runtime + Duke map behavior.
- `source/sw/UNIFY_WITH_NNBUILD.md` for high-level SW integration status.
- `source/nnbuild/NETCODE.md` for netcode-specific behavior.
