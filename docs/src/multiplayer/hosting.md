# Hosting a match

This page explains how to host a match that you also play in. To run a server
that only hosts, read [Running a dedicated server](dedicated-server.md)
afterwards. It builds on this page.

The page describes the Duke Nukem 3D family (Duke Nukem 3D, Duke Nukem 3D:
World Tour, NAM, NAPALM, World War II GI and Ion Fury). The last section,
[Other games](#other-games), lists what differs for Shadow Warrior, Blood,
Redneck Rampage and Exhumed.

Some words used on this page:

- The **host** is the computer that runs the match. Other players connect to
  it.
- A **slot** is a place for one player. The host uses one slot itself.
- A **GRP** is the data file of the original game, for example `DUKE3D.GRP`.
- A **mod pack** is one file that holds a mod: a GRP, ZIP, PK3, PK4 or SSI
  file.
- The **lobby server** keeps a list of open matches. Menus and logs call it
  the masterserver. See [The lobby server](lobby-server.md).
- **NAT** is what a home router does when several devices share one internet
  address. A router with NAT blocks connections that come in from outside
  unless you tell it otherwise.

## Host from the menu

1. Choose **MULTIPLAYER** in the main menu.
2. Choose **HOST GAME**.
3. Choose one of:
   - **HOST INTERNET GAME** to host and put the match on the lobby server's
     list.
   - **HOST LAN GAME** to host without putting it on any list.

Both choices do the same thing apart from the list. NNBuild restarts itself as
a host with these settings:

- 4 slots,
- UDP port 23513,
- the match starts at once, without waiting for anyone,
- players can join at any time.

The menu text names E1L1 as the starting level. To move to another level, open
the console and use the `map` command, for example `map E1L3`. See
[Administering a server](server-admin.md#change-the-level).

**HOST INTERNET GAME** puts the match on the lobby in the console setting
`cl_masterserver`. Until you change that setting it is the public lobby,
`nnb.ms.nukemnet.com`, so everyone who opens **JOIN GAME** sees your match.
[Playing online](playing-online.md#choose-a-lobby-server) shows how to use
another lobby.

Players on your local network join a **HOST LAN GAME** match by address: they
choose **JOIN GAME**, then **Enter address manually...**, and type your
computer's local address and the port, for example `192.168.1.20:23513`.

Ion Fury's main menu has no **MULTIPLAYER** entry. Host from the command line
instead.

## Host from the command line

The command line gives you every setting. The network part starts with `--net`
and must come last: everything after `--net` belongs to the connection, and
every other option must come before it.

```text
nnbuild [options] --net [/p<port>] /n0:<slots>
```

- `/n0:<slots>` hosts a match with that many slots. The host counts as one.
  `/n0` alone means 2 slots.
- `/p<port>` chooses the UDP port to listen on. The number must be from 1025
  to 65534. Without `/p`, port 23513 is used.

The tokens after `--net` may also be written with a dash, as `-n0:4` and
`-p23513`.

If you have data for more than one game installed, add `--game duke` (or the
name of the game you mean) before `--net`. Without it, NNBuild asks which game
to run when it is started from a terminal, and takes the first game it finds
when it is not. See [First launch](../getting-started/first-launch.md).

### Worked examples

**A match for two.** The host waits for the second player, then the match
starts on episode 1, level 1.

```sh
nnbuild --name HOST --coop 1 --volume 1 --level 1 --net /n0
```

The other player joins with:

```sh
nnbuild --name GUEST --net /n0 203.0.113.10:23513
```

**An open deathmatch with 8 slots.** The match starts at once, and players
come and go as they like.

```sh
nnbuild --name ARENA --startnow --coop 1 --volume 1 --level 1 --net /n0:8
```

**A co-op game for four that starts when the first friend arrives.** The other
two can join later.

```sh
nnbuild --name HOST --latejoin --coop 2 --volume 1 --level 1 --net /n0:4
```

**A different port.** Players then add `:23600` to your address.

```sh
nnbuild --name ARENA --startnow --coop 1 --volume 1 --level 1 --net /p23600 /n0:8
```

**A mod pack and a custom map.** Players who join get both files
automatically.

```sh
nnbuild --name ARENA --startnow --coop 1 --grp arena.pk3 --map "User Arena.map" --net /n0:8
```

**A match on the lobby list.** Add `--advertise` and the lobby's address.

```sh
nnbuild --name ARENA --startnow --coop 1 --volume 1 --level 1 --advertise lobby.example.org --net /n0:8
```

## Number of slots

The number after `/n0:` is the number of slots, with the host included. A
match needs at least 2 slots. The largest match that players can join has 15
slots.

On a dedicated server the server itself takes one slot and does not play, so
`/n0:8` leaves room for 7 players.

## Game mode and starting level

| Option | Meaning |
| --- | --- |
| `--coop 1` | Dukematch |
| `--coop 2` | Cooperative |
| `--coop 3` | Team Dukematch |
| `--coop 4` | Survival |
| `--volume <n>` | Episode to start in |
| `--level <n>` | Level to start on, within that episode |
| `--map <file>` | Start on a custom map instead |
| `--monsters 0` | No monsters |
| `--monsters <n>` | Monsters on, at skill level `n` (1 or higher) |
| `-y<n>` | Frag limit, for example `-y20` |

The older short forms still work: `-c1`, `-c2` and `-c3` for the game mode,
`-v1` for the episode and `-l1` for the level. Write the number directly after
the letter. `-c` followed by a space means something else: it gives a
[startup command](dedicated-server.md#startup-commands).

`--map` also accepts the name of a built-in level, such as `--map E2L1`.

### The setup screen inside the game

A host who starts without a level and without `--startnow` arrives at the
title menu once the players have connected. Choosing **NEW GAME** there opens
a setup screen with **GAME TYPE**, **EPISODE**, **LEVEL**, **MONSTERS**,
**GAME FLAGS**, **USER MAP** and **START GAME**.

The same screen is available during a match: press Esc and choose **NEW
GAME**. When the host chooses **START GAME**, the new game starts for everyone.
When another player does, a vote starts and the others are asked to press F1
to accept or F2 to decline.

## Open slots and joining in progress

How the match starts depends on one option:

| Option | When the match starts | Joining later |
| --- | --- | --- |
| none | When every slot is filled | Not possible |
| `--latejoin` (short `-lj`) | When one other player has connected | Possible while slots are free |
| `--startnow` (short `-sn`) | At once, even if the host is alone | Possible while slots are free |

`--startnow` includes `--latejoin`. The older spelling `-nowait` means the
same as `--startnow`.

With one of these options a player can also leave and come back, and can be
brought back automatically after losing sync. See
[Playing online](playing-online.md#leaving-and-coming-back).

When a player joins a running match, the match pauses for everyone while the
host sends the newcomer the current state of the game. The pause lasts as long
as that transfer.

Without either option, ask players to connect close together. A player's game
that has connected and then hears nothing from the host for 20 seconds stops
waiting and closes.

### 32-bit and 64-bit builds

NNBuild comes as a 64-bit build and as a 32-bit build. A player can join a
running match only with the same kind of build as the host's.

- **Host with the 64-bit build unless the machine cannot run it**, and ask
  your players to use the 64-bit build too.
- A 32-bit player cannot join a running match on a 64-bit host, and a 64-bit
  player cannot join a running match on a 32-bit host.
- 64-bit with 64-bit works, and 32-bit with 32-bit works. The operating system
  does not matter: a Linux server and Windows players get along.

The reason is that the host sends a newcomer the running match as a saved
game, and a saved game made by one kind of build cannot be read by the other.
A player with the wrong kind of build is told so at the end of the transfer,
and the game closes. The other players still see the usual pause while that
join is attempted.

The rule covers every way of entering a running match: joining late, coming
back, reconnecting after a loss of sync, and reconnecting after a change of
mod pack.

## Lockstep and no-wait mode

NNBuild keeps every player's game identical by running the same steps on every
computer. There are two ways to pace those steps.

**Lockstep** is the normal mode. The match moves forward only when the host
has every player's input for the next step. Everyone sees the same thing at
the same moment. The cost is that one slow connection slows everybody. NNBuild
limits the damage with a countdown and a vote, described in
[Playing online](playing-online.md#when-a-player-lags).

**No-wait mode** is turned on by the host with `--async` (short `-as`).

```sh
nnbuild --name ARENA --startnow --async --coop 1 --volume 1 --level 1 --net /n0:8
```

In this mode the host never waits. If a player's input is late, the host
repeats that player's last input for now and uses the real one when it
arrives. The match keeps its pace for everyone else.

The trade-off: each player's own actions travel to the host and back before
they take effect. On a slow connection your own controls feel delayed, while
in lockstep the delay is shared by all. There is no lag countdown in this
mode. A player whose connection goes completely silent is still removed after
five seconds.

Players do not need to set anything. The host tells each game which mode is in
use.

## Peer-to-peer mode

The older `/n1` form is another way to start a match. There is no host that
players join by address. Instead, every player gives the addresses of all
players at launch.

List every player's address, your own included. The order does not matter:

```sh
nnbuild --net /n1 192.168.1.2 192.168.1.100 192.168.1.4
```

Every player runs the same command. NNBuild sorts the addresses the same way
on each computer and finds its own entry by comparing the list with the
computer's network addresses. This works on Linux and macOS.

On Windows, use the original form instead. Each player lists only the other
players, and the position of `/n1` marks that player's own place in the order:

| Player | Address | Command ends with |
| --- | --- | --- |
| First | 192.168.1.2 | `--net /n1 192.168.1.100 192.168.1.4` |
| Second | 192.168.1.100 | `--net 192.168.1.2 /n1 192.168.1.4` |
| Third | 192.168.1.4 | `--net 192.168.1.2 192.168.1.100 /n1` |

The game mode, level and other match settings are taken from the player who
comes first in the order.

The map cycle (`mapcycle`, `nextmap` and map rotation) and chat commands that
start with `!` work only in the host-based `/n0` mode. The rest of this
section of the manual describes `/n0`.

## Ports and routers

- The host must be reachable on its UDP port: 23513, or the port given with
  `/p`.
- If the host is behind a home router, forward that UDP port to the host's
  computer in the router's settings. If the host's computer has a firewall,
  allow the port there too.
- Players who only join do not need to open anything.

**Hole punching** can make the port forward unnecessary. It is a way for two
computers behind routers to reach each other with the help of a third computer
that both can reach, here the lobby server. It applies when:

- the host is on a lobby's list (started with `--advertise`, or with **HOST
  INTERNET GAME**), and
- the player joins through the same lobby, by picking the match in **JOIN
  GAME**.

It does not work with every router. If players cannot connect, forward the
port. [The lobby server](lobby-server.md#hole-punching) explains the steps and
the limits.

## IPv4 and IPv6

The game listens on IPv4 and IPv6 at the same time when the system allows it,
and on IPv4 only when it does not. At startup it prints which:

```text
mmulti: bound UDP port 23513 (IPv4+IPv6)
```

An address can be written in these ways, here and in the join screens:

| Form | Example |
| --- | --- |
| IPv4 with port | `203.0.113.10:23513` |
| IPv6 with port | `[2001:db8::10]:23513` |
| Host name with port | `play.example.org:23513` |
| Any of them without a port | Port 23513 is used |

When a host name has both an IPv4 and an IPv6 address, the IPv6 address is
used.

## Put the match on a lobby list

Add `--advertise` (short `-ad`) and the lobby's address before `--net`:

```sh
nnbuild --name ARENA --startnow --coop 1 --volume 1 --level 1 --advertise lobby.example.org --net /n0:8
```

- The address can be a host name, an IPv4 address or an IPv6 address in square
  brackets. Add `:port` if the lobby does not use port 23519.
- The match appears on the list once it is running, and the entry is renewed
  every five seconds.
- The list shows your `--name` as the server's name. A name is cut to 10
  characters and shown in capital letters.
- When the host quits, it tells the lobby, and the entry is removed.

## Mods and custom maps

Start the host with the mod pack and it is shared with everyone who joins:

```sh
nnbuild --name ARENA --startnow --coop 1 --grp arena.pk3 --volume 1 --level 1 --net /n0:8
```

- `--grp <file>` (short `-g <file>`) loads a mod pack. A pack can be a GRP,
  ZIP, PK3, PK4 or SSI file. You can give `--grp` several times. Up to 8 packs
  are offered to players.
- Each player's game checks every pack by name, size and checksum, downloads
  the ones it lacks, and loads the same packs before it reads the game's
  scripts and art.
- A custom map given with `--map` is sent in the same way, together with the
  files that share its name: a map-hack file (`.mhk`), map music, and map art
  such as `arena_00.art`.
- The base game data is never sent. Every player needs their own copy of the
  same edition.
- Only packs are shared. Files that the host loads loose from a folder are not
  sent, so put a mod's files into one pack.

The base game can be given with `--grp` as well, for example
`--grp ~/games/duke3d/DUKE3D.GRP`. NNBuild recognises it as game data and does
not treat it as a mod pack.

A running host can change to another mod pack, or go back to the plain game,
without being stopped. The `mod` command does this, and a map rotation can do
it by itself. The players' games reconnect and download the new pack
automatically. See
[Switch mod packs on a live server](server-admin.md#switch-mod-packs-on-a-live-server).

## Other games

"No" means the code of that runtime does not contain the feature.

| | Duke Nukem 3D family | Shadow Warrior | Blood | Redneck Rampage | Exhumed |
| --- | --- | --- | --- | --- | --- |
| Start a host | `--net /n0:<slots>` | `--net /n0:<slots>` | `--server <players>` | `--server` | `--net /n0:<slots>` |
| Choose the port | `/p<port>` after `--net` | `/p<port>` after `--net` | `--port <port>` | `--port <port>` | `/p<port>` after `--net` |
| Standard port | 23513 | 23513 | 23513 | 23513 | 23513 |
| Start without waiting for every player | `--latejoin`, `--startnow` | No | No | `--startnow` with `--nplayers <n>` | `--startnow` with `--nplayers <n>` |
| Join a running match | With `--latejoin` or `--startnow` | No | No | No | With `--latejoin` |
| Put the match on a lobby list | `--advertise <lobby>` | `--advertise <lobby>` | `--advertise <lobby>` | `--advertise <lobby>` | `--advertise <lobby>` |
| Send files to players | Automatic for mod packs and the custom map | `--netfile <file>` and the custom map | `--netfile <file>` | `--netfile <file>` and the custom map | `--netfile <file>` and the custom map |

Notes:

- `--netfile` can be given up to 8 times. The player's game keeps a file it
  already has and writes a missing one into the folder it runs from.
- `--nplayers <n>` sets how many players must be connected before the match
  starts. The smallest value is 2.
