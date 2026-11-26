# Shadow Warrior

Shadow Warrior has its own runtime inside NNBuild. It plays the game, has
multiplayer with lobby support, and runs a limited set of script features. The
map editor, the dedicated server and the host console commands belong to the
Duke Nukem 3D runtime and are not available here.

| | |
| --- | --- |
| `--game` name | `sw` (also `shadowwarrior`, `shadow-warrior`) |
| Main data file | `SW.GRP` |
| Folder NNBuild looks in first | `sw` |
| Settings and log | `voidsw.cfg`, `voidsw.log` |

## Data files

You need `SW.GRP` from your own copy of the game. NNBuild recognises these
releases by size and checksum:

| Release | Kind |
| --- | --- |
| Shadow Warrior (registered) | Full game |
| Shadow Warrior, European, UK and censored releases | Full game |
| Shadow Warrior Shareware 1.0, 1.1 and 1.2 | Shareware |
| Shadow Warrior Mac Demo | Shareware |
| Wanton Destruction | Add-on, needs the registered game |
| Twin Dragon (three known releases) | Add-on, needs the registered game |

How the data is found:

1. At start, NNBuild looks for `SW.GRP` (also `Sw.grp` or `sw.grp`) in the
   folder you started it from and in its `sw` subfolder.
2. A data file that you pass on the command line selects Shadow Warrior, by
   its checksum or by one of these names: `SW.GRP`, `WANTON.GRP`, `TD.GRP`,
   `TWIN DRAGON.GRP`, `TWIN_DRAGON.GRP`.
3. Once Shadow Warrior is chosen, the game scans its search folders, which
   include the Steam and GOG.com install locations of the Classic releases.

To start an add-on whose data file was found, add one of these switches:

| Switch | Starts |
| --- | --- |
| `-addon0` | Shadow Warrior itself |
| `-addon1` | Wanton Destruction |
| `-addon2` | Twin Dragon |

## Starting the game

Start Shadow Warrior with `--skip-menu`. It then goes straight to its own
title screen.

```sh
# Data in the sw subfolder
nnbuild --game sw --skip-menu

# Data somewhere else
nnbuild --game sw --grp ~/games/sw/SW.GRP --skip-menu --skip-cinema

# Start at level 4 on skill 3
nnbuild --game sw --skip-menu --level 4 -s3

# A map file of your own (not with shareware data)
nnbuild --game sw --skip-menu -map ~/maps/dojo.map

# Wanton Destruction
nnbuild --game sw --skip-menu -addon1
```

Useful options of this runtime:

| Option | Meaning |
| --- | --- |
| `--level <n>` | Start at level `n`. Level 1 is the first level. |
| `-s<n>` | Skill, 1 to 4. |
| `-map <file>` | Load a map file. |
| `-monst` | No monsters. |
| `-name <name>` | Your player name. |
| `-nodemo` | Do not play demos at the title screen. |
| `-nometers` | Hide the air and boss meter bars. |

Set the video mode in the game's own options menu.

## Multiplayer

Shadow Warrior uses the same `-net` syntax as Duke Nukem 3D. The host names
the number of players and everyone else names the host. The game starts when
all players are connected. A match holds up to 8 players, or 4 with shareware
data.

```sh
# Host a 2-player game on UDP port 23513
nnbuild --game sw --skip-menu -name Host -net /p23513 /n0:2

# Join it
nnbuild --game sw --skip-menu -name Guest -net /n0 192.0.2.10:23513
```

`-net` and everything after it must come last on the command line. The parts
of the `-net` block are explained in
[Launch options](../reference/command-line.md#the--net-block).

Lobby options, written before `-net`:

| Option | Meaning |
| --- | --- |
| `--advertise <address>` | Host: list this game on the lobby server at `address`. |
| `--lobbylist <address>` | Write the games listed on that lobby server to the log, then continue starting. |
| `--joinlobby <address>` | Join the first game listed on that lobby server. No `-net` block is needed. |

```sh
# Host and list the game on a lobby server
nnbuild --game sw --skip-menu --advertise lobby.example.org -net /p23513 /n0:2

# Join whatever that lobby server lists first
nnbuild --game sw --skip-menu --joinlobby lobby.example.org
```

The short forms `-ad` (for `--advertise`) and `-ll` (for `--lobbylist`) work
too. Write `--joinlobby` in full: Shadow Warrior ignores its short form `-jl`.

When the host has loaded a map file with `-map`, it is sent to the other
players before the level starts.

When Shadow Warrior runs as a network game, **New Game** opens the Network
Game menu. Its **Browse Servers** entry shows the games on a lobby server and
joins the one you pick. It asks the lobby server named with
`-masteraddr <address>`, or the public lobby `nnb.ms.nukemnet.com` if that
option was not given.
Joining from this list works on Linux and macOS.

## Script mods

Shadow Warrior loads JavaScript and TypeScript mods the same way Duke Nukem 3D
does: from `autojs/<name>/index.ts` or `index.js`, or with `--js <file>`.
It offers the part of the script interface that does not depend on Duke Nukem
3D: events with `on` and `off`, `log`, the built-in modules, `fetch`, and
debugging in VS Code. Functions that read or change Duke Nukem 3D game state
stop the script with an error. See [Modding](../modding/overview.md).

## Playing Duke Nukem 3D levels

The Shadow Warrior runtime can load levels from Duke Nukem 3D, World Tour, NAM,
NAPALM and WWII GI. You play as Lo Wang, with Shadow Warrior's weapons, inside
the other game's level.

```sh
nnbuild --game sw --grp ~/games/sw/SW.GRP \
    --game.levels duke --grp ~/games/duke3d/DUKE3D.GRP \
    -map E1L1.MAP --skip-menu
```

- `--game.levels duke` names the game the levels come from.
- The second `--grp` gives that game's data. NNBuild uses it only as a source
  of levels, art and sounds.
- `-map` names the level to start.

What happens to the level:

- Item, weapon and ammo pickups become their Shadow Warrior counterparts.
- The Duke Nukem 3D enemies stay in the level.

To add a Duke Nukem 3D mod, give its file with another `--grp` after the base
game. A file that NNBuild does not recognise as a known game is taken as add-on
data for the game named by `--game.levels`. Files of type `.grp`, `.zip`,
`.pk3`, `.pk4` and `.ssi` are accepted.

```sh
nnbuild --game sw --grp ~/games/sw/SW.GRP \
    --game.levels duke --grp ~/games/duke3d/DUKE3D.GRP --grp ~/games/duke3d/vacation.grp \
    -map VACA1.MAP --skip-menu
```

A single map file is played with `-map`, plus `-j <folder>` when it is not in
a folder that is already searched:

```sh
nnbuild --game sw --grp ~/games/sw/SW.GRP \
    --game.levels duke --grp ~/games/duke3d/DUKE3D.GRP \
    -j ~/maps -map MYLEVEL.MAP --skip-menu
```

`--game.items` and `--game.weapons` are explained in
[Supported games](overview.md#playing-one-games-levels-in-another). Left out,
they mean Shadow Warrior.

## Known limits

- No dedicated server, no server console and no host commands such as kick or
  map rotation.
- Players cannot join a match after it has started.
- Shareware data cannot load map files or extra data files.
- The video mode is not set from the command line.
- Joining from **Browse Servers** is not available on Windows.
