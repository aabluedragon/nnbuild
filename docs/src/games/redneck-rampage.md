# Redneck Rampage

Redneck Rampage has its own runtime inside NNBuild. It plays Redneck Rampage
and Redneck Rampage: Rides Again, and has multiplayer with lobby options on
the command line. The map editor, script mods and the host console commands of
the Duke Nukem 3D runtime are not available here.

| | |
| --- | --- |
| `--game` name | `rr` (also `redneck`, `redneckrampage`, `redneck-rampage`) |
| Main data file | `REDNECK.GRP` |
| Folder NNBuild looks in first | `rr` |
| Settings and log | `rednukem.cfg`, `rednukem.log` |

## Data files

You need `REDNECK.GRP` from your own copy of the game. NNBuild recognises two
releases by size and checksum:

| Release | Data file |
| --- | --- |
| Redneck Rampage | `REDNECK.GRP` |
| Redneck Rampage: Rides Again | `REDNECK.GRP` of Rides Again |

Both games name their data file `REDNECK.GRP`. Keep them in separate folders
and point NNBuild at the one you want.

How the data is found:

1. At start, NNBuild looks for `REDNECK.GRP` (or `redneck.grp`) in the folder
   you started it from and in its `rr` subfolder.
2. A data file that you pass on the command line selects Redneck Rampage, by
   its checksum or by one of these names: `REDNECK.GRP`, `RR.GRP`, `RRRA.GRP`,
   `RIDES.GRP`.

## Starting the game

Start Redneck Rampage with `--skip-menu`. It then goes straight to its own
title screen.

```sh
# Data in the rr subfolder
vibebuild32 --game rr --skip-menu

# Data somewhere else
vibebuild32 --game rr --grp ~/games/redneck/REDNECK.GRP --skip-menu

# Rides Again
vibebuild32 --game rr --grp ~/games/rides/REDNECK.GRP --skip-menu

# Episode 1, level 2, skill 3
vibebuild32 --game rr --skip-menu --volume 1 --level 2 --skill 3

# A map file of your own
vibebuild32 --game rr --skip-menu -map ~/maps/barn.map
```

Useful options of this runtime:

| Option | Meaning |
| --- | --- |
| `--volume <n>`, `--level <n>` | Start at that episode and level. |
| `--skill <n>` | Skill level. |
| `-map <file>` | Load a map file. |
| `--monsters 0` | No enemies. |
| `-name <name>` | Your player name. |
| `-cfg <file>` | Use another settings file. |
| `--con <file>` | Load another main script. |
| `--def <file>` | Load another definitions file. |
| `-ns`, `-nm` | No sound, no music. |

Set the video mode in the game's own options menu.

## Multiplayer

Redneck Rampage uses its own network system. One player starts a server and
the others connect to it.

```sh
# Server
vibebuild32 --game rr -server

# Client
vibebuild32 --game rr -connect 192.0.2.10
```

| Option | Meaning |
| --- | --- |
| `-server` | Start a server. |
| `-connect <host>` or `-connect <host>:<port>` | Connect to a server. |
| `-port <n>` | The port to host on or connect to. The default is 23513. |
| `-password <text>` | The password of the server. |

`-server` and `-connect` skip the startup window and the intro by themselves.

Lobby and server options:

| Option | Meaning |
| --- | --- |
| `--advertise <address>` | Start a server and list it on the lobby server at `address`. |
| `--lobbylist <address>` | Write the games listed on that lobby server to the log. |
| `--joinlobby <address>` | Connect to the first game listed on that lobby server. |
| `--netfile <file>` | Server: send this file to players who connect. Can be given up to eight times. |
| `--headless` or `--dedicated` | Start a server without a window. It waits for players and starts the match by itself. |
| `--nplayers <n>` | How many players the server waits for before it starts the match. The default is 2. |
| `--wait` | Server without a window: do not start the match automatically. |
| `--startnow` | Server with a window: start the match automatically as well. |

Each of these options has a short form: `-ad`, `-ll`, `-jl`, `-nf`, `-hl` or
`-dd`, `-np`, `-w` and `-sn`, in the order of the table.

```sh
# A server without a window, listed on a lobby server, starting when 3 players are in
vibebuild32 --game rr --headless --advertise lobby.example.org --nplayers 3

# Join whatever that lobby server lists first
vibebuild32 --game rr --joinlobby lobby.example.org
```

A player who receives a file with `--netfile` gets it written into the folder
the game runs from. Files of type `.grp`, `.zip`, `.pk3`, `.pk4` and `.rff`
are loaded at once. A file the player already has is not sent again.

The game's console has its own `connect`, `disconnect`, `listplayers`, `kick`,
`kickban` and `password` commands.

## Known limits

- A server without a window has no console in the terminal.
- `--monsters` can only turn enemies off in this game. Use `--skill` to set
  the skill.
- The video mode is not set from the command line.
- `-net` is not used by this game. It stops with a message that points to
  `-server` and `connect`.
- Duke Nukem 64 is not included, although the code this runtime comes from
  supports it.
- Redneck Rampage does not take part in
  [playing one game's levels in another](overview.md#playing-one-games-levels-in-another).
