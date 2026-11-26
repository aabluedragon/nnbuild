# Console commands

This page lists the console commands and settings that NNBuild adds. The
commands the games already had, such as `bind`, `god` or `vidmode`, are still
there and are not repeated here.

Unless a section says otherwise, everything on this page belongs to the Duke
Nukem 3D runtime: Duke Nukem 3D, World Tour, Ion Fury, NAM, NAPALM and WWII
GI.

## How to enter commands

- **In the game:** open the console and type the command.
- **On a dedicated server:** type the command in the terminal the server runs
  in, and press Enter.
- **At startup:** give the command with `--cmd`, or put commands in a text file
  and name it with `--cmd-file`. See
  [Launch options](command-line.md#sound-settings-and-startup-commands).

  ```sh
  nnbuild --game duke --dedicated --startnow \
      --cmd "sv_maprotation 1" -net /n0:8
  ```

- **From a script or an AI assistant:** see
  [Server scripts](../modding/server-scripts.md) and
  [AI assistants and MCP](../editor/ai-and-mcp.md).

A **setting** (also called a cvar) is a named value. Type its name alone to
see the value. Type the name and a new value to change it.

Put quotes around a file name or path that contains spaces.

## Who may run a command

Many commands are for the **host**: the computer that hosts the match. On a
dedicated server that is the server itself. Some commands need the match to be
in **host-and-clients mode**, which is what `-net /n0` starts and what the
Multiplayer menu uses.

| Command or setting | Who may run it | Purpose |
| --- | --- | --- |
| [`listplayers`](#listplayers) | Anyone in a network game | List the players. |
| [`kick`](#kick) | Host | Remove a player. |
| [`ban`](#ban) | Host | Remove a player and block their address. |
| [`unban`](#unban) | Whoever keeps the ban list | Remove a ban. |
| [`listban`](#listban) | Anyone | Show the ban list. |
| [`map`, `changemap`](#map-and-changemap) | Host, for everyone. Otherwise for yourself. | Change level. |
| [`mod`](#mod) | Host, host-and-clients mode | Change mod pack and level. |
| [`nextmap`](#nextmap) | Host | Go to the next map of the rotation. |
| [`mapcycle`](#mapcycle) | Anyone to view, host to change | View or edit the map rotation. |
| [`maplist`](#maplist) | Anyone | View the map rotation. |
| [`sv_maprotation`](#sv_maprotation) | Host | Turn automatic rotation on or off. |
| [`sendfile`](#sendfile) | Host | Send a file to all players. |
| [`monsters`](#monsters) | Host, or yourself in single player | Remove or allow monsters. |
| [`spawn`](#spawn) | Host, or yourself in single player | Add an enemy or item. |
| [`serverlist`](#serverlist) | Anyone | Print the games listed on a lobby server. |
| [`cl_masterserver`](#cl_masterserver) | Each player | Which lobby server the menus use. |
| [`cl_preferipv6`](#cl_preferipv6) | Each player | Prefer IPv6 when joining from the game list. |
| [`skin`](#skin) | Each player | Appear as an enemy in multiplayer. |
| [`cl_mousesmooth`](#cl_mousesmooth) | Each player | Smooth the mouse. |
| [`cl_joysmoothing`](#cl_joysmoothing) | Each player | Smooth the controller. |
| [`cl_swapjoysticks`](#cl_swapjoysticks) | Each player | Swap the two controller sticks. |
| [`wt_commentary`](#wt_commentary) | Each player | World Tour developer commentary. |

## Players

### `listplayers`

```text
listplayers
```

Shows every connected player with slot number, name and network address. The
host is marked. It only works in a network game.

```text
listplayers
Players:
  0: Server  (this machine)  [host]
  1: Maya  192.0.2.44:23530
```

### `kick`

```text
kick <name|slot>
```

Removes a player from the match. Only the host can use it. Give the slot
number shown by `listplayers`, or the name. The start of a name is enough, and
upper and lower case do not matter. The host cannot kick itself.

```text
kick 1
kick maya
```

### `ban`

```text
ban <name|slot|ip>
```

Removes a player and adds their IP address to the ban list. Only the host can
use it. A banned address is refused when it tries to connect.

You can also give an IP address directly. This works before the player
connects, and even before you start hosting.

```text
ban maya
ban 192.0.2.44
```

About the ban list:

- It is saved in the file `banlist.txt` and loaded again the next time you
  host.
- It holds up to 256 entries.
- Bans work on IPv4 addresses. A player who is connected over IPv6 cannot be
  banned. Use `kick` for that player.
- Addresses on your own computer (`127.x.x.x`) are never banned.

`banlist.txt` is a text file with one entry per line: the address, then
optionally the player's name. Lines that start with `#` are ignored.

```text
# NNBuild ban list - one entry per line: <ip> <name>
192.0.2.44 Maya
198.51.100.7
```

### `unban`

```text
unban <ip|name>
```

Removes entries from the ban list and prints how many were removed. Give the
IP address, or the name that was saved with the ban. The start of the name is
enough.

```text
unban 192.0.2.44
unban maya
```

### `listban`

```text
listban
```

Shows the ban list.

## Levels and mod packs

A **pack** is a file with game content: maps, art, sounds or scripts. Packs
can be `.grp`, `.zip`, `.pk3`, `.pk4` or `.ssi` files. In these commands, the
word `base` means "the base game with no pack", and `current` means "the pack
in use now".

A level is written either as episode and level, such as `E1L3`, or as the path
of a map file, such as `maps/arena.map`.

### `map` and `changemap`

```text
map <E#L#|map path> [pack|base]
changemap <E#L#|map path> [pack|base]
```

The two names do the same thing.

- **Host of a host-and-clients match:** changes the level for everyone. With
  a second word, it also changes the pack, as described in
  [When the pack changes](#when-the-pack-changes).
- **Single player:** loads that level. With no arguments it lists the map
  files it can find.
- **A player who is not the host:** starts a vote for the level.

```text
map E1L3
map "maps/User Arena.map"
map "maps/User Arena.map" "packs/arena.pk3"
changemap E2L1 base
```

When the level is a map file, the host sends it to the players who do not have
it, and the match goes on without a restart. Files that belong to the map go
with it: a map hack file (`.mhk`), music and map art with the same name as the
map. Players keep them in their `netcache` folder.

These commands are not available with shareware data.

### `mod`

```text
mod <pack|base> [E#L#|map path]
```

Switches the server to another pack and starts a level of it. Without a level,
`E1L1` is used. Only the host of a host-and-clients match can use it.

```text
mod "packs/arena.pk3" E2L1
mod "packs/arena.pk3" "maps/User Arena.map"
mod base E1L1
```

`mod base` goes back to the base game with no pack.

### When the pack changes

A pack change can come from `mod`, from the pack word of `map` or `changemap`,
or from a [`mapcycle`](#mapcycle) entry that names another pack. The same
steps follow in each case.

On the server:

1. The server checks the new pack and the level in a separate process. The
   running match continues meanwhile. The check can take up to a minute.
2. If the check fails, the server prints `Could not activate the requested
   pack and level` and nothing else changes. When the change came from the
   map rotation at the end of a level, the current level is started again.
3. If the check passes, the server tells the players, restarts itself with
   the new pack on the same address and port, and starts the level.

The restarted server keeps the map rotation and its position, the value of
`sv_maprotation`, the game mode, the skill, the frag limit and the other game
rules. Startup commands that had not run yet are run after the restart. The
server accepts players at any time after the restart, as if it had been
started with `--startnow`. Script mods start again from the beginning.

For the players:

1. The game leaves the match and restarts itself about two seconds later. The
   player does not have to do anything.
2. It connects to the same server again and compares packs with it.
3. A pack the player does not have is downloaded. The screen shows
   `Downloading <file> (1 of 2)` with a progress figure.
4. The player enters the new match.

Downloaded packs are kept in a folder named `netcache`, next to the settings
file, and are reused next time. A pack is not downloaded when the player
already has a file with the same name and the same content in `netcache`, in a
folder the game searches, or anywhere below the folder that holds `netcache`.

Rules for packs:

- A pack is a `.grp`, `.zip`, `.pk3`, `.pk4` or `.ssi` file.
- Give its full path, or a path inside a folder the game searches.
- The base game's own data file cannot be used as a pack and is never sent.
  Every player needs their own copy of the edition the server runs. A player
  without that edition cannot join, and the log says that the server uses
  different base game data.

### `nextmap`

```text
nextmap
```

Goes to the next entry of the map rotation right away. Only the host can use
it. See [`mapcycle`](#mapcycle) for how the rotation is filled.

### `mapcycle`

```text
mapcycle
mapcycle list
mapcycle add [pack|base|current] <E#L#|map path>
mapcycle clear
mapcycle reload
```

Views or edits the map rotation, the list of levels a server plays one after
another.

| Form | What it does | Who |
| --- | --- | --- |
| `mapcycle` or `mapcycle list` | Shows the rotation and marks the current entry. | Anyone |
| `mapcycle add <level>` | Adds a level of the current pack. | Host |
| `mapcycle add <pack> <level>` | Adds a level of another pack, of `base`, or of `current`. | Host |
| `mapcycle clear` | Empties the rotation. | Host |
| `mapcycle reload` | Reads `maplist.txt` again. | Host |

```text
mapcycle add base E1L1
mapcycle add "packs/arena.pk3" E2L1
mapcycle add "packs/arena.pk3" "maps/User Arena.map"
mapcycle list
```

The rotation holds up to 64 entries. Changes made with this command last until
the server is shut down, and they survive a pack change. Editing is possible
only in a host-and-clients match, and not while a level change is in progress.

When the rotation reaches an entry that names another pack, the server
changes pack as described in [When the pack changes](#when-the-pack-changes).

To keep a rotation between sessions, write it in a text file named
`maplist.txt` in a folder the game searches, for example the folder the game
runs from. Each line is one entry:

```text
# One level of the current pack
E1L1
E1L2

# Pack first, then level
base E2L1
"packs/arena.pk3" "maps/User Arena.map"
current E3L1
```

Lines that start with `#` or `;` are ignored.

### `maplist`

```text
maplist
```

Shows the map rotation. It is the same as `mapcycle list`.

### `sv_maprotation`

```text
sv_maprotation <0|1|2>
```

A setting that controls what happens when a level ends in a network game.

| Value | Meaning |
| --- | --- |
| `0` | Off. This is the default. |
| `1` | Play the rotation in order. |
| `2` | Pick a random entry of the rotation. |

Only the host can change it. The other players are told the new value.

```text
sv_maprotation 1
```

### `sendfile`

```text
sendfile <name>
```

Sends a file to every connected player. Only the host can use it. Players keep
received files in their `netcache` folder. A player who already has an
identical copy does not download it again. Base game data is refused.

```text
sendfile "extras/readme.txt"
```

## Changing the match

These two commands change the level that is running, for all players at the
same moment. In a network game only the host can use them. They also work in
single player. You have to be in a level.

### `monsters`

```text
monsters off
monsters on [skill]
monsters <skill>
```

- `monsters off` removes every monster from the level and keeps monsters
  switched off.
- `monsters on` switches monsters back on. With a skill number it also sets
  the skill. It does not put removed monsters back into the level that is
  running.
- `monsters <skill>` is short for `monsters on <skill>`.

Skill numbers start at 1.

```text
monsters off
monsters on 3
```

### `spawn`

```text
spawn <tile> <player>
spawn <tile> <x> <y> [z]
```

Adds an enemy, weapon or item to the level.

- `<tile>` is a tile number, or a name defined by the game's scripts, such as
  `LIZTROOP` or `SHOTGUNSPRITE`.
- `<player>` is a player's name or slot number. The object appears a short
  distance in front of that player.
- `<x> <y> [z]` are map coordinates. Without `z` the object is placed just
  above the floor.

```text
spawn LIZTROOP maya
spawn SHOTGUNSPRITE 1
spawn 1680 2048 -4096
```

## Lobby

A lobby server keeps a list of games that players can join. See
[Lobby server](../multiplayer/lobby-server.md).

### `serverlist`

```text
serverlist [address]
```

Asks a lobby server for its list and prints one line per game: address,
players, map and name. Without an address it asks the lobby server set in
[`cl_masterserver`](#cl_masterserver).

```text
serverlist
serverlist lobby.example.org
```

### `cl_masterserver`

```text
cl_masterserver <host[:port]>
```

A setting with the address of the lobby server the menus use. **Join Game**
reads its list from this address, and **Host Internet Game** lists your game
there. The default is the public lobby, `nnb.ms.nukemnet.com`. Without a
port, 23519 is used.

```text
cl_masterserver lobby.example.org
cl_masterserver lobby.example.org:24000
```

### `cl_preferipv6`

```text
cl_preferipv6 <0|1>
```

A setting for joining from the game list. NNBuild asks a listed game over its
IPv4 and its IPv6 address at the same time. With `1`, the default, IPv6 is
used when it answers, and it gets 2.5 more seconds to do so after IPv4
answered. With `0`, the address that answers first is used.

## Player appearance

### `skin`

```text
skin <0-9>
```

A setting that chooses how other players see you in multiplayer. It changes
your looks, not how you play.

| Value | Skin |
| --- | --- |
| `0` | Duke |
| `1` | Assault Trooper |
| `2` | Assault Captain |
| `3` | Octabrain |
| `4` | Assault Commander |
| `5` | Enforcer |
| `6` | Pig Cop |
| `7` | Alien Beast |
| `8` | Lo Wang |
| `9` | Shelly |

If the art for a skin is not in the loaded game data, you appear as Duke. The
Alien Beast, for example, is not part of the 1.3D data. Lo Wang and Shelly
need the data of their own games to be loaded.

```text
skin 6
```

To set a skin when you start the game:

```sh
nnbuild --game duke --cmd "skin 6"
```

## Input

These three settings exist in every game of NNBuild, not only in the Duke
Nukem 3D runtime. Each is `0` (off, the default) or `1` (on).

### `cl_mousesmooth`

```text
cl_mousesmooth <0|1>
```

Smooths mouse movement by averaging each movement with the one before it.

### `cl_joysmoothing`

```text
cl_joysmoothing <0|1>
```

Smooths looking and turning with a controller.

### `cl_swapjoysticks`

```text
cl_swapjoysticks <0|1>
```

Swaps the left and right sticks of a game controller.

```text
cl_swapjoysticks 1
```

## World Tour

### `wt_commentary`

```text
wt_commentary <0|1>
```

A setting that shows and enables the developer commentary markers of World
Tour. The default is `0`. It has an effect only when the game is World Tour.

```text
wt_commentary 1
```

## Other runtimes

### `say` (Exhumed)

```text
say <text>
```

Sends a chat message to the other players. It exists while a network game of
Exhumed / PowerSlave is running. A server without a window uses it to talk to
its players.

```text
say The next level starts in one minute
```

### KEX control names (Exhumed)

In [KEX mode](../games/exhumed-powerslave.md#kex-mode), the console accepts
the control names used by the PowerSlave Exhumed remaster, so that key
bindings written with those names work: `automap`, `jump`, `strafeleft`,
`backward`, `+use`, `forward`, `straferight`, `+weapprev`, `+weapnext`,
`mapzoomout`, `inventoryscreen`, `mapzoomin`, `turnleft`, `turnright`,
`attack`, `weaponwheel` and `weapon`.

### `saveboard` (classic Build tools)

```text
saveboard
```

Saves the map that is open in the classic Build editing tools. See
[Classic Build tools](../editor/classic-build-tools.md).
