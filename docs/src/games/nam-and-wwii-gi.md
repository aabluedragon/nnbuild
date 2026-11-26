# NAM and WWII GI

NAM, NAPALM, WWII GI and the WWII GI add-on Platoon Leader all run on the same
runtime as Duke Nukem 3D. NNBuild switches that runtime into the right mode
when you choose one of them.

| Game | `--game` name | Main data file | Folder NNBuild looks in first |
| --- | --- | --- | --- |
| NAM | `nam` | `NAM.GRP` | `nam` |
| NAPALM | `napalm` | `NAPALM.GRP` | `napalm` |
| WWII GI | `ww2gi` | `WW2GI.GRP` | `ww2gi` |
| Platoon Leader | `ww2gi` | `PLATOONL.GRP`, together with `WW2GI.GRP` | `ww2gi` |

Settings and the log are shared with Duke Nukem 3D: `vibebuild32.cfg` and
`vibebuild32.log`.

## Data files

NNBuild recognises one release of each game by the size and checksum of its
data file:

| Data file | Recognised as |
| --- | --- |
| `NAM.GRP` | NAM |
| `NAPALM.GRP` | NAPALM |
| `WW2GI.GRP` | WWII GI |
| `PLATOONL.GRP` | Platoon Leader, an add-on that needs `WW2GI.GRP` |

How the data is found:

1. At start, NNBuild looks for the file names in the first table, in upper or
   lower case, in the folder you started it from and in the game's subfolder.
2. A data file that you pass on the command line selects the game, by its
   checksum or by its name.
3. Once the game is chosen, it also searches the Steam and GOG.com install
   locations of NAM and WWII GI.

Platoon Leader is offered only when `WW2GI.GRP` is found as well. Keep the two
files in the same folder.

## Starting the games

```sh
# Data in the nam, napalm or ww2gi subfolder
vibebuild32 --game nam
vibebuild32 --game napalm
vibebuild32 --game ww2gi

# Naming the data file selects the game by itself
vibebuild32 --grp ~/games/nam/NAM.GRP
vibebuild32 --grp ~/games/ww2gi/WW2GI.GRP

# Platoon Leader
vibebuild32 --gamegrp ~/games/ww2gi/PLATOONL.GRP

# No startup window and no intro
vibebuild32 --game ww2gi --skip-menu --skip-cinema
```

The older switches `-nam`, `-napalm` and `-ww2gi` still exist. They change the
mode of the runtime but do not help NNBuild find the data, so prefer `--game`.

## What you can do in these games

These games use the Duke Nukem 3D runtime, so its features and launch options
apply. See [Duke Nukem 3D](duke-nukem-3d.md#what-you-can-do-in-this-game) and
[Launch options](../reference/command-line.md).

- **Multiplayer.** The title menu has the same **Multiplayer** entry, and the
  same launch options and host console commands are available. The deathmatch
  modes are named Gruntmatch in NAM and NAPALM, and GI Match in WWII GI.
- **Mods in JavaScript or TypeScript.** See [Modding](../modding/overview.md).
- **Map editor.** NAM, NAPALM and WWII GI are target games of the editor. See
  [Map editor](../editor/overview.md).

## Known limits

- Each player in a network game needs their own copy of the same data file.
  Base game data is never sent to other players.
- NAM and NAPALM can share files: when the script or definitions file of one
  is missing, the runtime uses the other's.
- Platoon Leader cannot start without `WW2GI.GRP`.
