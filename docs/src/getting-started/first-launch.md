# First launch

This page describes what NNBuild does between the moment you start it and the
moment a game appears, and how to skip the steps you do not need.

It assumes your game data is in place. If it is not, read
[Installing](installing.md) first.

## What happens when you start NNBuild

1. **A game is chosen.** One NNBuild program can run several games. It decides
   which one from the launch options you gave and from the data files it finds.
2. **If it cannot decide, it asks.** When data for more than one game is found
   and nothing else settles the choice, NNBuild shows a short text prompt, the
   game picker.
3. **NNBuild switches into the game's folder.** The game runs from the folder
   where its data was found.
4. **The startup window may open.** This is a small window with video and
   input settings. It appears before the game itself.
5. **The game starts** and its settings are saved for next time.

## How the game is chosen

NNBuild goes through these rules in order and stops at the first one that
gives an answer.

1. **You named it.** `--game <name>` (short form `-gm <name>`) always wins.
   The names are listed in [Supported games](../games/overview.md).

   ```sh
   vibebuild32 --game sw
   ```

2. **You pointed at data.** If you gave a data file or folder, NNBuild works
   out the game from it. It looks at `--grp`, `--gamegrp`, `--rff`, `--ini`,
   `--game-dir`, `-j`, and at any file name given without an option, from left
   to right. A file is recognised by its size and checksum when it is a known
   release, otherwise by its name (for example `SW.GRP` or `STUFF.DAT`). A
   folder is recognised when it holds the main data file of exactly one game.

   ```sh
   vibebuild32 --grp ~/games/sw/SW.GRP
   ```

3. **Only one game is installed.** If the [places NNBuild looks
   first](installing.md#where-nnbuild-looks-first) hold data for a single game,
   that game starts.
4. **Several games are installed.** The game picker asks which one you want.
5. **Nothing was found.** NNBuild starts Duke Nukem 3D, which then runs its own
   search for installed copies and reports an error if it finds none.

If your copy of NNBuild was built without one of the games, choosing that game
starts Duke Nukem 3D instead.

## The game picker

The picker is a text prompt in the terminal NNBuild was started from. It lists
the games whose data was found:

```text
Select a game:
  1) Duke Nukem 3D
  2) Shadow Warrior
  3) Blood
Choice [1]:
```

Type the number and press Enter. Pressing Enter alone picks the first entry.

NNBuild only asks when a person can answer. When it was not started from a
terminal, for example by double-click, or when its input comes from another
program, it does not ask and uses the first entry.

Games are always listed in this order: Duke Nukem 3D, Duke Nukem 3D: World
Tour, Shadow Warrior, Blood, Redneck Rampage, NAM, Napalm, World War II GI,
Kexhumed (the name NNBuild uses for Exhumed / PowerSlave), Ion Fury.

## The startup window

The startup window belongs to the Duke Nukem 3D family. It lets you set:

- the video mode and whether to use the full screen,
- which input devices to use (mouse, joystick),
- which **game** to run, when several recognised data files were found (for
  example the Atomic Edition and an add-on),
- a custom game content directory,
- whether to **always show this window at startup**.

Press **Start** to continue or **Cancel** to quit.

The window opens when there is no settings file yet, when "Always show this
window at startup" is ticked (it is ticked at first), or when you ask for it
with `--menu`. It exists in Windows and macOS builds, and in Linux builds made
with GTK 2 support. Other builds go straight to the game.

For Shadow Warrior, Blood, Redneck Rampage and Exhumed, start with
`--skip-menu`. These games then go straight to their own title screen.

## Skipping the menus

| Option | Effect |
| --- | --- |
| `--game <name>` | No game picker: you already chose. |
| `--skip-menu` or `-sm` | Skips the game picker and the startup window. If several games are installed and you did not choose one, the first in the list above is used. |
| `--no-game-menu` or `-ngm` | Skips only the game picker. |
| `--menu` or `-m` | Shows the startup window even if it would be skipped. |
| `--skip-cinema` or `-sc` | Skips the logos and intro movie. |

A typical command that goes straight into the game:

```sh
vibebuild32 --game duke --skip-menu --skip-cinema
```

In the Duke Nukem 3D family, some options skip the startup window by
themselves, because they only make sense for a direct start: `-net`
(multiplayer), `--debug` and `--devtools` (script debugging), and starting a
project. `--grp` often does the same. Add `--skip-menu` when you want to be
sure.

## Where settings are stored

Each group of games has its own settings file, written when the game exits.
The file names and folders for every system are in
[Files NNBuild writes](installing.md#files-nnbuild-writes).

In short:

- **Linux and macOS:** a folder under `.config` in your home folder, for
  example `~/.config/vibebuild32/vibebuild32.cfg`.
- **Windows:** the folder the game runs from, unless you opted in to the
  folder under `%APPDATA%`.
- `-usecwd` keeps settings in the folder the game runs from on every system.

For the Duke Nukem 3D family the settings file also holds a player ID that
NNBuild creates on first run. A server uses it to give you your score back when
you rejoin a match, so keep the file if you play online.

To try something without touching your normal settings, name another file:

```sh
vibebuild32 --game duke -cfg ~/games/test.cfg
```

`-cfg` works for the Duke Nukem 3D family, Blood, Redneck Rampage and Exhumed.

## Next step

Read the page for your game under [Supported games](../games/overview.md), or
go to [Playing online](../multiplayer/playing-online.md).
