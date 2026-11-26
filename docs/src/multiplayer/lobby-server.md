# The lobby server

A lobby server keeps a list of matches that are open right now. Hosts report
their matches to it, and players ask it for the list. It also helps players
reach a host that sits behind a home router.

The menus, the logs and the option names call it the **masterserver**. This
manual calls it the lobby server.

The lobby only keeps the list and passes on requests to connect. Game traffic
never goes through it.

NNBuild comes with a public lobby, **`nnb.ms.nukemnet.com`**. The menus use it
until you name another one, so most players never have to think about lobbies.
Any community can also run its own.

One lobby serves every game NNBuild runs. Each match is listed with the game
it is for, and a player is only shown the matches of the game they are
running. See [Games on one lobby](#games-on-one-lobby).

Most of this page describes the Duke Nukem 3D family.
[Other games](#other-games) lists what the other runtimes do.

## Run a lobby server

NNBuild itself is the lobby program. Start it with `-masterserver`:

```sh
nnbuild --headless -masterserver
```

To use another port, write the port number directly after the option:

```sh
nnbuild --headless -masterserver 23600
```

- `-masterserver` is written with one dash.
- Without a number, the lobby listens on UDP port 23519.
- No game data is needed, and NNBuild does not ask which game to run.
- `--headless` keeps NNBuild from opening its startup window.
- The lobby runs until you stop the process.

The lobby listens on IPv4 and IPv6. When it starts it prints:

```text
NNBuild masterserver listening on UDP 23519 (IPv4:on IPv6:on)
```

After that it prints a line for each event:

```text
masterserver: + 203.0.113.10:23513 "ARENA ONE" (E1L1) [duke]
masterserver: punch 198.51.100.4:23530 <-> 203.0.113.10:23513
masterserver: - 203.0.113.10:23513 "ARENA ONE" (going away)
```

| Line | Meaning |
| --- | --- |
| `+` | A server was added to the list. The name in square brackets is its game; `?` means the host is too old to say. |
| `punch` | The lobby helped a player and a server find each other. |
| `- ... (going away)` | A server said it was shutting down. |
| `- ... (timed out)` | A server stopped reporting and was dropped. |

The same lines are written to `nnbuild.log` in the folder the lobby was
started in.

Open the lobby's UDP port for incoming traffic in the machine's firewall, for
IPv4 and for IPv6. Hosts and players must both be able to reach it. Give the
lobby's host name both an IPv4 (A) and an IPv6 (AAAA) record: hosts list their
IPv6 address only with a lobby they can reach over IPv6.

### The lobby as a program of its own

`nnbuild-lobby` is the same lobby as a small separate program. It needs no
game data and none of the display and sound libraries the game uses, which
suits a server that only keeps the list. The Linux downloads contain it. To
build it yourself, run `make nnbuild-lobby`.

```sh
nnbuild-lobby          # UDP port 23519
nnbuild-lobby 23600    # another port
```

It writes the same lines as above, each with the date and time, to standard
error. Run it under your usual service manager and as a user without special
rights, for example with pm2:

```sh
pm2 start ./nnbuild-lobby --name nnbuild-lobby --interpreter none --uid nnbuild --gid nnbuild
pm2 save
```

## Put a match on the list

A host reports its match with `--advertise` (short `-ad`) and the lobby's
address, placed before `--net`:

```sh
nnbuild --name ARENA --startnow --coop 1 --volume 1 --level 1 --advertise lobby.example.org --net /n0:8
```

The address can be a host name, an IPv4 address, or an IPv6 address in square
brackets. Add `:port` if the lobby does not listen on 23519, for example
`lobby.example.org:23600`.

A host started from the menu with **HOST INTERNET GAME** reports to the lobby
in the console setting `cl_masterserver`, which is the public lobby unless you
changed it. To put a match started from the command line on the public lobby,
write `--advertise nnb.ms.nukemnet.com`.

The host sends its name, its game port, the number of players in the match,
the number of slots, the name of the current map and the name of its game.
The lobby adds the address the report came from.

## Use a lobby as a player

Players point their game at a lobby with the console setting `cl_masterserver`:

```text
cl_masterserver lobby.example.org
```

- Until it is changed, the setting is the public lobby, `nnb.ms.nukemnet.com`.
- The setting is saved with the player's other settings.
- **JOIN GAME** asks the lobby when the screen opens and again when you choose
  **Refresh list**. It shows the first 12 matches. The menu keeps working
  while it waits for the answer.

[Playing online](playing-online.md#join-from-the-menu) describes the screen.

## The serverlist command

The console command `serverlist` asks a lobby for its list and prints it.

```text
serverlist
serverlist lobby.example.org
```

Without an address, the command asks the lobby in `cl_masterserver`, the same
one **JOIN GAME** uses. With an address, it asks that lobby instead.

Unlike **JOIN GAME**, the command lists the matches of every game. The answer
starts with the number of matches and the name of the game you are running.
Then there is one line per match with its address, the player and slot
numbers it reported, the map, its game and the server's name. If a match also
has an IPv6 address, a second line starting with `also IPv6:` follows. The
command prints up to 64 matches.

## Games on one lobby

A host tells the lobby which game its match is for, and a player's list holds
only the matches of the same game. Data that cannot play together goes by
different names:

| Name | Game |
| --- | --- |
| `duke` | Duke Nukem 3D, Atomic Edition (1.4 and 1.5) |
| `duke13` | Duke Nukem 3D 1.3D |
| `duke-shareware` | Duke Nukem 3D shareware |
| `duke-worldtour` | Duke Nukem 3D: 20th Anniversary World Tour |
| `nam`, `napalm`, `ww2gi`, `ionfury` | NAM, NAPALM, World War II GI, Ion Fury |
| `sw`, `sw-shareware` | Shadow Warrior, and its shareware episode |
| `blood` | Blood |
| `rr` | Redneck Rampage and Rides Again |
| `exhumed` | Exhumed / PowerSlave |

- The same goes for `--lobbylist` and `--joinlobby`: they see only matches of
  the game being started.
- A host from a release before these names existed is listed without one. It
  appears in `serverlist`, and to players on older releases, but not in the
  list of a current game.
- A lobby from an older release does not know the names. A current game then
  gets that lobby's whole list, as before.

## How long an entry lives

- A host starts reporting once its match is running.
- It reports again every 5 seconds. Each report renews the entry and updates
  its details.
- When a host quits normally, it tells the lobby, and the entry is removed at
  once.
- If the reports stop without that message, for example because the host
  crashed or lost its connection, the lobby drops the entry after 30 seconds
  without a report.

## IPv4 and IPv6

- A host reports over IPv4 and over IPv6 when the lobby's host name has both
  kinds of address and the host can use both. The lobby joins the two reports
  into one entry.
- In **JOIN GAME**, an entry with both kinds of address is marked `+v6`, and an
  entry with only an IPv6 address is marked `v6`.
- A player's game asks the server over both addresses at the same time and
  joins over the one that works. IPv6 is preferred: it wins when it answers,
  and after an answer over IPv4 it gets 2.5 more seconds. Set
  `cl_preferipv6 0` in the console to take whichever answers first.
  [Playing online](playing-online.md#the-connecting-screen) describes the
  screen that shows this.

## Hole punching

**NAT** is what a home router does when several devices share one internet
address. Such a router lets answers in, but it blocks a connection that starts
from outside. A host behind such a router is normally unreachable until its
owner forwards the game port.

**Hole punching** gets around this. The host sends a packet to the player
first. To the host's router the player's packets then look like answers, and
it lets them in. The lobby's part is to tell the host where the player is.

The steps:

1. The player picks a match in **JOIN GAME**. The **CONNECTING** screen opens
   and the player's game asks the server, over each listed address, whether
   it is there.
2. While there is no answer, the player's game asks the lobby for help, first
   after about a second and then every three seconds. It sends the request
   from the same port it will use for the game, once for each kind of address
   the lobby can be reached over.
3. The lobby tells the host the address and port the request came from.
4. The host sends a small packet from its game port to that address.
5. The player's game is still asking, and its next question gets through. The
   path stays open for the match that starts right after.

Nothing has to be set up for this beyond the lobby itself. If the host can be
reached directly, the first attempt succeeds and none of this is needed.

### Limits

- **The host must be on the same lobby's list.** The lobby can only help with
  matches it knows.
- **The player must use the listed address.** Picking the match from the list
  does this. Typing the address in **JOIN BY ADDRESS** works too, but only if
  it is written exactly as the list shows it.
- **The host's router must keep the port number.** The lobby lists a host
  under the address it saw, with the port number the host reported. If the
  router changes that port number on its public side, players are sent to the
  wrong port.
- **The player's router must not change the port per destination.** The host
  sends its packet to the address and port the lobby saw. If the player's
  router uses a different one towards the host, the packet opens nothing.
- **There is no relay.** If hole punching fails, the lobby does not carry the
  game instead. Forward the host's port.
- **The attempt ends after 25 seconds.** The player is then returned to
  **JOIN GAME** with the message `No answer from <server>.`
- **IPv6 needs the lobby on IPv6.** A router's firewall blocks unexpected
  IPv6 packets just as it blocks IPv4 ones. The lobby can only open an IPv6
  path when the player reaches the lobby over IPv6.

### From the command line

A join typed on the command line does not ask a lobby for help unless you name
the lobby and the server:

```sh
nnbuild --name DUKEFAN --punchtarget 203.0.113.10:23513 --masteraddr lobby.example.org --net /n0 203.0.113.10:23513
```

- `--punchtarget` (short `-pt`) is the server's address as the lobby lists it.
- `--masteraddr` (short `-ma`) is the lobby's address.

The menu adds both for you when you join from **JOIN GAME**.

## Limits of the list

- A lobby holds up to 256 matches, of all games together.
- One internet address can have up to 8 matches on the list.
- The lobby answers up to 60 packets in 10 seconds from one address and
  ignores the rest. Ordinary play stays far below that. The limit stops a
  public lobby from being misused to flood somebody else.
- Names are limited to plain printable characters; anything else is shown as
  `?`.
- The list is sent to a player in one packet. A very long list is cut short.
- **JOIN GAME** shows the first 12 matches of your game and `serverlist` the
  first 64 of all games.

## Other games

"No" means the code of that runtime does not contain the feature.

| | Duke Nukem 3D family | Shadow Warrior | Blood | Redneck Rampage | Exhumed |
| --- | --- | --- | --- | --- | --- |
| Report a match with `--advertise <lobby>` | Yes | Yes | Yes | Yes | Yes |
| Server list in the game | **JOIN GAME** | **Browse Servers** | No | No | No |
| Print a lobby's list at startup with `--lobbylist <lobby>` | No | Yes | Yes | Yes | Yes |
| Join the first listed match with `--joinlobby <lobby>` | No | Yes | Yes | Yes | Yes |
| Hole punching when hosting | Yes | Yes | No | No | Yes |
| Hole punching when joining | Yes | Yes | Only the request is sent | Only the request is sent | Yes |

Notes:

- Shadow Warrior's **Browse Servers** screen asks the lobby given at launch
  with `--masteraddr <lobby>`, or the public lobby without it. Press Enter to
  join and R to refresh. Joining works as in the Duke Nukem 3D family: the
  server is asked over its IPv4 and its IPv6 address at once, IPv6 is
  preferred, the screen shows the progress, and Esc cancels.
- For a join by address in Shadow Warrior and Exhumed, `--punchto <server>`
  and `--masteraddr <lobby>` ask the lobby for help, as `--punchtarget` and
  `--masteraddr` do in the Duke Nukem 3D family.
- In Blood and Redneck Rampage, a host does not act on a lobby's request to
  open a path. Forward the host's port for these games.
