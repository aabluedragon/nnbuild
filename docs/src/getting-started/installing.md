# Installing

NNBuild is a program. It does not contain any game. To play, you need NNBuild
plus the data files from your own copy of each game.

Getting the program is covered on the [Downloads](../downloads.md) page. This
page explains where the game data goes, how to point NNBuild at it, and where
NNBuild keeps its own files.

The program file is named `nnbuild` (`nnbuild.exe` on Windows). The
examples on this site use that name.

## What you need

- The NNBuild program. The download also contains a file named `eduke32.dat`
  next to the program. Keep it there. On macOS it is inside the app bundle.
- The data files of each game you want to play. A data file is the large
  archive that holds a game's levels, art and sounds.

| Game | Main data file |
| --- | --- |
| Duke Nukem 3D | `DUKE3D.GRP` |
| Duke Nukem 3D: 20th Anniversary World Tour | `DUKE3D.GRP` plus the loose files installed next to it |
| Ion Fury | `fury.grp` |
| NAM | `NAM.GRP` |
| NAPALM | `NAPALM.GRP` |
| WWII GI | `WW2GI.GRP` |
| Shadow Warrior | `SW.GRP` |
| Blood | `BLOOD.RFF` and the rest of the Blood folder |
| Redneck Rampage | `REDNECK.GRP` |
| Exhumed / PowerSlave | `STUFF.DAT` |

Each game has its own page with the exact files and versions NNBuild
recognises. Start from [Supported games](../games/overview.md).

## Where NNBuild looks first

When it starts, NNBuild checks two places for each game:

1. the folder you started it from, and
2. a subfolder of that folder named after the game.

| Game | Subfolder | File it looks for |
| --- | --- | --- |
| Duke Nukem 3D | `duke3d` | `DUKE3D.GRP` or `duke3d.grp` |
| World Tour | `worldtour`, `world-tour` or `duke3d_wt` | see below |
| Shadow Warrior | `sw` | `SW.GRP`, `Sw.grp` or `sw.grp` |
| Blood | `blood` | `BLOOD.RFF` or `blood.rff` |
| Redneck Rampage | `rr` | `REDNECK.GRP` or `redneck.grp` |
| NAM | `nam` | `NAM.GRP` or `nam.grp` |
| NAPALM | `napalm` | `NAPALM.GRP` or `napalm.grp` |
| WWII GI | `ww2gi` | `WW2GI.GRP` or `ww2gi.grp` |
| Exhumed / PowerSlave | `exhumed` | `STUFF.DAT` or `stuff.dat` |
| Ion Fury | `ionfury` | `fury.grp` or `FURY.GRP` |

On systems where upper and lower case are different letters in file names
(Linux, and some macOS disks), use one of the spellings in the table.

A folder set up for three games looks like this:

```text
nnbuild/
  nnbuild
  duke3d/
    DUKE3D.GRP
  sw/
    SW.GRP
  blood/
    BLOOD.RFF
    (the other Blood files)
```

"The folder you started it from" is the current folder of the program, also
called the working directory. When you double-click the program it is normally
the program's own folder. In a terminal it is the folder you are in when you
type the command.

A folder counts as **World Tour** only when it holds all of these files:
`DUKE3D.GRP`, `GAME.CON`, `DEFS.CON`, `USER.CON`, `FLAMETHROWER.CON`,
`FIREFLYTROOPER.CON`, `EPISODE5BOSS.CON`, `TILES020.ART` and `maps/E5L1.map`.

Once a game is chosen, NNBuild switches into the folder where it found that
game's data and runs the game from there. What happens when more than one game
is found is described in [First launch](first-launch.md).

## Pointing NNBuild at data somewhere else

You do not have to copy anything. These launch options tell NNBuild where the
data is. All of them also let NNBuild work out which game you mean.

| Option | Use it for |
| --- | --- |
| `--grp <file>` or `-g <file>` | A data file: the main data file of a game, or a mod pack to load on top of it. |
| `--gamegrp <file>` | The main data file, when you want to say exactly which one. Use it to start an official add-on. |
| `--game-dir <folder>` | The folder that holds the game's data. |
| `-j <folder>` | An extra folder to search for files. |

```sh
nnbuild --grp ~/games/duke3d/DUKE3D.GRP
nnbuild --game sw --grp ~/games/sw/SW.GRP
nnbuild --game blood --game-dir ~/games/blood
```

```text
nnbuild.exe --grp C:\Games\Duke3D\DUKE3D.GRP
```

Give full paths, as in the examples. NNBuild changes folder before the game
starts, so a path written relative to where you typed the command may no longer
be found.

The full list of options is in [Launch options](../reference/command-line.md).

## Installed copies that are found automatically

The Duke Nukem 3D family and Shadow Warrior also search the usual install
locations of the store versions. This search runs after the game has been
chosen, so it is most useful when you start NNBuild with `--game`:

```sh
nnbuild --game duke
```

The locations are listed per system below. If your copy is somewhere else, use
one of the options above.

## Windows

- **Installed copies.** NNBuild reads the install location from the Windows
  registry for these releases: Duke Nukem 3D (Megaton Edition and World Tour on
  Steam, the 3D Realms Anthology, and the GOG.com and ZOOM Platform Atomic
  Edition), NAM (Steam, GOG.com), WWII GI (Steam, GOG.com), Ion Fury (Steam,
  GOG.com) and Shadow Warrior (Classic Redux and Classic on Steam, GOG.com).
- **Settings.** By default the settings files are written into the folder the
  game runs from. That is the folder NNBuild switched into, such as `duke3d`.
  To keep them in your user profile instead, create an empty file named
  `user_profiles_enabled` in that folder. The settings then go to a folder
  under `%APPDATA%`, named as in the [table below](#files-nnbuild-writes).

## Linux

- **Installed copies.** For the Duke Nukem 3D family NNBuild searches Steam
  (`~/.steam/steam` and the library folders listed there), the GOG.com folders
  `~/GOG Games/Duke Nukem 3D Atomic Edition` and `~/GOG Games/ION Fury`, and
  the system folders `/usr/share/games/jfduke3d`,
  `/usr/local/share/games/jfduke3d`, `/usr/share/games/eduke32` and
  `/usr/local/share/games/eduke32`. For Shadow Warrior it searches Steam and
  the GOG.com folders `~/GOG Games/Shadow Warrior Classic Redux` and
  `~/GOG Games/Shadow Warrior Classic Complete`.
- **Settings.** Settings are kept in a folder under `~/.config`, one per group
  of games (see the [table below](#files-nnbuild-writes)). NNBuild creates the
  folder the first time it is needed. Game data placed in that folder is found
  as well.
- **Keeping everything in one folder.** Start with `-usecwd`, or create an
  empty file named `user_profiles_disabled` in the folder the game runs from.
  Settings are then read from and written to that folder. `-usecwd` also
  skips the search of installed copies.

## macOS

- **Installed copies.** For the Duke Nukem 3D family NNBuild searches Steam
  under Application Support, the GOG.com and ZOOM Platform app bundles in the
  Applications folders, and the folders `JFDuke3D` and `EDuke32` under
  Application Support. The folder that contains the program is searched too.
- **Settings.** As on Linux, settings are kept under `.config` in your home
  folder, and `-usecwd` or a `user_profiles_disabled` file keeps them in the
  folder the game runs from.
- **Log files.** Shadow Warrior, Blood, Redneck Rampage and Exhumed write
  their log to `~/Library/Logs`. With `-usecwd` the log stays in the folder the
  game runs from.

## Files NNBuild writes

One NNBuild program contains several game engines, and each keeps its own
settings.

| Games | Settings folder (Linux, macOS) | Folder under `%APPDATA%` (Windows, optional) | Settings file | Log file |
| --- | --- | --- | --- | --- |
| Duke Nukem 3D, World Tour, Ion Fury, NAM, NAPALM, WWII GI | `~/.config/nnbuild` | `NNBuild` | `nnbuild.cfg` | `nnbuild.log` |
| Shadow Warrior | `~/.config/voidsw` | `VoidSW` | `voidsw.cfg` | `voidsw.log` |
| Blood | `~/.config/notblood` | `NotBlood` | `notblood.cfg` | `notblood.log` |
| Redneck Rampage | `~/.config/rednukem` | `Rednukem` | `rednukem.cfg` | `rednukem.log` |
| Exhumed / PowerSlave | `~/.config/kexhumed` | `Kexhumed` | `kexhumed.cfg` | `kexhumed.log` |

- The Duke Nukem 3D family also writes `nnbuild_settings.cfg` next to its
  settings file. It holds key bindings and console settings.
- The log file is a plain text record of one run. It is written to the folder
  the game runs from, except on macOS as noted above. Look there first when
  something does not start.
- `grpfiles.cache` remembers which data files were found, so later starts are
  faster. It is safe to delete.
- `netcache` is a folder for maps and mod packs downloaded from servers you
  joined.
- `banlist.txt` is the ban list of a server you host.
- `autojs` holds script mods. It lives in the folder you start NNBuild from.
  See [Modding quick start](../modding/quick-start.md).

## Next step

Continue with [First launch](first-launch.md).
