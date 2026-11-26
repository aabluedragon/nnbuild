# Launch options

Launch options are the words you add after the program name when you start
NNBuild. This page lists them by purpose.

```sh
vibebuild32 --game duke --skip-menu --volume 1 --level 3
```

## How options are read

NNBuild reads options in two steps.

1. The **launcher** runs first. It is the same for every game. It picks the
   game, handles the options marked *All* below, and rewrites a few options so
   that every game understands them.
2. The **runtime** of the chosen game reads the rest. A runtime is the part of
   NNBuild that runs one game or one family of games. Each runtime has its own
   options.

The tables use these names for the runtimes:

| Name in the tables | Games |
| --- | --- |
| Duke | Duke Nukem 3D, World Tour, Ion Fury, NAM, NAPALM, WWII GI |
| SW | Shadow Warrior |
| Blood | Blood |
| RR | Redneck Rampage |
| Exhumed | Exhumed / PowerSlave |
| All | Handled by the launcher, for every game |

### Writing options

- Long names start with two dashes (`--game`). Short names start with one
  (`-gm`).
- A value follows its option as the next word: `--game duke`. You can also
  write `--game=duke`.
- Some short options accept the value attached: `-l3`, `-v1`, `-g/path/file.grp`,
  `-res1280x720`. Where this works, the table says so.
- A value can be attached to a long name only when it is a number or a path:
  `--level1`, `--grp/path/file.grp`. Anything else needs a space or `=`.
- Give full paths for files and folders. NNBuild changes folder before the
  game starts, so a path relative to where you typed the command may not be
  found.
- Put `-net` last. Everything after `-net` belongs to the network setup. See
  [The `-net` block](#the--net-block).
- Use the spelling shown in the tables. Many options have older spellings, but
  not all of them are understood by every game.

### Help

| Option | Runtime | What it does |
| --- | --- | --- |
| `-?`, `-help`, `--help` | Duke, Exhumed | Shows the option list of that runtime. Depending on the game and the system it appears in a message box or in the log. |
| `-?`, `-h`, `-help` | SW, Blood, RR | The same, for these runtimes. |
| `-debughelp` | Duke, RR | Shows the developer options. |

NNBuild picks the game before any help is shown, so add `--game <name>` to get
the help of a particular runtime.

## Choosing the game

| Option | Other spellings | Argument | Runtime | What it does |
| --- | --- | --- | --- | --- |
| `--game` | `-gm`, `-game` | game name | All | Chooses the game. Names are listed in [Supported games](../games/overview.md). The value may be attached to `-gm` (`-gmduke`). |
| `--skip-menu` | `-sm` | | All | Skips the game picker and the startup window. |
| `--no-game-menu` | `-ngm`, `-nogamemenu` | | All | Skips only the game picker. The first installed game is used. |
| `--menu` | `-m`, `-setup` | | All | Shows the startup window even when it would be skipped. |
| `-nosetup` | | | All | Skips the startup window. `--skip-menu` adds this for you. |
| `--skip-cinema` | `-sc`, `-nologo` | | All | Skips the logos and intro movie. |
| `-quick` | | | SW, Blood, Exhumed | Skips the intro screens. |

What these do step by step is described in
[First launch](../getting-started/first-launch.md).

## Game data and mods

| Option | Other spellings | Argument | Runtime | What it does |
| --- | --- | --- | --- | --- |
| `--gamegrp` | `-gamegrp` | file | Duke, SW, RR, Exhumed | Names the main data file of the game. Also used to pick the game. |
| `--grp` | `-grp`, `-g` | file | All | Loads a data file. Also used to pick the game. The value may be attached to `-g`. See the notes below. |
| `--game-dir` | `-game_dir` | folder | Duke, SW, Blood, RR | Names the game's data folder. Also used to pick the game. In Duke, Blood and RR it becomes the folder for custom content. |
| `--search-path` | `--path`, `-j` | folder | All | Adds a folder to search for files. Also used to pick the game. The value may be attached to `-j`. |
| `--rff` | `-rff` | file | Blood | Uses another main `.rff` archive. |
| `--ini` | `-ini` | file | Blood | Uses another episode `.ini` file. |
| `-snd` | | file | Blood | Uses another sound `.rff` archive. |
| `-art` | | file | Blood | Uses another base art file name. |
| `--con` | `-x` | file | Duke, RR | Loads another main CON script. CON is the script language of these games. |
| `-mx` | | file | Duke, RR | Loads an additional CON script module. |
| `--def` | | file | Duke, RR | Loads another definitions (`.def`) file. |
| `-h<file>` | | attached | Duke, SW, RR | The same as `--def`, with the file name attached: `-hmy.def`. |
| `-h` | | file | Exhumed | Loads another definitions file. |
| `-mh` | | file | Blood, RR, Exhumed | Loads an additional definitions module. In SW attach the name: `-mhextra.def`. |
| `-rts` | | file | Duke, RR | Loads another Remote Ridicule (`.rts`) sound bank. |
| `-noautoload` | | | Duke, Blood, RR, Exhumed | Does not load files from the `autoload` folder. |
| `-addon0`, `-addon1`, `-addon2` | | | SW | Starts Shadow Warrior, Wanton Destruction or Twin Dragon. |
| `-addon` | | number | RR | Selects a numbered add-on. |
| `-kex` | | folder | Exhumed | Turns on [KEX mode](../games/exhumed-powerslave.md#kex-mode) and names the PowerSlave Exhumed install folder. |
| `-nam`, `-napalm`, `-ww2gi` | | | Duke | Older way to switch the runtime to NAM, NAPALM or WWII GI. Prefer `--game`. |

Notes on `--grp`:

- When the file is the main data file of World Tour, Ion Fury, NAM, NAPALM,
  WWII GI, Redneck Rampage or Exhumed, the launcher treats it like
  `--gamegrp`.
- When the file belongs to a different game than the one running, it is not
  loaded as game data. It is kept as a source of levels and art. See
  [Playing one game's levels in another](../games/overview.md#playing-one-games-levels-in-another).
- In Blood, a `.rff` file is passed on as `--rff` and a `.ini` file as `--ini`.
- In the Duke runtime, a file given with `--grp` that is not recognised game
  data is a mod pack. In multiplayer it is offered to the players who join.
  Recognised game data, such as `DUKE3D.GRP` or an official add-on, is never
  offered: every player needs their own copy.
- To start an official add-on, name its data file with `--gamegrp`.

### Playing one game's levels in another

| Option | Other spellings | Argument | Runtime | What it does |
| --- | --- | --- | --- | --- |
| `--game.levels` | `-gm.lvls` | game name | All | The game the levels come from. Used by the Duke and SW runtimes. |
| `--game.items` | `-gm.its` | game name | All | The game whose item pickups to use. |
| `--game.weapons` | `-gm.wpns` | game name | All | The game whose weapon and ammo pickups to use. |

## Video

| Option | Other spellings | Argument | Runtime | What it does |
| --- | --- | --- | --- | --- |
| `--window` | `-wnd` | | Duke | Starts in a window. |
| `--fullscreen` | `-fscr` | | Duke | Starts full screen. |
| `--resolution` | `-res` | `WIDTHxHEIGHT` | Duke | Sets the resolution, from `320x200` to `8192x4320`. The value may be attached: `-res1280x720`. |

These options override the saved video settings for this run. In the other
games, set the video mode in the game's own options menu.

## Starting a level

| Option | Other spellings | Argument | Runtime | What it does |
| --- | --- | --- | --- | --- |
| `--level` | `-l` | number | Duke, SW, RR | Starts at that level. The value may be attached: `-l3`. |
| `--volume` | `-v`, `--episode`, `-e` | number | Duke, RR | Starts in that episode. The value may be attached: `-v1`. |
| `-level` | | episode and level | Blood | Starts at that level: `-level 1 3`. |
| `-<n>` | | | Exhumed | Jumps to level `n`: `-3`. |
| `-<n>` | | | Duke | Loads the saved game in slot `n` (0 to 9). Single player only. |
| `-map` | | file | Duke, SW, Blood, RR, Exhumed | Loads a map file. Not available with shareware data in Duke and SW. |
| `--skill` | `-s<n>` | number | Duke, RR | Sets the skill level. |
| `-s<n>` | | attached | SW | Skill, 1 to 4. |
| `-s` | | number | Blood | Difficulty, 0 to 4. |
| `--monsters` | `-mnst` | number | Duke, Blood | `0` turns monsters off. `1` or more turns them on and sets the skill to that number minus one. In RR only `--monsters 0` is supported. |
| `-monst` | | | SW | No monsters. |
| `-nodudes` | | | Blood | No monsters. |
| `-nocreatures` | | | Exhumed | No enemies. |
| `-name` | | name | Duke, SW, Blood, RR, Exhumed | Your player name. Blood also accepts `-pname`. |

## Game rules (Duke runtime)

| Option | Other spellings | Argument | What it does |
| --- | --- | --- | --- |
| `-c<n>` | `--coop` | number | Multiplayer game mode: 1 Dukematch, 2 Co-op, 3 Team Dukematch. Attach the number: `-c2`. |
| `-t<n>` | `--respawn` | `1`, `2`, `3` or `x` | Respawn: 1 monsters, 2 items, 3 inventory, `x` all. Attach the value: `-t2`. |
| `-y<n>` | | number | Frag limit. Attach the number: `-y20`. |
| `-u<digits>` | `--weapons` | digits | Your preferred weapon order: `-u3425689071`. |
| `-wstay` | | | Weapons stay after pickup. |
| `-noffire` | | | Friendly fire off. |
| `-allowvisibility` | | | Allows visibility adjustment in multiplayer. |

`-c` on its own is a different option: see `--cmd` below. RR accepts `-c<n>`,
`-t<n>` and `-u<digits>` as well.

## Sound, settings and startup commands

| Option | Other spellings | Argument | Runtime | What it does |
| --- | --- | --- | --- | --- |
| `-nosound` | `-ns` | | Duke | Sound effects off. RR accepts `-ns`. Exhumed accepts `-nosound`. |
| `-nomusic` | `-nm` | | Duke | Music off. RR accepts `-nm`. Exhumed accepts `-nomusic`. |
| `-cfg` | | file | Duke, Blood, RR, Exhumed | Uses another settings file. |
| `-usecwd` | | | All runtimes | Keeps settings in the folder the game runs from and skips the search for installed copies. |
| `--cmd` | `-c` | text | Duke | Runs console commands when the game is ready. Put several commands in one quoted argument, one per line, or repeat the option. |
| `--cmd-file` | `-cf` | file | Duke | Reads startup console commands from a text file, one per line. Lines that start with `#` are ignored. |
| `-cachesize` | | size in kB | Duke, Blood, RR, Exhumed | Sets the size of the file cache. |

```sh
vibebuild32 --game duke --cmd "skin 6" --cmd "echo Ready"
```

Console commands are listed in [Console commands](console-commands.md).

## Recording and playing demos

| Option | Other spellings | Argument | Runtime | What it does |
| --- | --- | --- | --- | --- |
| `--record` | `-r` | | Duke, RR | Records a demo. |
| `--playback` | `-pb` | file, optional | Duke | Plays a demo file. `.dmo` is added when the name has no extension. |
| `-d<file>` | `--demo` | file | Duke, RR | Plays a demo file. |
| `--record`, `--playback` | | file for playback | SW | Records or plays a demo. `-dr<file>` and `-dp<file>` do the same with the file name attached. |
| `-record`, `-playback` | | | Exhumed | Records to or plays from the file `DATA.VCR`. |

## Multiplayer with `-net`

The Duke, SW and Exhumed runtimes start network games with `-net`.

| Option | Other spellings | Argument | Runtime | What it does |
| --- | --- | --- | --- | --- |
| `-net` | `--net` | network words | Duke, SW, Exhumed | Starts a network game. See [The `-net` block](#the--net-block). |
| `--startnow` | `-sn`, `-nowait` | | Duke | Host: start at once with whoever is present, even alone. The other slots stay open and players can join at any time. |
| `--startnow` | `-sn` | | Exhumed | Host: start with the players who are present. |
| `--latejoin` | `-lj` | | Duke, Exhumed | Host: let players join while the match is running. |
| `--async` | `-as` | | Duke | Host: never make everyone wait for one slow player. Only that player feels the delay. Without it, all players wait for each other. |
| `--zeroping` | | | Duke | Shows your own shots at once instead of waiting for the server. Each player sets this for themselves. |
| `--nplayers` | `-np` | number | Exhumed, RR | Number of players to wait for before the match starts. The default is 2. |
| `--netfile` | `-nf` | file | Exhumed, RR | Host: send this file to players who join. Blood accepts `--netfile` only. |

Rules for the Duke runtime:

- `--startnow` includes `--latejoin`.
- A host that only gives `-net` waits until every slot is filled.
- Mod packs given with `--grp` and map files are sent to joining players
  automatically. There is no `--netfile` in this runtime.

### The `-net` block

Everything after `-net` describes the connection. The words can start with `/`
or `-`.

| Word | Meaning |
| --- | --- |
| `/n0` | Host-and-clients mode: one computer is the host and the others connect to it. |
| `/n0:<count>` | Host in that mode with `count` player slots, for example `/n0:4`. |
| `/n1` | Peer-to-peer mode: every computer connects to every other. |
| `/n1:<count>` | Peer-to-peer with `count` slots, leaving room for late joiners. |
| `/p<port>` | The UDP port this computer uses, for example `/p23513`. It must be between 1025 and 65534. The default is 23513. |
| *address* | A computer to connect to. Any word that is not one of the above is an address. |

An address can be written as:

- `192.0.2.10` or `192.0.2.10:23513` (IPv4, with or without port),
- `[2001:db8::1]:23513` (IPv6 with port) or `2001:db8::1` (IPv6 alone),
- `host.example.org` or `host.example.org:23513` (a name).

Without a port, 23513 is used. An address with no `/n` word means `/n0`.

```sh
# Host four slots on the default port and start at once
vibebuild32 --game duke --startnow -net /n0:4

# Host on another port
vibebuild32 --game duke --startnow -net /p23600 /n0:4

# Join a host
vibebuild32 --game duke -net /n0 192.0.2.10:23513

# Join when your own port has to differ from the host's (same computer)
vibebuild32 --game duke -net /p23514 /n0 127.0.0.1:23513
```

In host-and-clients mode the host lists no addresses, and each client lists
the host.

In peer-to-peer mode each player lists the addresses of the other players.
All players use the same player order, and the place where `/n1` stands among
the addresses is your own place in that order. With three players at
`192.0.2.2`, `192.0.2.100` and `192.0.2.4`:

```text
first player:    -net /n1 192.0.2.100 192.0.2.4
second player:   -net 192.0.2.2 /n1 192.0.2.4
third player:    -net 192.0.2.2 192.0.2.100 /n1
```

On Linux and macOS you can instead give every player the same list, with all
addresses including their own, in any order. NNBuild finds its own address in
the list.

In the Duke runtime the slot count includes a dedicated server. A dedicated
server started with `/n0:4` has room for three players.

## Multiplayer in Blood and Redneck Rampage

These two games do not use `-net`.

### Blood

| Option | Argument | What it does |
| --- | --- | --- |
| `-server` | players | Starts a server for that many players (1 to 8). |
| `-client` | host | Connects to a server. |
| `-port` | number | The server's port. The default is 23513. |
| `-clientport` | number | The local port a client uses. |
| `-netretry` | | Client: keep trying to connect. Hold Escape to stop. |
| `-auto` | | Starts the network game automatically. Also skips the intro. |
| `-broadcast`, `-masterslave` | | Selects the packet mode. |
| `-mp_mode` | 0 to 2 | Game mode: 0 co-op, 1 Bloodbath, 2 teams. |
| `-mp_level` | episode and level | Level for the match: `-mp_level 1 3`. |
| `-mp_map` | file | Map file for the match. |
| `-mp_mapclient` | file | Client: use another map file than the one the server names. |
| `-mp_diff` | 0 to 4 | Difficulty. |
| `-mp_dudes` | 0 to 2 | Monsters: 0 none, 1 spawn, 2 respawn. |
| `-mp_weaps` | 0 to 3 | Weapons: 0 do not respawn, 1 permanent, 2 respawn, 3 respawn with markers. |
| `-mp_items` | 0 to 2 | Items: 0 do not respawn, 1 respawn, 2 respawn with markers. |
| `-mp_spawn` | 0 to 2 | Spawn location: 0 random, 1 smart random, 2 by distance. |
| `-mp_protect` | 0 to 3 | Length of spawn protection. |
| `-mp_secwind` | 0 or 1 | Second wind. |
| `-mp_score` | 1 to 255 | Score or lives limit. |
| `-mp_time` | 1 to 60 | Time limit. |
| `-mp_noexit` | | Disables the exit button. |
| `-mp_noflag` | | Removes the flag in teams mode. |
| `-mp_chaseview` | | Allows chase view. |
| `-mp_holstering` | | Allows holstering. |
| `-mp_spectating` | | Allows spectating. |

Blood stops with an "Invalid argument" error when it is given an option it
does not know. Only use options listed for Blood on this page.

### Redneck Rampage

| Option | Argument | What it does |
| --- | --- | --- |
| `-server` | | Starts a server. |
| `-connect` | host or host:port | Connects to a server. |
| `-port` | number | The port to host on or connect to. The default is 23513. |
| `-password` | text | The server password. |
| `--startnow` | | Server with a window: start the match automatically when enough players are in. |
| `--wait` | | Server without a window: do not start the match automatically. |

## Dedicated server

A dedicated server is a copy of the game that only hosts. It has no window,
graphics or sound.

| Option | Other spellings | Runtime | What it does |
| --- | --- | --- | --- |
| `--dedicated` | `-dd` | Duke, RR, Exhumed | Runs as a server without a window. |
| `--headless` | `-hl`, `--console` | Duke, RR, Exhumed | The same. |
| `--wait` | `-w` | RR, Exhumed | Do not start the match automatically. |

- **Duke.** The server reads console commands typed in its terminal. It takes
  one of the player slots but does not appear in the match. Combine it with
  `--startnow` and `-net`. See
  [Dedicated server](../multiplayer/dedicated-server.md).
- **Exhumed.** The server starts the match by itself. On Linux and macOS it
  reads console commands typed in its terminal.
- **RR.** The server waits for players and starts the match by itself. It has
  no console in the terminal.

```sh
vibebuild32 --game duke --dedicated --startnow -name Server -net /p23513 /n0:8
```

## Lobby server

A lobby server keeps a list of games that players can join. NNBuild can list a
game on a lobby server, read the list, and be the lobby server.

| Option | Other spellings | Argument | Runtime | What it does |
| --- | --- | --- | --- | --- |
| `-masterserver` | | port, optional | All | Runs NNBuild as a lobby server and nothing else. No game is chosen and no game data is needed. The default UDP port is 23519. |
| `--advertise` | `-ad` | address | Duke, SW, RR, Exhumed | Host: list this game on the lobby server at that address. Blood accepts `--advertise` only. In RR it also starts a server. |
| `--lobbylist` | `-ll` | address | SW, Blood, RR, Exhumed | Writes the games listed on that lobby server to the log, then continues starting. Blood accepts `--lobbylist` only. |
| `--joinlobby` | `-jl` | address | SW, Blood, RR, Exhumed | Joins the first game listed on that lobby server. SW and Blood accept `--joinlobby` only. |
| `-masteraddr` | | address | SW, Exhumed | The lobby server to use for connection help and, in SW, for the server list in the menu. |
| `-punchto` | | address | SW, Exhumed | Client: the host to ask the lobby server for connection help with. |

A lobby address is a host name or IP address, optionally followed by `:port`.
Without a port, 23519 is used.

```sh
# Run a lobby server on the default port
vibebuild32 -masterserver

# The same on another port, on a computer without a display
vibebuild32 --headless -masterserver 24000

# Host a Duke Nukem 3D match and list it
vibebuild32 --game duke --startnow --advertise lobby.example.org -net /n0:4
```

Notes:

- Write `-masterserver` with one dash.
- `--advertise` has to come before `-net`.
- In the Duke runtime, players find listed games in the **Multiplayer** menu
  or with the `serverlist` console command. The address the menu asks is the
  console setting `cl_masterserver`. See
  [Console commands](console-commands.md#cl_masterserver).
- The name shown in the list is the host's player name (`-name`).

## Script mods

These options belong to the JavaScript and TypeScript mod system. Mods run in
the Duke and SW runtimes. See [Modding](../modding/overview.md).

| Option | Other spellings | Argument | Runtime | What it does |
| --- | --- | --- | --- | --- |
| `--js` | `-js` | file | Duke, SW | Loads a script file (`.js` or `.ts`). Works anywhere on the command line. |
| *script file* | | | Duke | A file name ending in `.js`, `.ts`, `.mjs`, `.mts`, `.cjs` or `.cts`, given without an option, is loaded as a script. |
| `--gen-ts` | `-gts` | name, optional | All | Creates a TypeScript mod starter in `autojs/<name>/` and exits. No game is started. |
| `--gen-js` | `-gjs` | name, optional | All | The same for JavaScript. |
| `--debug` | `-dbg` | port, optional | Duke, SW | Turns on the script debugger connection for VS Code. The default port is 8501. Also skips the menus. The port can be attached (`-dbg8600`) and is honoured by the Duke runtime. |
| `--devtools` | `-dt` | | Duke, SW | Opens the script DevTools window. Includes `--debug`. In the Duke runtime it also forces windowed mode. |

Mods in `autojs/<name>/index.ts` or `index.js` are loaded automatically. The
`autojs` folder is looked for in the folder you start NNBuild from. When a name
is left out of `--gen-ts` or `--gen-js`, the next free name of the form `mod-1`,
`mod-2` is used.

The `index.ts` or `index.js` of a mod in `autojs` can carry extra launch
options for debugging, in a comment at its top. They are used only when NNBuild
is started with `--debug` or `--devtools`:

```ts
/// vbd.params="--game duke --gamegrp /home/you/games/duke3d/DUKE3D.GRP"
```

## Map editor and projects

These options belong to the Duke runtime. See
[Map editor](../editor/overview.md).

| Option | Argument | What it does |
| --- | --- | --- |
| `--editor` | map file, optional | Opens the built-in map editor, with that map if one is named. |
| `--build-editor` | map file, optional | Opens the classic Build editing tools. |
| `--project` | folder or ZIP | Plays a project. With `--editor`, opens it for editing. |
| `--project-map` | map path inside the project | Chooses which map of the project to play or edit. |
| `--allow-project-scripts` | | Lets the script files inside a project ZIP run. They do not run otherwise. |

A project ZIP given without an option is played as a project. This is what
happens when you drop the ZIP onto the program.

```sh
vibebuild32 --game duke --gamegrp ~/games/duke3d/DUKE3D.GRP --editor
vibebuild32 "/home/you/projects/My Project.zip"
vibebuild32 "/home/you/projects/My Project.zip" --project-map "Maps/Outpost.map"
vibebuild32 --game duke -j ~/games/duke3d --editor --project "/home/you/projects/My Project"
```

Write these options exactly as shown, with two dashes.

## MCP

MCP is an open standard that lets an AI assistant control a program. These
options belong to the Duke runtime. See
[AI assistants and MCP](../editor/ai-and-mcp.md).

| Option | Argument | What it does |
| --- | --- | --- |
| `--mcp-stdio` | | Lets an MCP client control this game, server or editor through the program's standard input and output. |
| `--mcp-http` | port | The same through a local web address, `http://127.0.0.1:<port>/mcp`. Only this computer can reach it. |
| `--editor-mcp` | host or host:port, optional | Joins a live map-editing session as an MCP participant instead of starting a game. No game is chosen and no game data is needed. The default host is this computer. Everything after this option belongs to it. |

After `--editor-mcp` you can give `--name <name>` for the participant's label
and `--mcp-http <port>` to offer it over the local web address instead of
standard input and output.

```sh
vibebuild32 --game duke --dedicated --startnow --mcp-http 8787 -net /n0:4
vibebuild32 --editor-mcp 127.0.0.1:23514 --name "Layout Assistant"
```

Two environment variables protect the web address. When `VIBEBUILD_MCP_TOKEN`
is set, a client must send that value as a bearer token. `VIBEBUILD_MCP_ORIGIN`
names one extra web origin that is allowed to connect.

## Files given without an option

A file name without an option in front of it is handled by its ending. This
is also what happens when you drop a file onto the program.

The launcher looks at such a file first, to work out the game. Then the
runtime uses it:

| Runtime | Ending | What happens |
| --- | --- | --- |
| Duke | `.map` | The map is loaded. |
| Duke | `.grp`, `.zip`, `.pk3` | Loaded as a data file. A ZIP that is a project is played as a project. |
| Duke | `.con` | Loaded as the main CON script. |
| Duke | `.def` | Loaded as the definitions file. |
| Duke | `.rts` | Loaded as the Remote Ridicule sound bank. |
| Duke | `.js`, `.ts`, `.mjs`, `.mts`, `.cjs`, `.cts` | Loaded as a script mod. |
| Blood | `.map`, or no ending | The map is loaded. |
| Blood | `.grp`, `.zip`, `.pk3`, `.pk4` | Loaded as a data file. |
| Blood | `.def` | Loaded as the definitions file. |
| RR | `.map` | The map is loaded. |
| RR | `.grp`, `.zip`, `.pk3`, `.pk4` | Loaded as a data file. |
| RR | `.con`, `.def`, `.rts` | Loaded as the main script, definitions file or sound bank. |
| SW, Exhumed | any | Not used by the runtime. Use `-map`, `--grp` and `-j`. |

## More options of single runtimes

### Shadow Warrior

| Option | Argument | What it does |
| --- | --- | --- |
| `-nodemo` | | No demos at the title screen. |
| `-nometers` | | Hides the air and boss meter bars. |
| `-nopredict` | | Turns off network prediction. |
| `-f2`, `-f4`, `-f8` | | Packet duplication in network games. |
| `-autonet` | ten numbers separated by commas | Starts a network game with preset rules: rules, level, enemies, markers, team play, hurt teammate, kill limit, time limit, colour, nuke. |
| `--color` | colour name or `#rrggbb` | Your player colour in a network game. Names: `brown`, `gray`, `purple`, `red`, `yellow`, `olive`, `green`, `blue`. A `#rrggbb` value picks the closest of these. |
| `-movescale`, `-turnscale` | number | Movement and turning scale. 256 means one unit. |
| `-extcompat` | | Controller compatibility mode. |
| `-nocontroller` | | Turns controller support off. |

### Blood

| Option | Argument | What it does |
| --- | --- | --- |
| `-nodemo` | | No demos. |
| `-noaim` | | Turns auto-aim off. |
| `-skill` | 0 to 4 | Player handicap. This is not the difficulty. |
| `-setupfile` | file | The same as `-cfg`. |

### Redneck Rampage

| Option | Argument | What it does |
| --- | --- | --- |
| `--players` | number | Fake multiplayer with that many players, for testing. |
| `-rotatesprite-no-widescreen` | | Stretches screen drawing from scripts to the full screen. |
| `-nocontroller` | | Turns controller support off. |

### Exhumed

| Option | Argument | What it does |
| --- | --- | --- |
| `-color` | 0 to 7 | Your player colour in a network game. |
| `-t<n>` | attached number | Time setting for network games. |

## Developer options

These are for testing and for the program's own use.

| Option | Runtime | What it does |
| --- | --- | --- |
| `-q<n>`, `--players <n>` | Duke | Fake multiplayer with `n` players on one computer. |
| `-a` | Duke | Simple computer control for the other players in fake multiplayer. |
| `-w` | Duke | Shows your coordinates. |
| `-condebug`, `-z<n>` | Duke, RR | CON script compile debugging. |
| `-nD` | Duke | Writes the default script variables to a file. |
| `-unstable` | Duke | Turns off the safety checks of the CON script system. |
| `-stun` | Duke | Older UDP hole-punching mode for `-net`. |
| `-noinstancechecking` | All runtimes | Windows: do not ask when NNBuild is already running. |
| `-punchtarget`, `-masteraddr`, `-joinerror` | Duke | Added by NNBuild when it restarts itself to join a game from the menu. |
| `--content-probe`, `--content-rejoin`, `--server-resume` | Duke | Added by NNBuild when a server changes its mod pack. |
| `-validate`, `-robust`, `-noresend` | Blood | Compatibility test and network checking switches. |
| `-short`, `-cheat`, `-allsync`, `-cam` | SW | Test switches. |

## Environment variables

| Variable | What it does |
| --- | --- |
| `DUKE3DGRP` | Duke runtime: the main data file to use when none is named on the command line. |
| `DUKE3DDEF` | Duke runtime: the definitions file to use when none is named. |
| `SWGRP` | Shadow Warrior: the main data file to use when none is named. |
| `BLOODDEF` | Blood: the definitions file to use when none is named. |
| `VIBEBUILD_MCP_TOKEN`, `VIBEBUILD_MCP_ORIGIN` | See [MCP](#mcp). |
| `EDUKE32_LOGSTDOUT` | Set to `TRUE` to write the program's console output to `stdout.txt`. |
