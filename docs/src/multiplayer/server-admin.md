# Administering a server

This page lists the commands a host uses to look after a running match: remove
players, change the level, switch mod packs, rotate maps and change the match
while it is being played.

It describes the Duke Nukem 3D family (Duke Nukem 3D, Duke Nukem 3D: World
Tour, NAM, NAPALM, World War II GI and Ion Fury), in the usual host-based
mode started with `/n0`.

## Where to type the commands

All of these are console commands. Only the host's copy of the game obeys
them. You can give them in several ways:

- **On a dedicated server**, type them in the server's terminal. See
  [The admin console](dedicated-server.md#the-admin-console).
- **On a host that also plays**, open the in-game console and type them there.
- **At startup**, with `--cmd` or `--cmd-file`. See
  [Startup commands](dedicated-server.md#startup-commands).
- **From a script or an AI assistant.** See
  [Server scripts](../modding/server-scripts.md) and
  [AI assistants and MCP](../editor/ai-and-mcp.md).

A **slot** is a player's place in the match. Each slot has a number. The host
is slot 0.

## See who is connected

```text
listplayers
```

The answer has one line per player: slot number, name and address. The host's
own line says `(this machine)` and ends in `[host]`.

```text
Players:
  0: ARENA  (this machine)  [host]
  1: DUKEFAN  203.0.113.77:23530
  2: PIGCOP99  [2001:db8::77]:23530
```

## Remove a player

### kick

```text
kick <name or slot>
```

Removes a player from the match. The player may join again.

```text
kick DUKEFAN
kick 1
```

- A name does not have to be complete. The first connected player whose name
  starts with what you typed is chosen, and capital letters do not matter. Use
  the slot number when two names start alike.
- The player's game closes with `You were removed from the game.` The others
  see a short message that the player left.
- You cannot kick the host itself.

### ban

```text
ban <name, slot or IPv4 address>
```

Removes a player and adds the player's address to the ban list. A banned
address cannot connect: the server does not answer it.

```text
ban DUKEFAN
ban 1
ban 203.0.113.77
```

- The third form bans an address that is not connected at the moment.
- The list holds IPv4 addresses, and up to 256 of them.
- A player who is connected over IPv6 cannot be banned. The command is
  refused, and the log tells you to use `kick` instead.
- Addresses of the server's own machine (127.x.x.x) are never banned.

### unban

```text
unban <IPv4 address or name>
```

Takes entries off the ban list. With a name, every entry whose recorded name
starts with what you typed is removed. The answer says how many entries were
removed.

```text
unban 203.0.113.77
unban DUKEFAN
```

### listban

```text
listban
```

Prints the ban list: the number of entries, then one address and name per
line.

### The ban list file

The ban list is kept in a text file named `banlist.txt`, so it survives a
restart.

- The file is in the folder the server runs from. That is the folder you
  start it in when you use `-usecwd`. Otherwise it is the profile folder,
  `~/.config/vibebuild32` on Linux and macOS.
- Each line is one address, optionally followed by a name. A line that starts
  with `#` is a comment.

  ```text
  # NNBuild ban list - one entry per line: <ip> <name>
  203.0.113.77 DUKEFAN
  198.51.100.4
  ```

- The server reads the file once, when it starts, and writes it again after
  every `ban` and `unban`. To edit the file by hand, stop the server first.

## Change the level

### map and changemap

```text
map <level> [pack]
changemap <level> [pack]
```

The two names mean the same. The command starts a fresh game on the given
level for everyone in the match.

A level is either a built-in level or a custom map file:

```text
map E1L3
map "maps/User Arena.map"
map castle
```

- A built-in level is written as `E`, the episode number, `L`, the level
  number. Capital letters do not matter.
- Anything else is taken as a map file. `.map` is added if you leave it out.
  Put quotes around a name that contains spaces.
- A custom map is sent to every connected player who does not have it,
  together with the files that share its name (a map-hack file, map music and
  map art).
- If the level cannot be found, the log says so and the current match
  continues.
- Scores saved for players who left are discarded, as for any new game.

Without a pack, the level is played with the mod packs the server is running
now. To change the mod pack at the same time, name it after the level:

```text
map "maps/User Arena.map" "packs/arena.pk3"
map E1L1 base
```

A change of pack is a bigger step than a change of level. Read
[Switch mod packs on a live server](#switch-mod-packs-on-a-live-server)
before you use it.

These commands are not available with shareware game data.

### changelevel

```text
changelevel <episode> <level>
```

The older command for built-in levels. Both values are numbers.

```text
changelevel 1 3
```

When a player other than the host uses it, it starts a vote instead.

### nextmap

```text
nextmap
```

Moves on to the next entry of the map cycle right away. In random rotation it
picks a random entry. If the cycle is empty, the command reports that it could
not advance and the match continues.

## Switch mod packs on a live server

A **mod pack** is one file that holds a mod: a GRP, ZIP, PK3, PK4 or SSI file.
A server can change to another mod pack, or back to the plain game, without
being stopped. The players are taken along automatically.

### mod

```text
mod <pack> [level]
```

Changes the mod pack and starts a fresh match.

- `<pack>` is the pack file, or the word `base` for the game without any mod
  pack.
- `[level]` is the level to start on, written as for `map`. Without it the
  match starts on E1L1.

```text
mod "packs/arena.pk3" E2L1
mod "packs/arena.pk3" "maps/User Arena.map"
mod base E1L1
```

`map <level> <pack>` does the same with the two parts the other way round.

Things to know about the pack:

- The file is looked for in the game's folders, so a path such as
  `packs/arena.pk3` is relative to the folder the server runs from.
- The new pack replaces every pack the server was running, including several
  packs given with `--grp` at startup.
- Level codes mean what the pack says they mean. `E2L1` is episode 2, level 1
  as the new pack defines them.
- The base game data cannot be named as a pack.

### What happens

1. The server checks the pack first. It starts a second, hidden copy of
   itself that loads the base game with the new pack, reads the pack's game
   scripts and opens the level. The running match is not touched and keeps
   going during the check.
2. If the check passes, the server logs
   `Restarting the match with the selected pack; players will reconnect`,
   tells every player, and restarts itself with the new pack.
3. The server comes back on the same address and port, with the same name and
   number of slots, and it lets players join at any time.
4. Each player's game restarts by itself, connects again, downloads the pack
   if it does not have it, and joins the new match. The players do not have to
   do anything. [Playing online](playing-online.md#when-the-server-changes-its-mod-pack)
   describes what they see.

While the check runs, the server refuses another level change and changes to
the map cycle.

Players come back in the same way as someone who joins a running match, so
they need the same kind of build as the server, 32-bit or 64-bit. See
[32-bit and 64-bit builds](hosting.md#32-bit-and-64-bit-builds).

### What is kept

The restarted server keeps the game mode, the skill, the frag limit, the game
flags, the rotation setting and the map cycle, including changes you made with
`mapcycle` and the place the rotation had reached. The ban list is kept in its
file as always.

It does not run the startup commands again. A server script starts again from
the beginning. Scores start at zero, because this is a new match.

### If the pack is rejected

A pack that fails never reaches the players. The running match continues as
if nothing had happened.

- If the pack file cannot be found, or is not a pack, the log says
  `Could not open server pack` and no check is started.
- If the check fails, the log says
  `Could not activate the requested pack and level`. This happens when the
  pack's scripts do not load, when the level does not exist in the pack, or
  when the check takes longer than 60 seconds. The check keeps a log of its
  own, `content-probe.log`. Look there for the reason.

### On a host that also plays

The commands work the same on a host that plays. The host's own game restarts
too.

## Map rotation

A server can change the level by itself each time a level ends. A level ends
when a player uses the exit or when the frag limit is reached.

### Turn rotation on

```text
sv_maprotation <0, 1 or 2>
```

| Value | Meaning |
| --- | --- |
| `0` | Off. Levels follow each other as in the original game. |
| `1` | In order. After the last entry the cycle starts again from the first. |
| `2` | Random. Any entry can be picked, including the one just played. |

Only the host can change this setting.

### The map cycle

The **map cycle** is the list of levels to rotate through. It can hold up to
64 entries. The server fills it from the file `maplist.txt` the first time it
is needed, and you can change it from the console while the server runs.

| Command | Meaning |
| --- | --- |
| `mapcycle` or `mapcycle list` | Print the cycle. `maplist` does the same. |
| `mapcycle add [pack] <level>` | Add an entry to the end. |
| `mapcycle clear` | Empty the cycle. |
| `mapcycle reload` | Read `maplist.txt` again and print the result. |

An entry is a level, optionally with a mod pack in front of it. The pack can
be:

- a pack file, such as `"packs/arena.pk3"`,
- `base`, for the game without any mod pack,
- `current`, for whatever the server is running when the entry comes up. This
  is also what an entry without a pack means.

```text
mapcycle add E3L2
mapcycle add "maps/User Arena.map"
mapcycle add base E1L1
mapcycle add "packs/arena.pk3" E2L1
mapcycle add "packs/arena.pk3" "maps/User Arena.map"
mapcycle list
```

The list looks like this:

```text
Map cycle (4, sequential)
  1. base | E1L1
  2. base | E1L2
  3. packs/arena.pk3 | E2L1 (current)
  4. packs/arena.pk3 | maps/User Arena.map
```

The first line gives the number of entries and the rotation mode. Each entry
shows its pack, a bar and its level. `(current)` at the end of a line marks
the entry the rotation last moved to.

Changes made with `mapcycle` last until the server stops, and they survive a
change of mod pack. They are not written to `maplist.txt`. `mapcycle clear`
and `mapcycle reload` also forget which entry was being played, so the next
rotation starts from the first entry.

### The maplist.txt file

Put `maplist.txt` in the folder the server runs from.

- One entry per line: a level, or a pack followed by a level. Both are written
  as for `mapcycle add`.
- A line that starts with `#` or `;` is a comment. A `#` comment may also
  follow an entry on the same line.
- Put quotes around a file name that contains spaces.
- Only the first 64 entries are used.
- A line the server cannot understand is skipped, and the log names its line
  number.

A cycle of built-in levels and custom maps, all played with what the server is
running:

```text
# maplist.txt
E1L1
E1L2
E2L1                     # a comment after an entry
"maps/User Arena.map"
castle.map
; another comment
```

A cycle that mixes the plain game with a mod pack:

```text
# maplist.txt
base E1L1
base E1L2
"packs/arena.pk3" E2L1
"packs/arena.pk3" "maps/User Arena.map"
base "maps/castle.map"
```

Built-in levels, custom maps and mod packs can be mixed freely. A custom map
in the cycle is sent to players in the same way as with `map`.

When a cycle uses more than one pack, name the pack on every line. An entry
without a pack is played with whatever pack the entry before it left running.

### Rotation across mod packs

When the next entry names a pack other than the one that is running, the
server changes pack exactly as the `mod` command does. The pack is checked
first, the server restarts with it, and the players' games reconnect and
download it by themselves. See
[Switch mod packs on a live server](#switch-mod-packs-on-a-live-server).

Entries that follow each other with the same pack change only the level.

### When rotation cannot continue

- If rotation is on and the cycle is empty, the current level starts again
  when it ends.
- If the next entry cannot be loaded when a level ends, the current level
  starts again and the log says
  `Map cycle could not advance; restarting the current level`. This includes
  an entry whose mod pack is rejected.

## Change the match while it runs

These commands change the match for every player at the same moment.

### fraglimit

```text
fraglimit <number>
```

Sets the number of frags that ends a level. Players see `FRAGLIMIT CHANGED TO`
and the number. `fraglimit` without a number shows the current value.

### monsters

```text
monsters off
monsters on [skill]
monsters <skill>
```

- `monsters off` removes every enemy from the level and turns monsters off.
- `monsters on` turns monsters on again. With a skill from 1 to 4, it also
  sets the skill. It does not put back the enemies that `monsters off`
  removed.
- `monsters 3` is a short way to write `monsters on 3`.

The command works only while a level is being played.

### spawn

```text
spawn <tile> <player>
spawn <tile> <x> <y> [z]
```

Adds an enemy or an item to the level that is being played.

- `<tile>` is a tile number or the name the game's scripts use for it, such as
  `LIZTROOP` or `SHOTGUNSPRITE`.
- With a player's name or slot, the object appears a short distance in front
  of that player.
- With coordinates, it appears at that point of the map. The point must be
  inside the level. Without `z` it is placed just above the floor.

```text
spawn LIZTROOP DUKEFAN
spawn SHOTGUNSPRITE 1
spawn LIZTROOP 2048 -4096
```

## Send a file to all players

```text
sendfile <file>
```

Sends one file to every connected player. Each player's game stores it in its
download cache, unless it already has an identical file. The game does not
load the file by itself. Base game data is refused.

## Other commands

| Command | Meaning |
| --- | --- |
| `name <text>` | Change the host's name. On a dedicated server this is the server's name, also on the lobby list. |
| `echo <text>` | Print text to the console and the log. Useful as a marker in a startup file. |
| `quit` or `exit` | Stop the host. See [Stop the server](dedicated-server.md#stop-the-server). |
| `serverlist [lobby]` | Print the matches on a lobby's list. See [The lobby server](lobby-server.md#the-serverlist-command). |

## Quick reference

| Command | Purpose |
| --- | --- |
| `listplayers` | Show slots, names and addresses |
| `kick <name or slot>` | Remove a player |
| `ban <name, slot or IPv4 address>` | Remove a player and ban the address |
| `unban <IPv4 address or name>` | Take entries off the ban list |
| `listban` | Show the ban list |
| `map <level> [pack]`, `changemap <level> [pack]` | Start a fresh game on a level, optionally with another mod pack |
| `changelevel <episode> <level>` | Start a fresh game on a built-in level |
| `nextmap` | Go to the next entry of the map cycle |
| `mod <pack> [level]` | Change the mod pack and start a fresh match |
| `sv_maprotation <0, 1 or 2>` | Rotation off, in order, or random |
| `mapcycle`, `mapcycle list`, `maplist` | Show the map cycle |
| `mapcycle add [pack] <level>` | Add an entry to the cycle |
| `mapcycle clear` | Empty the cycle |
| `mapcycle reload` | Read `maplist.txt` again |
| `fraglimit <number>` | Set the frag limit |
| `monsters off`, `monsters on [skill]`, `monsters <skill>` | Remove or allow monsters |
| `spawn <tile> <player>`, `spawn <tile> <x> <y> [z]` | Add an enemy or item |
| `sendfile <file>` | Send a file to all players |
| `name <text>` | Change the host's name |
| `quit`, `exit` | Stop the host |
