# Duke Nukem 3D

Duke Nukem 3D is the main game of NNBuild. Every feature described on this
site works with it. This page also covers the official add-ons and
[World Tour](#world-tour).

| | |
| --- | --- |
| `--game` name | `duke` (World Tour: `worldtour`) |
| Main data file | `DUKE3D.GRP` |
| Folder NNBuild looks in first | `duke3d` (World Tour: `worldtour`) |
| Settings and log | `vibebuild32.cfg`, `vibebuild32.log` |

## Data files

You need `DUKE3D.GRP` from your own copy of the game. NNBuild recognises a data
file by its size and checksum, not by its name, and knows these releases:

| Release | Kind |
| --- | --- |
| Duke Nukem 3D 1.3D | Full game |
| Duke Nukem 3D 1.3D, South Korean censored release | Full game |
| Duke Nukem 3D: Atomic Edition (1.5) | Full game |
| Duke Nukem 3D: Atomic Edition, the `DUKE3D.GRP` that comes with World Tour | Full game |
| Duke Nukem 3D: Plutonium Pak | Full game |
| Shareware 0.99, 1.0, 1.1 and 1.3D | Shareware |
| Mac Demo and MacUser Demo | Shareware |

These official add-ons are recognised too. An add-on is offered only when the
full game it needs is found as well.

| Add-on | Needs |
| --- | --- |
| Duke it out in D.C. (1.3D release) | Duke Nukem 3D 1.3D |
| Duke it out in D.C. | Atomic Edition |
| Duke Caribbean: Life's a Beach (1.3D release) | Duke Nukem 3D 1.3D |
| Duke Caribbean: Life's a Beach (Plutonium Pak release) | Plutonium Pak |
| Duke Caribbean: Life's a Beach | Atomic Edition |
| Duke: Nuclear Winter, and its demo | Atomic Edition |
| Duke!ZONE II (1.3D release) | Duke Nukem 3D 1.3D |
| Duke!ZONE II | Atomic Edition |
| Duke Nukem's Penthouse Paradise | Atomic Edition |

### How the data is found

1. At start, NNBuild looks for a file named `DUKE3D.GRP` (or `duke3d.grp`) in
   the folder you started it from and in its `duke3d` subfolder.
2. Once Duke Nukem 3D is chosen, the game checks every `.grp`, `.ssi`, `.dat`
   and `.zip` file in its search folders against the list above. The search
   folders include the installed store copies listed in
   [Installing](../getting-started/installing.md).
3. If several recognised files are found, the **Game** list in the startup
   window lets you choose. On the command line, `--gamegrp <file>` chooses.

```sh
vibebuild32 --grp ~/games/duke3d/DUKE3D.GRP
```

To start an add-on, name the add-on's own data file with `--gamegrp`. Keep
the full game's `DUKE3D.GRP` in the same folder:

```sh
vibebuild32 --gamegrp ~/games/duke3d/dukedc.grp
```

Other games and total conversions can be made known to NNBuild with `.grpinfo`
files placed in a search folder.

## Starting the game

```sh
# Data in the duke3d subfolder, or an installed store copy
vibebuild32 --game duke

# Straight into the game, no startup window, no intro
vibebuild32 --game duke --skip-menu --skip-cinema

# Episode 1, level 3, skill 3
vibebuild32 --game duke --volume 1 --level 3 --skill 3

# A map file of your own
vibebuild32 --game duke -map ~/maps/arena.map

# No monsters
vibebuild32 --game duke --volume 1 --level 1 --monsters 0
```

`--monsters <n>` turns monsters off when `n` is 0. A value of 1 or more turns
them on and also sets the skill. Higher numbers are harder. The skill used is
`n` minus one, so `--monsters 3` gives the same skill as `--skill 2`.

All options are listed in [Launch options](../reference/command-line.md).

## World Tour

*Duke Nukem 3D: 20th Anniversary World Tour* is run by the same runtime. It
needs the `DUKE3D.GRP` of World Tour together with the loose files the game
installs next to it. NNBuild treats a folder as World Tour when it holds all
of these:

```text
DUKE3D.GRP
GAME.CON
DEFS.CON
USER.CON
FLAMETHROWER.CON
FIREFLYTROOPER.CON
EPISODE5BOSS.CON
TILES020.ART
maps/E5L1.map
```

Put that folder at `worldtour` (or `world-tour`, or `duke3d_wt`) next to where
you start NNBuild, or point at it:

```sh
vibebuild32 --game worldtour --game-dir "/home/you/games/World Tour"
```

Details:

- NNBuild loads World Tour's own `GAME.CON` script automatically. Give
  `--con <file>` to load a different one.
- If the loose files are missing, the World Tour `DUKE3D.GRP` is used as a
  plain Atomic Edition data file.
- The developer commentary is off by default. Turn it on in the sound setup
  menu or with the console setting `wt_commentary 1`. See
  [Console commands](../reference/console-commands.md#wt_commentary).

## What you can do in this game

- **Multiplayer.** The title menu has a **Multiplayer** entry with **Join
  Game** (a list of games from a lobby server, or an address you type) and
  **Host Game** (internet or LAN). You can join a match that is already
  running and come back to it after dropping out. See
  [Playing online](../multiplayer/playing-online.md) and
  [Hosting a match](../multiplayer/hosting.md).
- **Dedicated server.** A server without a window that takes commands in the
  terminal: kick and ban, map changes, mod pack changes and map rotation. See
  [Dedicated server](../multiplayer/dedicated-server.md) and
  [Console commands](../reference/console-commands.md).
- **Automatic downloads.** Custom maps and mod packs used by a server are sent
  to players when they join.
- **Enemy skins.** In multiplayer you can appear as one of the enemies. Set it
  with the console setting [`skin`](../reference/console-commands.md#skin).
- **Mods in JavaScript or TypeScript.** See
  [Modding](../modding/overview.md).
- **Map editor and projects.** Open **Map Editor** from the title menu or
  start with `--editor`. See [Map editor](../editor/overview.md).
- **Control by an AI assistant.** See
  [AI assistants and MCP](../editor/ai-and-mcp.md).

## Playing other games' levels

The Duke Nukem 3D runtime can load a Shadow Warrior level. Name Shadow Warrior
as the source of the levels and give its data file:

```sh
vibebuild32 --game duke --gamegrp ~/games/duke3d/DUKE3D.GRP \
    --game.levels sw --grp ~/games/sw/SW.GRP -map LEVELNAME.MAP
```

Replace `LEVELNAME.MAP` with the name of a map inside `SW.GRP`.

You keep Duke's player, weapons and rules. Item and weapon pickups in the level
are replaced by their Duke Nukem 3D counterparts. Every other Shadow Warrior
object in the level, including its enemies, is removed.

`--game.items` and `--game.weapons` are explained in
[Supported games](overview.md#playing-one-games-levels-in-another).

## Known limits

- **Shareware data.** With a shareware data file you cannot load map files
  (`-map`), use the `map` and `changemap` console commands, or open the map
  editor.
- **Players.** A match has at most 16 player slots.
- **Joining a running match.** The joining player needs the same kind of
  build as the host: both 64-bit, or both 32-bit.
- **Base game data is never sent.** Every player needs their own copy of the
  same edition the server uses. A server can share up to eight mod packs.
- **`--grp` and mod packs.** In multiplayer, a mod pack given with `--grp` is
  offered to the players who join. A file that NNBuild recognises as game
  data, such as `DUKE3D.GRP` or an official add-on, is not offered. The
  game's own data file can be given with `--grp` or with `--gamegrp`.
- **World Tour and the editor.** The map editor can target Duke Nukem 3D 1.3D
  and Atomic / Plutonium. It has no World Tour target.
