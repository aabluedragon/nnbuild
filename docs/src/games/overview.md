# Supported games

One NNBuild program runs all the games on this page. You choose a game when
you start the program, with `--game <name>`, or NNBuild works it out from the
data files it finds. See [First launch](../getting-started/first-launch.md).

You need your own copy of each game's data files.
[Installing](../getting-started/installing.md) explains where to put them.

## The games

| Game | `--game` name | Main data file | Status |
| --- | --- | --- | --- |
| [Duke Nukem 3D](duke-nukem-3d.md) | `duke` | `DUKE3D.GRP` | The main game. Every NNBuild feature is available. |
| [Duke Nukem 3D: 20th Anniversary World Tour](duke-nukem-3d.md#world-tour) | `worldtour` | `DUKE3D.GRP` and the loose World Tour files | Runs on the Duke Nukem 3D runtime, with World Tour's own scripts and levels. |
| [Ion Fury](ion-fury.md) | `ionfury` | `fury.grp` | Runs on the Duke Nukem 3D runtime. |
| [NAM](nam-and-wwii-gi.md) | `nam` | `NAM.GRP` | Runs on the Duke Nukem 3D runtime. |
| [NAPALM](nam-and-wwii-gi.md) | `napalm` | `NAPALM.GRP` | Runs on the Duke Nukem 3D runtime. |
| [WWII GI](nam-and-wwii-gi.md) | `ww2gi` | `WW2GI.GRP` | Runs on the Duke Nukem 3D runtime. The Platoon Leader add-on is recognised. |
| [Shadow Warrior](shadow-warrior.md) | `sw` | `SW.GRP` | Its own runtime. Single player, multiplayer with a server list, and a limited set of script features. |
| [Blood](blood.md) | `blood` | `BLOOD.RFF` | Its own runtime. Single player, and multiplayer with lobby options on the command line. |
| [Redneck Rampage](redneck-rampage.md) | `rr` | `REDNECK.GRP` | Its own runtime. Single player, and multiplayer with lobby options on the command line. |
| [Exhumed / PowerSlave](exhumed-powerslave.md) | `exhumed` | `STUFF.DAT` | Its own runtime. Single player, multiplayer, and KEX mode for the remaster's data. |

A **runtime** is the part of the program that runs one game or one family of
games. NNBuild contains five of them. The Duke Nukem 3D runtime carries almost
all of the new features, and the games that run on it share them.

If your copy of NNBuild was built without one of the separate runtimes,
choosing that game starts Duke Nukem 3D instead.

## Other names you can type

`--game` accepts several spellings for each game. Upper and lower case do not
matter.

| Game | Accepted names |
| --- | --- |
| Duke Nukem 3D | `duke`, `duke3d`, `dukenukem3d` |
| World Tour | `worldtour`, `world-tour`, `wt`, `duke3dwt`, `duke-wt`, `duke20`, `duke-20`, `duke3d-world-tour`, `twentieth`, `20th` |
| Ion Fury | `ionfury`, `ion-fury`, `fury`, `aftershock`, `ionfury-aftershock`, `ion-fury-aftershock` |
| NAM | `nam`, `nam3d`, `nam-3d` |
| NAPALM | `napalm`, `napalm3d`, `napalm-3d` |
| WWII GI | `ww2gi`, `wwii`, `wwiigi`, `wwii-gi`, `worldwariigi`, `world-war-ii-gi`, `gi` |
| Shadow Warrior | `sw`, `shadowwarrior`, `shadow-warrior` |
| Blood | `blood` |
| Redneck Rampage | `rr`, `redneck`, `redneckrampage`, `redneck-rampage` |
| Exhumed / PowerSlave | `exhumed`, `kexhumed`, `powerslave`, `power-slave`, `pcexhumed` |

A name NNBuild does not know is ignored, and the game is then chosen as if
`--game` had not been given.

## Which features work in which game

The table lists what each runtime offers. A dash means the feature is not part
of that runtime.

| Feature | Duke Nukem 3D family | Shadow Warrior | Blood | Redneck Rampage | Exhumed |
| --- | --- | --- | --- | --- | --- |
| How a network game is started | `-net` | `-net` | `-server`, `-client` | `-server`, `-connect` | `-net` |
| Host and join from the game's menus | Multiplayer menu | A server list only, see [Shadow Warrior](shadow-warrior.md#multiplayer) | - | - | - |
| List your game on a lobby server (`--advertise`) | Yes | Yes | Yes | Yes | Yes |
| Join the first listed game (`--joinlobby`) | - | Yes | Yes | Yes | Yes |
| Join a match that is already running | Yes | - | - | - | Yes |
| Server without a window | Yes | - | - | Yes | Yes |
| Server console (type commands in the terminal) | Yes | - | - | - | Linux and macOS |
| Kick, ban, map rotation and the other host commands | Yes | - | - | - | - |
| JavaScript and TypeScript mods | Yes | Limited | - | - | - |
| Map editor and projects | Yes | - | - | - | - |
| Control by an AI assistant (MCP) | Yes | - | - | - | - |

The "Duke Nukem 3D family" column covers Duke Nukem 3D, World Tour, Ion Fury,
NAM, NAPALM and WWII GI.

## Playing one game's levels in another

NNBuild can load the levels of one game while another game's runtime is
running. Three options control this. Each takes a game name from the table
above.

| Option | Short form | Meaning |
| --- | --- | --- |
| `--game.levels <name>` | `-gm.lvls <name>` | The game the levels come from. |
| `--game.items <name>` | `-gm.its <name>` | The game whose item pickups to use. |
| `--game.weapons <name>` | `-gm.wpns <name>` | The game whose weapon and ammo pickups to use. |

When an option is left out, it means "the game that is running".

You also have to give the data of the game the levels come from, with `--grp`.
NNBuild sees that the file belongs to a different game than the one running and
uses it only as a source of levels and art.

```sh
vibebuild32 --game sw --grp ~/games/sw/SW.GRP \
    --game.levels duke --grp ~/games/duke3d/DUKE3D.GRP -map E1L1.MAP
```

Two runtimes take part in this:

- **Shadow Warrior** can load levels from Duke Nukem 3D, World Tour, NAM,
  NAPALM and WWII GI. See [Shadow Warrior](shadow-warrior.md#playing-duke-nukem-3d-levels).
- **The Duke Nukem 3D runtime** can load Shadow Warrior levels. See
  [Duke Nukem 3D](duke-nukem-3d.md#playing-other-games-levels).

Blood, Redneck Rampage and Exhumed do not use these options.
