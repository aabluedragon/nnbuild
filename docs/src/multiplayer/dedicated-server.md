# Running a dedicated server

A dedicated server hosts a match and does not play in it. It can run on a
rented machine or a spare computer and stay up while players come and go.

Read [Hosting a match](hosting.md) first. Everything there about slots, ports,
game modes, mods and the lobby list also applies to a dedicated server. This
page adds what is special about a server that runs on its own.

The page describes the Duke Nukem 3D family (Duke Nukem 3D, Duke Nukem 3D:
World Tour, NAM, NAPALM, World War II GI and Ion Fury). See
[Other games](#other-games) at the end for the rest.

## What a dedicated server is

Start NNBuild with `--headless` (short `-hl`) or `--dedicated` (short `-dd`).
In the Duke Nukem 3D family the two options do the same thing:

- **The server is headless.** Headless means it has no window, no graphics and
  no sound, and it does not look for game controllers. It needs only a
  terminal, so it runs on a machine without a screen and over SSH.
- **The server reads commands from its terminal.** Whatever you type on its
  standard input is run as a console command. This is the admin console.
- **The server does not play.** It holds the first slot as a spectator that
  cannot be seen, cannot be hit and is left out of the scoreboard and the frag
  bar. With `/n0:8` there is room for 7 players.

A dedicated server has nobody at the keyboard, so always give it:

- `--startnow`, so that the match starts by itself and players can join at any
  time,
- a game mode and a starting level.

```sh
vibebuild32 --headless --startnow --name ARENA --coop 1 --volume 1 --level 1 --net /p23513 /n0:8
```

As on the hosting page, `--net` and what follows it must come last.

## The admin console

Type a command in the server's terminal and press Enter. The answer is printed
in the same terminal and written to the log.

```text
listplayers
Players:
  0: ARENA  (this machine)  [host]
  1: DUKEFAN  203.0.113.77:23530
```

In the terminal each line of the answer also carries the log's usual prefix,
which is left out here.

A line you type can be up to 4096 characters long. The commands are described
in [Administering a server](server-admin.md).

## Startup commands

You can give the server console commands to run when it starts. They run in
the order they appear on the command line, as soon as the server is up.

| Option | Meaning |
| --- | --- |
| `--cmd <text>` (short `-c <text>`) | Run this console command. The text may hold several commands, one per line. |
| `--cmd-file <path>` (short `-cf <path>`) | Run the commands in this text file, one per line. |

Both options can be given more than once.

```sh
vibebuild32 --headless --startnow --name ARENA --coop 1 --volume 1 --level 1 \
  --cmd-file startup.txt --cmd "echo Server ready" \
  --net /p23513 /n0:8
```

In a command file:

- each line is one command,
- empty lines are skipped,
- a line that starts with `#` is a comment,
- spaces and tabs at the start of a line are ignored.

```text
# startup.txt
sv_maprotation 1
fraglimit 20
mapcycle list
```

If the file cannot be read, the server does not start and the log says
`Could not read startup command file`. A command file can be up to 1 MB.

`-c` followed by a space is a startup command. `-c1`, `-c2` and `-c3`, with
the number attached, are the older way to set the game mode. Use `--cmd` and
`--coop` to keep the two apart.

## Server scripts

A server can run a script written in JavaScript or TypeScript, for example to
make timed announcements or to answer chat commands such as `!rules`. Give the
script file on the command line, either by itself or after `--js`:

```sh
vibebuild32 --headless --startnow --name ARENA --coop 1 --volume 1 --level 1 \
  --js server.ts --net /p23513 /n0:8
```

[Server scripts](../modding/server-scripts.md) explains how to write one.

## Control through MCP

MCP is an open standard that lets an AI assistant operate a program. A server
can offer its console that way.

| Option | Meaning |
| --- | --- |
| `--mcp-http <port>` | Listen for MCP requests at `http://127.0.0.1:<port>/mcp`. Only programs on the same machine can connect. |
| `--mcp-stdio` | Speak MCP on standard input and output. The typed admin console is then not available. |

The tools that matter for a server are `server_status` (role, port, slots and
the list of connected players), `console_execute` (run a console command) and
`console_output` (read recent console messages).

If the environment variable `VIBEBUILD_MCP_TOKEN` is set, each HTTP request
must carry that value as a bearer token.

[AI assistants and MCP](../editor/ai-and-mcp.md) describes MCP in NNBuild in
full.

## Logs

- The server writes its log to `vibebuild32.log` in the folder it runs from.
  The file is started afresh each time the server starts. When the server
  restarts itself for a change of mod pack, it continues the same file.
- The same lines are printed to the terminal, on standard error.
- The answers to console commands appear in both places.

## Run under a process supervisor

A process supervisor starts the server when the machine boots and starts it
again if it stops. This is a sample unit for systemd. It assumes a server
directory prepared as in the [worked example](#worked-example) below, placed
at `/srv/nnbuild`, and a user named `nnbuild` that owns it.

```ini
[Unit]
Description=NNBuild dedicated server
After=network-online.target
Wants=network-online.target

[Service]
User=nnbuild
WorkingDirectory=/srv/nnbuild
ExecStart=/srv/nnbuild/vibebuild32 -usecwd --game duke --headless --startnow --name "ARENA ONE" --coop 1 --volume 1 --level 1 --cmd-file startup.txt --net /p23513 /n0:8
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

Save it as `/etc/systemd/system/nnbuild.service`, then:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now nnbuild
journalctl -u nnbuild -f
```

Things to know:

- **There is no keyboard.** A service has no terminal on standard input, so
  you cannot type admin commands. Put the commands the server needs in the
  startup file, use a server script, or add `--mcp-http` and send commands
  through it. If you want to type commands by hand, run the same command line
  inside a terminal multiplexer such as `tmux` or `screen` instead of as a
  service.
- **Name the game.** `--game duke` makes NNBuild run Duke Nukem 3D whatever
  other game data it finds. A server without a terminal is never asked which
  game to run. Without `--game` it runs the first game it finds data for.
- **A change of mod pack restarts the server in place.** On Linux and macOS
  the server replaces itself within the same process, so the supervisor
  notices nothing. On Windows the server starts a new process and the old one
  ends. See
  [Switch mod packs on a live server](server-admin.md#switch-mod-packs-on-a-live-server).
- **Keep the files together.** `-usecwd` keeps the settings file, the ban list
  and the download cache in the working directory instead of the user's
  profile folder.
- **The log goes to the journal.** systemd collects what the server prints.
  The file `vibebuild32.log` in the working directory is written as well.

## Worked example

This example goes from an empty directory to a public server for Duke Nukem
3D with 7 player slots, a three-map rotation and an entry on a lobby list.

1. Make a directory for the server and go into it.

   ```sh
   mkdir ~/nnbuild-server
   cd ~/nnbuild-server
   ```

2. Unpack NNBuild into the directory, so that `vibebuild32` and the files that
   came with it are there.

3. Copy your game data into the directory.

   ```sh
   cp ~/games/duke3d/DUKE3D.GRP .
   ```

4. Create `maplist.txt` with the levels to rotate through, one per line.

   ```text
   # maplist.txt
   E1L1
   E1L2
   E1L3
   ```

5. Create `startup.txt` with the commands to run at startup.

   ```text
   # startup.txt
   sv_maprotation 1
   fraglimit 20
   mapcycle list
   ```

   `sv_maprotation 1` turns on rotation in order. `fraglimit 20` ends a level
   when someone reaches 20 frags. `mapcycle list` prints the rotation so that
   you can check it in the log.

6. Start the server.

   ```sh
   ./vibebuild32 -usecwd --game duke --headless --startnow \
     --name "ARENA ONE" --coop 1 --volume 1 --level 1 \
     --advertise lobby.example.org \
     --cmd-file startup.txt \
     --net /p23513 /n0:8
   ```

   | Part | Meaning |
   | --- | --- |
   | `-usecwd` | Keep the settings file, the ban list and downloads in this directory. |
   | `--game duke` | Run Duke Nukem 3D without asking. |
   | `--headless` | No window, graphics or sound. Read commands from the terminal. The server does not play. |
   | `--startnow` | Start the match at once and let players join at any time. |
   | `--name "ARENA ONE"` | The server's name, as players see it. At most 10 characters. |
   | `--coop 1` | Dukematch. |
   | `--volume 1 --level 1` | Start on episode 1, level 1. |
   | `--advertise lobby.example.org` | Put the server on this lobby's list. Leave it out for a server that players reach by address only. |
   | `--cmd-file startup.txt` | Run the startup commands. |
   | `--net /p23513 /n0:8` | Listen on UDP port 23513 with 8 slots: the server and 7 players. |

7. Check the output. Among the lines you should find:

   ```text
   Headless dedicated server: no video, no sound (terminal only).
   Start-now enabled: match begins immediately, players may join anytime.
   mmulti: bound UDP port 23513 (IPv4+IPv6)
   Map cycle (3, sequential)
   ```

8. Open UDP port 23513 for incoming traffic: in the machine's firewall, and in
   the router if the machine is behind one.

9. Join from another computer, by picking the server in **JOIN GAME** or with:

   ```sh
   vibebuild32 --name DUKEFAN --net /n0 203.0.113.10:23513
   ```

   Then type `listplayers` in the server's terminal to see the new player.

To use your own lobby instead of someone else's, see
[The lobby server](lobby-server.md).

## Stop the server

Type `quit` (or `exit`) in the admin console. The server tells the lobby that
it is going away and then ends. It can take several seconds.

The players are not told that a dedicated server has stopped. Their games
stop moving forward, and each player has to leave by hand. A host that also
plays is different: when it quits, the other players' games close with
`Host has terminated the game.`

If the server is ended in another way, for example by a supervisor, its lobby
entry may stay on the list until it expires, 30 seconds after the last update.

## Other games

- **Exhumed.** `--headless` or `--dedicated` starts a server without a window.
  On Linux and macOS it reads console commands from its terminal. It starts
  the match by itself once enough players are connected. `--nplayers <n>` sets
  that number (2 if you do not say), and `--wait` turns the automatic start
  off. The console command `say <text>` sends a chat message to the players.
- **Redneck Rampage.** `--headless` or `--dedicated` starts a server without a
  window that waits in the pre-game lobby. It starts the match by itself once
  enough players are connected. `--nplayers <n>` sets that number (2 if you do
  not say), and `--wait` turns the automatic start off.
- **Shadow Warrior and Blood.** Both accept `--headless` and `--dedicated` and
  then open no window. The code does not show more than that for a dedicated
  server in these two games.

The admin commands in [Administering a server](server-admin.md) belong to the
Duke Nukem 3D family.
