# Exhumed / PowerSlave

The DOS game known as PowerSlave in North America and Exhumed in Europe has
its own runtime inside NNBuild. NNBuild's name for this runtime is
**Kexhumed**, and that is the name you see in the game picker and the window
title.

The runtime plays the classic game, has multiplayer, and has a
[KEX mode](#kex-mode) that uses the data of the *PowerSlave Exhumed* remaster.
The map editor, script mods and the host console commands of the Duke Nukem 3D
runtime are not available here.

| | |
| --- | --- |
| `--game` name | `exhumed` (also `kexhumed`, `powerslave`, `power-slave`, `pcexhumed`) |
| Main data file | `STUFF.DAT` |
| Folder NNBuild looks in first | `exhumed` |
| Settings and log | `kexhumed.cfg`, `kexhumed.log` |

## Data files

You need `STUFF.DAT` from your own copy of the game. Leave it in the game's
folder, next to the files it was installed with. NNBuild recognises these
releases by size and checksum:

| Release | Kind |
| --- | --- |
| PowerSlave | Full game |
| Exhumed | Full game |
| PowerSlave Demo | Demo |
| Exhumed Demo | Demo |

How the data is found:

1. At start, NNBuild looks for `STUFF.DAT` (or `stuff.dat`) in the folder you
   started it from and in its `exhumed` subfolder.
2. A data file that you pass on the command line selects this game, by its
   checksum or by the name `STUFF.DAT`.

## Starting the game

Start with `--skip-menu`. The game then goes straight to its own title
screens.

```sh
# Data in the exhumed subfolder
vibebuild32 --game exhumed --skip-menu

# Data somewhere else
vibebuild32 --game exhumed --grp ~/games/powerslave/STUFF.DAT --skip-menu

# Jump straight to level 3, without the intro
vibebuild32 --game exhumed --skip-menu -quick -3

# A map file of your own
vibebuild32 --game exhumed --skip-menu -map ~/maps/tomb.map
```

Useful options of this runtime:

| Option | Meaning |
| --- | --- |
| `-<n>` | Jump to level `n`, for example `-3`. |
| `-map <file>` | Load a map file. |
| `-quick` | Skip the intro screens and movie. `--skip-cinema` also skips them. |
| `-nocreatures` | No enemies. |
| `-nosound`, `-nomusic` | No sound effects, no music. |
| `-record`, `-playback` | Record a demo to `DATA.VCR`, or play that file back. |
| `-name <name>` | Your player name in a network game. |
| `-color <0-7>` | Your player colour in a network game. |
| `-cfg <file>` | Use another settings file. |
| `-g <file>`, `-j <folder>` | Load an extra data file, or search an extra folder. |

This runtime does not understand `--level`, `--volume`, `--monsters` or the
video options. Use the options in the table, and set the video mode in the
game's own menu.

## Multiplayer

The multiplayer of this runtime is new code written for NNBuild. It uses the
same `-net` syntax as Duke Nukem 3D and holds up to 8 players.

```sh
# Host a 2-player game on UDP port 23513
vibebuild32 --game exhumed --skip-menu -name Host -net /p23513 /n0:2

# Join it
vibebuild32 --game exhumed --skip-menu -name Guest -net /n0 192.0.2.10:23513
```

`-net` and everything after it must come last on the command line. The parts
of the `-net` block are explained in
[Launch options](../reference/command-line.md#the--net-block).

Options written before `-net`:

| Option | Short form | Meaning |
| --- | --- | --- |
| `--advertise <address>` | `-ad` | Host: list this game on the lobby server at `address`. |
| `--lobbylist <address>` | `-ll` | Write the games listed on that lobby server to the log, then continue starting. |
| `--joinlobby <address>` | `-jl` | Join the first game listed on that lobby server. No `-net` block is needed. |
| `--netfile <file>` | `-nf` | Host: send this file to players before the level loads. |
| `--dedicated` or `--headless` | `-dd`, `-hl` | Run a server without a window. On Linux and macOS it reads console commands typed in its terminal. |
| `--nplayers <n>` | `-np` | Start once `n` players are connected. The default is 2. |
| `--startnow` | `-sn` | Start with the players who are present. The other slots stay reserved. |
| `--latejoin` | `-lj` | Host: let players enter a reserved slot while the level is running. Use it together with `--startnow`. |
| `--wait` | `-w` | Server without a window: do not start automatically. |

A server without a window starts the match by itself unless `--wait` is given.

```sh
# A server with four slots. It starts when one player has joined.
# Players can keep joining while the level runs.
vibebuild32 --game exhumed --skip-menu -name Server \
    --dedicated --startnow --nplayers 2 --latejoin -net /p23513 /n0:4

# The same server, listed on a lobby server and sharing a mod file
vibebuild32 --game exhumed --skip-menu -name Server \
    --dedicated --advertise lobby.example.org --netfile mymod.grp -net /n0:4

# Join whatever that lobby server lists first
vibebuild32 --game exhumed --skip-menu -name Guest --joinlobby lobby.example.org
```

**Chat.** The console command `say <text>` sends a chat message to the other
players. On a server without a window (Linux and macOS), type it in the
terminal:

```text
say The next level starts in one minute
```

## KEX mode

*PowerSlave Exhumed*, the 2022 remaster by Nightdive Studios, is a different
program with its own data. KEX mode lets this runtime use that data.

```sh
vibebuild32 --game exhumed --skip-menu \
    -j ~/games/powerslave -kex "/home/you/games/PowerSlave Exhumed"
```

- `-kex <folder>` names the folder of your own PowerSlave Exhumed install.
  NNBuild loads the file `PowerslaveEX.kpf` from it and also searches that
  folder for other files.
- The remaster's levels are not Build maps. They have to be converted to
  `.map` files first, with the converter script that is part of NNBuild's
  source code (`source/exhumed/tools/kexmap.py`). Put the converted files in a
  folder NNBuild searches, for example with `-j`.
- You still need `STUFF.DAT` from the DOS game.
- In KEX mode, **New Game** starts the remaster's campaign. The level comes
  from the remaster's level-select screen, or from the map named in the file
  `kexstart.txt` that the converter writes.
- KEX mode has its own default key bindings.

To start one converted level directly, add `-map`:

```sh
vibebuild32 --game exhumed --skip-menu \
    -j ~/games/powerslave -kex "/home/you/games/PowerSlave Exhumed" -map THOTH.MAP
```

## Known limits

- The video mode is not set from the command line.
- File names given without an option are ignored. Use `-g`, `-map` and `-j`.
- There is no list of games in the menus. Use `--joinlobby`.
- Players can join a running match only when the host used `--latejoin` and a
  slot is free.
- KEX mode needs the converter step described above.
- Exhumed does not take part in
  [playing one game's levels in another](overview.md#playing-one-games-levels-in-another).
