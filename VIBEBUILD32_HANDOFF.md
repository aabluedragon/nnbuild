# VibeBuild32 handoff

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

- There are many modified files under `source/vibebuild32/`, `source/sw/`,
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
make -j8 vibebuild32
```

The last recorded build after the current Shadow-Warrior-in-Duke bridge edits
linked successfully with warnings only. Rebuild before making claims about the
current checkout because the worktree is dirty.

## Public command-line syntax

VibeBuild32 public options follow this rule:

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
The intended public VibeBuild32 syntax is `-m`/`--menu` for the native menu and
`-mnst`/`--monsters` for monsters.

## Common launch commands

Use `-nosound -nomusic` while testing unless sound/music behavior is the thing
being tested.

Duke:

```sh
./vibebuild32 -gm duke -g ~/games/duke3da/duke3d.grp -wnd -res1280x720 -sc -sm
```

Shadow Warrior:

```sh
./vibebuild32 -gm sw -g "/Users/alonamir/games/Shadow Warrior Complete/Sw.grp" -wnd -res1280x720 -sc -sm
```

Ion Fury:

```sh
./vibebuild32 -gm ionfury --game-dir ~/games/Ion\ Fury -wnd -res1280x720 -sc -sm
```

Shadow Warrior runtime playing Duke E1L1:

```sh
./vibebuild32 -gm sw -g "/Users/alonamir/games/Shadow Warrior Complete/Sw.grp" -gm.lvls duke -g ~/games/duke3da/duke3d.grp -map E1L1.MAP -wnd -res1280x720 -sc -sm
```

Shadow Warrior runtime playing Duke E3L6 / Rabid Transit:

```sh
./vibebuild32 -gm sw -g "/Users/alonamir/games/Shadow Warrior Complete/Sw.grp" -gm.lvls duke -g ~/games/duke3da/duke3d.grp -map E3L6.MAP -wnd -res1280x720 -sc -sm
```

## JavaScript/TypeScript mods

Primary doc: `source/glue/VBJS.md`.

Generation commands:

```sh
./vibebuild32 --gen-ts my-mod
./vibebuild32 --gen-js my-mod
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

Current status from the last recorded smoke tests:

- `make -j8 vibebuild32` passed.
- SW runtime + Duke E1L1 startup smoke passed long enough to initialize the
  bridge and run.
- SW runtime + Duke E3L6/Rabid Transit currently crashes with `SIGSEGV` shortly
  after bridge capture. This is the first issue a new agent should reproduce and
  debug before claiming the bridge is fixed.

Do not mark the crossplay goal complete until these are verified in-game or via
reliable automation:

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

## Documentation maintenance rule

When changing behavior, update the relevant Markdown in the same patch:

- `VIBEBUILD32_HANDOFF.md` for restart/agent handoff facts.
- `source/glue/VBJS.md` for JS/TS API and debugging.
- `source/sw/DUKE_CROSSPLAY_BRIDGE.md` for SW runtime + Duke map behavior.
- `source/sw/UNIFY_WITH_VIBEBUILD32.md` for high-level SW integration status.
- `source/vibebuild32/NETCODE.md` for netcode-specific behavior.
