# Playing online

This page is for players. It explains how to find and join a match, what you
see while you join, what is downloaded for you, and what happens when a
connection goes wrong.

Most of this page describes the Duke Nukem 3D family: Duke Nukem 3D, Duke
Nukem 3D: World Tour, NAM, NAPALM, World War II GI and Ion Fury. They share one
game runtime, and that runtime has the full set of online features. The table
in [Other games](#other-games) shows what Shadow Warrior, Blood, Redneck
Rampage and Exhumed support.

Some words used on this page:

- The **host** is the computer that runs the match. Every other player
  connects to it. A host that only runs the match and does not play is a
  **server**.
- A **slot** is a place for one player in a match.
- The **lobby server** keeps a list of matches that are open. The menus and
  logs call it the masterserver.
- A **GRP** is the data file of the original game, for example `DUKE3D.GRP`.
- A **mod pack** is one file that holds a mod: a GRP, ZIP, PK3, PK4 or SSI
  file.

## Before you join

- **You need your own copy of the game data.** The host never sends the base
  game. If the host uses an edition you also have installed, NNBuild selects
  it for you. If you do not have it, the join stops and the log says
  `The server uses different base game data`.
- **Use the same NNBuild version as the host.** If the versions differ, the
  game closes with `You cannot play with different versions of NNBuild!`.
- **Use the 64-bit build unless your computer cannot run it.** To join a match
  that is already running, your build must be of the same kind as the host's:
  64-bit with 64-bit, or 32-bit with 32-bit. See
  [32-bit and 64-bit builds](#32-bit-and-64-bit-builds).
- **Tell NNBuild which lobby server to use**, if you want a server list. See
  the next section.

## Choose a lobby server

NNBuild asks one lobby server for the list of matches. Until you change it,
that is the public lobby, `nnb.ms.nukemnet.com`, so the list works without any
setup.

The address is the console setting `cl_masterserver`. To use another lobby,
for example one your community runs:

1. Open the in-game console.
2. Type the setting and the lobby's address:

   ```text
   cl_masterserver lobby.example.org
   ```

   Add a port if the lobby does not use the standard one, for example
   `lobby.example.org:23600`. The standard port is 23519.

The address is saved with your other settings. More about lobbies is on the
[lobby server](lobby-server.md) page.

## Join from the menu

1. Choose **MULTIPLAYER** in the main menu. The **NETWORK GAME** screen opens
   with **PLAYER SETUP**, **JOIN GAME** and **HOST GAME**.
2. Choose **JOIN GAME**. NNBuild asks the lobby server for its list and shows
   up to 12 matches. The bottom of the screen shows which lobby was asked, as
   `Masterserver:` followed by its address.
3. Move to a match and press Enter.

Each row shows the server's name, the player and slot numbers the server
reported, the map and the address. A row that ends in `+v6` also has an IPv6
address. A row that ends in `v6` has only an IPv6 address.

The list is fetched in the background. While NNBuild waits for the lobby, the
screen shows `Asking the masterserver...` and the menu keeps working.

### The CONNECTING screen

After you pick a match, the **CONNECTING** screen opens. NNBuild first checks
that the server can be reached, and the game keeps running while it does:

- A server can have an IPv4 address, an IPv6 address, or both. NNBuild asks the
  server over **both at the same time**. Each address has its own line, which
  shows `no answer yet` or how long the answer took.
- **IPv6 is preferred.** If the server answers over IPv6, that address is
  used. If it answers over IPv4 first, the IPv6 attempt gets 2.5 more seconds;
  after that IPv4 is used. The screen counts those seconds down.
- When there is no answer after about a second, NNBuild also asks the lobby
  to help it through the host's router. See
  [hole punching](lobby-server.md#hole-punching).
- **CANCEL** stops the attempt and returns to the list. Press Enter, click, or
  press Esc.
- With no answer after 25 seconds, NNBuild returns to the list with a message.

As soon as an address is chosen, NNBuild restarts itself as a player in that
match. The window closes and opens again. This is normal.

Below the matches there are two more entries:

- **Enter address manually...** opens **JOIN BY ADDRESS**. Type the address
  and port, then press Enter. Press Esc to go back. Use this for a match on
  your local network, or for one that is not on the list. A typed address
  goes through the **CONNECTING** screen too. A typed host name is joined
  directly, without that screen.

  ```text
  203.0.113.10:23513
  [2001:db8::10]:23513
  ```

  The first form is an IPv4 address and the second is an IPv6 address. If you
  leave out the port, 23513 is used.
- **Refresh list** asks the lobby again.

The screen can also show these messages:

| Message | Meaning |
| --- | --- |
| `Asking the masterserver...` | The list is being fetched. |
| `Could not reach the masterserver.` | The lobby's address could not be looked up. Check `cl_masterserver`. |
| `No servers advertised.` | The lobby's list is empty, or the lobby did not answer in time. |
| `No answer from <server>.` | You tried to join and the server did not answer within 25 seconds. It is offline, or it cannot be reached from your network. |
| `<server> is full.` | The server answered, and every player slot is taken. |
| `No answer from the server.` | The server answered at first, but the match could not be joined within about 30 seconds. A server that has banned your address behaves like this. |

The host has to run the same NNBuild release as you. An older host does not
answer the question the **CONNECTING** screen asks.

Ion Fury's main menu has no **MULTIPLAYER** entry. Join from the command line
instead.

## Join from the command line

Put the network options last. Everything after `--net` belongs to the
connection.

```sh
nnbuild --name DUKEFAN --net /n0 203.0.113.10:23513
```

- `--name` sets your nickname for this run.
- `/n0` followed by an address means "join the match hosted at this address".
- The address can be an IPv4 address, an IPv6 address in square brackets, or a
  host name. Add `:port` when the host does not use port 23513.

```sh
nnbuild --name DUKEFAN --net /n0 play.example.org:23600
nnbuild --name DUKEFAN --net /n0 '[2001:db8::10]:23513'
```

The quotes around the IPv6 address stop the shell from treating the square
brackets as a pattern.

If you run two copies of the game on one computer, give each copy its own
local port with `/p`. The number must be from 1025 to 65534.

```sh
nnbuild --name TESTER --net /p23514 /n0 127.0.0.1:23513
```

While the game waits for the server, the log shows a line every three seconds
(and the startup window, where there is one, shows it in its title):

```text
Waiting for the server - retrying (4)... Cancel to abort
```

A command-line join keeps trying until the server answers or you cancel. It is
safe to start your game before the server is up.

If you have data for more than one game installed, add `--game duke` (or the
name of the game you mean) before `--net`. Without it, NNBuild asks which game
to run when it is started from a terminal, and takes the first game it finds
when it is not. See [First launch](../getting-started/first-launch.md).

## Joining a match that is already running

A host can leave slots open so that players may join at any time. When you
join such a match you see a joining screen instead of the game:

- a status line,
- a progress bar and a percentage,
- two choices, **WAITING** and **CANCEL**.

The status line changes as the join moves forward:

| Status line | What is happening |
| --- | --- |
| `Connecting to game in progress` | The host knows you are there. |
| `Downloading <file> (2 of 3)` | A file you do not have is being sent. |
| `Receiving game state` | The host is sending the running match. |
| `Entering the level` | Everything has arrived. |

To give up, press Esc, or move down to **CANCEL** and press Enter. The game
then closes.

Mod packs are downloaded earlier, before the game window opens. During that
time the same `Downloading` text is written to the log and, where there is a
startup window, shown in its title.

While you receive the running match, the match is paused for everyone. When
you are in, the other players see a short message with your name followed by
`joined`. You appear at one of the map's start points with fresh weapons. Your
score starts at zero, unless you are coming back to a match you left (see
[Leaving and coming back](#leaving-and-coming-back)).

If nothing arrives from the host for 20 seconds, the join stops with
`Timed out joining the game in progress (no data from host).`

The host also gives up on a player whose game needs more than 25 seconds to
load after connecting, and that player then sees the same message. If it
happens to you, join again. The second attempt is usually faster, because your
computer has just read the same files.

If the host has not started the match yet, your game waits with it and enters
the level together with everyone else.

### 32-bit and 64-bit builds

NNBuild comes as a 64-bit build and as a 32-bit build. A player cannot join a
running match with the other kind. The running match is sent to a new player
in the form of a saved game, and a saved game made by one kind of build cannot
be read by the other.

- **Use the 64-bit build unless your computer cannot run it.** The
  [Downloads](../downloads.md) page lists the builds.
- A player with the 32-bit build cannot join a running match on a 64-bit host.
  A player with the 64-bit build cannot join a running match on a 32-bit host.
- 64-bit with 64-bit works, and 32-bit with 32-bit works. The operating system
  does not matter. A Windows player can join a Linux server, for example.

If your build is the wrong kind, the join runs to the end of the transfer and
then the game closes with one of these messages:

```text
This match is hosted by a 64-bit build.
Joining a match in progress needs the 64-bit build of the game.
```

```text
This match is hosted by a 32-bit build.
Joining a match in progress needs the 32-bit build of the game.
```

The same rule applies whenever your game joins a running match by itself:
when you come back after a lost connection, after it lost sync, and after the
server changed its mod pack.

## Automatic downloads

When you join, the host tells your game which files the match uses. For each
one your game checks the name, the size and a checksum. Only files you do not
have are sent.

The host sends:

- the mod packs the match is using,
- the custom map being played, together with the files that belong to it: a
  map-hack file (`.mhk`), map music, and map art files such as
  `arena_00.art`.

The host does not send the base game data. Each player needs their own copy.

Your game looks for a file you already have in three places before it
downloads: its download cache, its normal game folders, and the folders below
the one it runs from. A file with the same name and the same content is used
even if it sits in another folder.

Downloads are kept in a folder named `netcache` inside the folder the game
runs from. On Linux and macOS that is `~/.config/nnbuild` unless you start
the game with `-usecwd`. A file that is already in the cache is not downloaded
again.

## Leaving and coming back

To leave, quit the game in the usual way. The other players see your name
followed by `is history!`. You can join the same server again later. After a
normal quit you start again with a score of zero.

If you did not mean to leave, the host keeps your score for you. It saves
your frags, suicides and deaths when it removes you for one of these reasons:

- your connection went silent, for example after a crash or a network
  failure. The host frees the slot of a player it has not heard from for five
  seconds;
- you started the game again and connected before the host had noticed that
  you were gone. The host then drops the old connection and lets you in;
- you were removed by the lag countdown or vote;
- your game restarted itself after losing sync;
- you were kicked.

To come back, join the same server again. If the match is still running, your
score is restored a moment after you appear.

- The host recognises you by a player ID, not by your nickname. NNBuild
  creates the ID the first time it runs and keeps it in your settings file.
  Someone who copies your name does not get your score.
- Scores are kept for the current match only. When the host starts a new game
  or changes the level, saved scores are discarded.
- If you delete your settings file you get a new ID, and the host treats you
  as a new player.

Coming back needs a host that lets players join a running match. Matches
started from the **HOST GAME** menu allow it.

## When a player lags

The usual way to run a match is called **lockstep**: the game moves forward
only when it has every player's input for the next moment. If one player's
input stops arriving, everyone would wait. NNBuild limits that wait.

After half a second without progress, every player sees:

```text
WAITING FOR PLAYERS
DUKEFAN is lagging
Continuing in 30...
```

The countdown starts at 30 seconds.

- If you are not the one lagging, you also see `Press F9 to vote to drop`.
  Press F9 to vote. The line changes to `Vote to drop cast`.
- If you are the one lagging, you see `Reconnect or you will be dropped`.

The lagging player is removed when the countdown reaches zero, or earlier when
enough players have voted. The host counts the players who are not lagging,
leaving itself out, and removes the lagging player once the votes reach half
of that number. In a match with only two players, only the countdown applies.

A player whose connection goes completely silent is removed after five
seconds, without a vote.

If the lagging player catches up, the message disappears and the match
continues. A removed player can join again, as described above.

A host can also run the match in a no-wait mode. In that mode there is no
countdown, because the match never stops for a slow player. See
[Lockstep and no-wait mode](hosting.md#lockstep-and-no-wait-mode).

## When your game loses sync

Every copy of the game must stay exactly the same as the host's. NNBuild
checks this all the time. If your copy has drifted away, you see:

```text
LOST SYNC WITH HOST (...)
The game desynchronized.
Reconnecting...
```

After about two seconds your game restarts itself with the same launch options
and joins the match again. Your score is restored in the same way as after
leaving and coming back. You do not need to do anything.

This also needs a host that lets players join a running match. If the restart
fails, the game closes with `Reconnect after desync failed; please rejoin.`

[How multiplayer works](how-it-works.md) explains why this can happen.

## When the server changes its mod pack

A server can switch to another mod pack while players are connected. Every
game must then load the new pack from the start, so the match begins again
and your game follows by itself:

1. Your game stops for about two seconds.
2. It restarts itself with the same launch options and connects to the server
   again. The window closes and opens again.
3. If you do not have the new pack, it is downloaded and kept in your
   download cache.
4. You see the joining screen, and then you are in the new match.

You do not need to do anything. Scores start at zero for everyone, because
this is a new match.

The log shows `Reconnecting to load the server's selected pack` when this
happens. A server that rotates maps may do it several times in an evening,
each time the rotation moves to a level from another pack.

## Player options

Open **MULTIPLAYER**, then **PLAYER SETUP**. The screen has these entries:
**Name**, **Color**, **Team**, **Skin**, **Auto aim**, **Mouse aim**, **Switch
weapons on pickup**, **Switch weapons when empty** and **Multiplayer macros**.

You can also set the first four from the console or at launch:

| Option | Console | Launch option |
| --- | --- | --- |
| Name | `name DUKEFAN` | `--name DUKEFAN` |
| Colour | `color <palette number>` | `--color <colour>` |
| Team | `team <0-3>` | |
| Skin | `skin <0-9>` | `--skin <name or number>` |

- **Name.** A name is cut to 10 characters and is shown in capital letters.
- **Colour.** `--color` takes one of `brown`, `gray`, `purple`, `red`,
  `yellow`, `olive`, `green`, `blue`, or a value such as `#2050e0`, which
  picks the closest of those eight.
- **Team.** The teams are Blue (0), Red (1), Green (2) and Gray (3).
- **Skin.** See below.

### Skins

A skin changes how other players see you. The first seven after Duke are
enemies from Duke Nukem 3D. The last two are characters from other games.

| Number | Skin |
| --- | --- |
| 0 | Duke |
| 1 | Assault Trooper |
| 2 | Assault Captain |
| 3 | Octabrain |
| 4 | Assault Commander |
| 5 | Enforcer |
| 6 | Pig Cop |
| 7 | Alien Beast |
| 8 | Lo Wang |
| 9 | Shelly |

- An enemy skin can be chosen only when the game data contains that enemy's
  art. Alien Beast needs the Atomic Edition.
- Lo Wang needs Shadow Warrior data and Shelly needs Ion Fury data.
- A skin that is not available falls back to Duke.

At launch you can give the number or the name. Spaces, hyphens and capital
letters in the name do not matter.

```sh
nnbuild --skin pigcop --net /n0 203.0.113.10:23513
nnbuild --skin "Assault Commander" --net /n0 203.0.113.10:23513
```

## Optional settings for your own game

- `--zeroping` shows your own gunfire the moment you press fire, instead of
  waiting for the shot to come back from the host. It changes only what you
  see. Each player decides this for themselves.

  ```sh
  nnbuild --zeroping --net /n0 203.0.113.10:23513
  ```

- `cl_preferipv6` decides which address wins when a listed server answers
  over both IPv6 and IPv4. With `1`, the default, IPv6 wins and is given a
  moment to answer. With `0`, the first answer wins.

## Chat commands

Some servers run scripts that answer commands typed in the chat box. A message
that starts with `!` is such a command, for example `!rules`. It goes to the
server only, and the answer is shown only to you. If the server has no such
command, you get `Unknown server command.`

## Other games

NNBuild can also run Shadow Warrior, Blood, Redneck Rampage and Exhumed
(PowerSlave). Each has its own runtime, and the online features differ. "No"
means the code of that runtime does not contain the feature.

| Feature | Duke Nukem 3D family | Shadow Warrior | Blood | Redneck Rampage | Exhumed |
| --- | --- | --- | --- | --- | --- |
| Server list in the game | **JOIN GAME** | **Browse Servers** in the Network Game menu | No | No | No |
| Join by address at launch | `--net /n0 <address>` | `--net /n0 <address>` | `--client <host>`, with `--port <port>` if needed | `--connect <address>` | `--net /n0 <address>` |
| Join the first match on a lobby with `--joinlobby <lobby>` | No | Yes | Yes | Yes | Yes |
| Join a running match | Yes | No | No | No | Yes, if the host allows it with `--latejoin` and has a free slot |
| Come back with your score | Yes, except after a normal quit | No | No | No | Yes, into the slot you left |
| Files sent to you | Mod packs and custom maps, checked against what you have | Files the host names with `--netfile`, and the custom map | Files the host names with `--netfile` | Files the host names with `--netfile`, and the custom map | Files the host names with `--netfile`, and the custom map |
| Lag countdown and F9 vote | Yes | No | No | No | No. A silent player is removed after 12 seconds. |
| Automatic reconnect after losing sync | Yes | No | No | No | No. The game shows `OUT OF SYNC`. |
| Enemy skins | Yes | No | No | No | No |
| `--zeroping` | Yes | No | No | No | No |

In Shadow Warrior, Blood and Redneck Rampage all players must be connected
before the match starts.
