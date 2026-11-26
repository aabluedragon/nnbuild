# Blood

Blood has its own runtime inside NNBuild. It plays the game and has
multiplayer with lobby options on the command line. The map editor, script
mods, the dedicated server and the host console commands belong to the Duke
Nukem 3D runtime and are not available here.

| | |
| --- | --- |
| `--game` name | `blood` |
| Main data file | `BLOOD.RFF` |
| Folder NNBuild looks in first | `blood` |
| Settings and log | `notblood.cfg`, `notblood.log` |

## Data files

Blood does not keep its data in a single file. Use the whole folder of your
own copy of the game. The runtime opens these files from it:

- `BLOOD.RFF`, `SOUNDS.RFF` and `GUI.RFF`
- `BLOOD.INI`
- the art files, starting with `TILES000.ART`
- `SURFACE.DAT`, `VOXEL.DAT` and `SHADE.DAT`

If a file named `notblood.pk3` is in a search folder, it is loaded as well.

Episode sets are described by `.ini` files. These three are known by name:

| File | Episode set |
| --- | --- |
| `BLOOD.INI` | Blood: One Unit Whole Blood |
| `CRYPTIC.INI` | Cryptic Passage. Also needs the art files `CPART07.AR_` and `CPART15.AR_`. |
| `DW.INI` | Death Wish |

How the data is found:

1. At start, NNBuild looks for `BLOOD.RFF` (or `blood.rff`) in the folder you
   started it from and in its `blood` subfolder.
2. A file that you pass on the command line selects Blood when it is a Blood
   `.rff` archive, a Blood episode `.ini` file, or is named `BLOOD.RFF` or
   `BLOOD.INI`.
3. A folder given with `--game-dir` or `-j` selects Blood when it holds
   `BLOOD.RFF` and no other game's main data file.

## Starting the game

Start Blood with `--skip-menu`. It then goes straight to its own title screen.

```sh
# Data in the blood subfolder
nnbuild --game blood --skip-menu

# Data somewhere else
nnbuild --game blood --game-dir ~/games/blood --skip-menu

# Episode 1, level 3
nnbuild --game blood --skip-menu -level 1 3

# A map file of your own
nnbuild --game blood --skip-menu -map ~/maps/crypt.map

# Cryptic Passage
nnbuild --game blood --skip-menu --ini ~/games/blood/CRYPTIC.INI
```

Useful options of this runtime:

| Option | Meaning |
| --- | --- |
| `-level <episode> <level>` | Start at that level. Write it with one dash. |
| `-map <file>` | Load a map file. |
| `--ini <file>` | Use another episode `.ini` file. The default is `BLOOD.INI`. |
| `--rff <file>` | Use another main `.rff` archive. |
| `-snd <file>` | Use another sound `.rff` archive. |
| `-art <file>` | Use another base art file name. |
| `--monsters <n>` | `0` turns monsters off. `1` or more turns them on and sets the skill to `n` minus one. |
| `-nodudes` | No monsters. |
| `-s <n>` | Difficulty, 0 to 4. |
| `-name <name>` or `-pname <name>` | Your player name. |
| `-nodemo` | Do not play demos. |
| `-quick` | Skip the intro screens. `--skip-cinema` does the same. |
| `-cfg <file>` | Use another settings file. |
| `-g <file>`, `-j <folder>` | Load an extra data file, or search an extra folder. |

**Blood is strict about options.** If it is given an option it does not know,
it stops with an "Invalid argument" error. Options that belong to other games,
for example `-nosound`, `--level`, `--volume` or `--con`, cannot be used with
Blood.

Set the video mode in the game's own options menu.

## Multiplayer

Blood uses its own network system. One player starts a server for a number of
players, and the others connect to it.

```sh
# Server for 2 players
nnbuild --game blood --skip-menu -server 2

# Client
nnbuild --game blood --skip-menu -client 192.0.2.10
```

| Option | Meaning |
| --- | --- |
| `-server <players>` | Start a server for that many players, 1 to 8. |
| `-client <host>` | Connect to a server. |
| `-port <n>` | The server's port. The default is 23513. |
| `-clientport <n>` | The local port a client uses. |
| `-netretry` | Client: keep trying to connect. Hold Escape to stop. |
| `-auto` | Start the network game without waiting in the menu. Also skips the intro. |
| `-mp_mode <0-2>` | Game mode: 0 co-op, 1 Bloodbath, 2 teams. |
| `-mp_level <episode> <level>` | Level for the match. |
| `-mp_map <file>` | Map file for the match. |
| `-mp_diff <0-4>` | Difficulty. |
| `-mp_dudes <0-2>` | Monsters: 0 none, 1 spawn, 2 respawn. |
| `-mp_weaps <0-3>` | Weapons: 0 do not respawn, 1 permanent, 2 respawn, 3 respawn with markers. |
| `-mp_items <0-2>` | Items: 0 do not respawn, 1 respawn, 2 respawn with markers. |
| `-mp_score <1-255>` | Score or lives limit. |
| `-mp_time <1-60>` | Time limit. |

Further `-mp_` options are listed in
[Launch options](../reference/command-line.md#blood).

Lobby options:

| Option | Meaning |
| --- | --- |
| `--advertise <address>` | Server: list this game on the lobby server at `address`. |
| `--lobbylist <address>` | Write the games listed on that lobby server to the log. |
| `--joinlobby <address>` | Connect to the first game listed on that lobby server. `-client` is not needed. |
| `--netfile <file>` | Server: send this file to players who connect. Can be given up to eight times. |

```sh
# Server listed on a lobby server, sharing a mod file
nnbuild --game blood --skip-menu -server 4 \
    --advertise lobby.example.org --netfile mymod.pk3

# Join whatever that lobby server lists first
nnbuild --game blood --skip-menu --joinlobby lobby.example.org
```

Write the lobby options in full. Blood does not know their short forms, such
as `-ad` or `-jl`, and stops when it is given one.

A player who receives a file with `--netfile` gets it written into the folder
the game runs from. Files of type `.grp`, `.zip`, `.pk3`, `.pk4` and `.rff`
are loaded at once. A file the player already has is not sent again.

## Known limits

- Unknown options stop the game, as described above.
- The cutscene movies of the game are not played.
- Maps that depend on the "modern map" extensions of other Blood ports are not
  supported.
- The video mode is not set from the command line.
- No dedicated server console and no host commands such as kick or map
  rotation.
- Blood does not take part in
  [playing one game's levels in another](overview.md#playing-one-games-levels-in-another).
