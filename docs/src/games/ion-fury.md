# Ion Fury

Ion Fury runs on the same runtime as Duke Nukem 3D. NNBuild switches that
runtime into Ion Fury mode when you choose this game.

| | |
| --- | --- |
| `--game` name | `ionfury` (also `ion-fury`, `fury`, `aftershock`) |
| Main data file | `fury.grp` |
| Folder NNBuild looks in first | `ionfury` |
| Settings and log | `vibebuild32.cfg`, `vibebuild32.log` (shared with Duke Nukem 3D) |

## Data files

You need `fury.grp` from your own copy of Ion Fury. Leave it in the game's own
folder, next to the files it was installed with.

NNBuild has one release built in: **Ion Fury: Aftershock**. It is recognised by
the size and checksum of its `fury.grp`. Other releases are recognised when a
`.grpinfo` file that describes them is in a search folder. Ion Fury installs
normally include one, named `fury.grpinfo`. For that reason, point NNBuild at
the game's own folder rather than at a copy of `fury.grp` alone.

How the data is found:

1. At start, NNBuild looks for `fury.grp` (or `FURY.GRP`) in the folder you
   started it from and in its `ionfury` subfolder.
2. A file named `fury.grp` that you pass on the command line selects Ion Fury.
3. Once Ion Fury is chosen, the game also searches the usual Steam and GOG.com
   install locations of Ion Fury.

## Starting the game

```sh
# Data in the ionfury subfolder
vibebuild32 --game ionfury

# The game's own folder
vibebuild32 --game ionfury --game-dir "/home/you/games/Ion Fury"

# Naming the data file selects Ion Fury by itself
vibebuild32 --grp "/home/you/games/Ion Fury/fury.grp"

# No startup window and no intro
vibebuild32 --game ionfury --game-dir "/home/you/games/Ion Fury" --skip-menu --skip-cinema
```

Ion Fury uses its own main script (`scripts/main.con`) and definitions file
(`ashock.def`). NNBuild selects both for you.

## What you can do in this game

Because Ion Fury uses the Duke Nukem 3D runtime, the launch options of that
runtime apply. See [Launch options](../reference/command-line.md).

- **Multiplayer from the command line.** `-net`, `--latejoin`, `--startnow`,
  `--dedicated` and `--advertise` are available, and so are the host console
  commands. See [Hosting a match](../multiplayer/hosting.md).
- **Mods in JavaScript or TypeScript.** See [Modding](../modding/overview.md).

## Playing a Duke Nukem 3D level with Ion Fury

You can give a Duke Nukem 3D data file as a second data source. NNBuild sees
that it belongs to another game and uses it only for its levels and art. This
example is the one printed by the program's own help:

```sh
vibebuild32 --game ionfury --game-dir "/home/you/games/Ion Fury" \
    --grp ~/games/duke3d/DUKE3D.GRP -map E3L6
```

It starts level E3L6 of Duke Nukem 3D with Ion Fury as the game.

## Known limits

- **No Multiplayer or Map Editor entry in the title menu.** Ion Fury's title
  menu offers New Game, Continue, Options, Read Me and Quit. Start network
  games with launch options.
- **The map editor has no Ion Fury target.** It can target Duke Nukem 3D,
  NAM, NAPALM and WWII GI.
- **Only Aftershock is built in.** Other releases depend on a `.grpinfo` file,
  as described above.
