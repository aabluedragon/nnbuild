# NNBuild Features

NNBuild is one program for playing, hosting and building for **Duke Nukem 3D**
and other games made with the Build engine. It is based on NetDuke32, a
community-made version of the game from the EDuke32 family. This page lists
what NNBuild adds on top of that, in plain language.

## At a glance

- Join a match that is already running, and come back if you drop out
- Find and host games from a Multiplayer menu inside the game
- Custom maps and mods download automatically when you join
- Run a dedicated server with admin tools, map rotation and scripts
- Several games in one program: Duke Nukem 3D, Ion Fury, NAM, WWII GI, Shadow
  Warrior, Blood, Redneck Rampage and Exhumed/PowerSlave
- Play as one of the enemies
- Build maps in a built-in editor that several people can use at the same time
- Write mods in JavaScript or TypeScript and debug them in VS Code

---

## Playing online

### Getting into a game

- **Mid-game join.** You no longer have to be there when the match starts. A
  host can leave spots open, and new players drop into the running match at one
  of the map's starting points, ready to play. The match pauses briefly while
  the newcomer is brought up to date.
- **Come back after a drop.** If your game crashes, your connection fails or
  you are dropped for lagging, you can rejoin the same match and your score is
  restored. The game recognizes you by an ID saved with your settings, not by
  your nickname, so someone else using your name does not inherit your score.
- **Multiplayer menu.** The main menu has a Multiplayer entry. From there you
  can host a game (public or local network) or join one, without typing any
  launch commands.
- **Game list.** Join Game shows the games people are hosting, with the server
  name, size and current map. Pick one to join, or type an address yourself.
  The list comes from a public lobby server that works out of the box.
  NNBuild can run as a lobby server too, so any community can keep its own
  list.
- **Connecting screen.** After you pick a game, a screen shows how the
  connection is going, and a Cancel button stops it. The game never freezes
  while it waits.
- **Fewer router headaches.** When a host sits behind a home router, the lobby
  server helps the two computers find each other. In many cases nobody has to
  change router settings.
- **Old and new internet addresses.** Connections work over both the older
  (IPv4) and newer (IPv6) kinds of internet address. When a game offers both,
  NNBuild tries them at the same time and uses the one that works, preferring
  the newer kind.
- **Automatic downloads.** If a game uses a custom map or mod you don't have,
  it is sent to you when you join. Files you already have are recognized and
  are not downloaded again. You still need your own copy of the base game.
- **A proper joining screen.** While you join, you see what is happening
  (connecting, downloading "file 2 of 3", receiving the game) with a progress
  bar and a Cancel option. If the server isn't up yet, the game keeps trying
  and tells you so.

### While you play

- **No more frozen matches.** If one player's connection stalls, everyone sees
  a countdown. The other players can press F9 to vote that player out. When the
  countdown ends or enough of the others agree, the match carries on without
  them. Players who vanish completely are removed automatically after a few
  seconds.
- **Automatic recovery.** If your game falls out of step with the host's, it
  tells you, reconnects by itself and gives you your score back. Before, the
  match simply broke.
- **Instant-feeling shots (optional).** A "zero ping" option shows your own
  gunfire the moment you press fire, instead of waiting for the server to
  answer.
- **No-wait mode (optional).** A host can choose a mode where the match never
  waits for a slow player. Only the slow player feels the delay.
- **Mixed computers.** Windows, Linux and Mac players can share a match.
  Matches between different kinds of computer used to fall apart within
  seconds; that is fixed.

---

## Hosting a server

- **Dedicated server mode.** Run a server that only hosts. It has no window,
  graphics or sound and runs in a text terminal, which suits a rented or remote
  machine. The server does not show up in the match or on the scoreboard as a
  player.
- **Always-open server.** A server can start right away, even when empty, and
  let players come and go whenever they like.
- **Kick and ban.** Remove a troublemaker, or ban them so they can't come back.
  The ban list is kept between sessions.
- **Map rotation.** Give the server a list of maps and it moves on to the next
  one when a match ends, in order or at random. The list can mix built-in
  levels, custom maps and mods. The host can also skip to the next map at any
  time.
- **Switch maps and mods without shutting down.** The host can change to a
  different map or mod pack during a session. Players are carried along
  automatically and download whatever they are missing. If a new mod pack turns
  out to be broken, the current match keeps going.
- **Change the match on the fly.** Add an enemy or item next to a player, or
  clear every monster from the level. It happens live, for everyone at once.
- **Set-and-forget startup.** List the commands a server should run when it
  starts, either directly or in a text file.
- **Server scripts.** Small scripts (JavaScript or TypeScript) can make timed
  announcements, change maps on a schedule or add chat commands. Players type
  something like `!rules` in the chat and get an answer.
- **Remote control for AI assistants.** A server or a running game can be
  supervised by an AI assistant through the open MCP standard: checking who is
  online, changing maps, removing players.

---

## Several games in one program

- **One download, many games.** The same program runs Duke Nukem 3D (1.3D,
  Atomic Edition and World Tour), Ion Fury, NAM, WWII GI, Shadow Warrior,
  Blood, Redneck Rampage and Exhumed/PowerSlave. You supply each game's own
  data files.
- **It works out which game you mean.** Point NNBuild at a data file or folder
  and it recognizes the game. If several are installed, it asks, or you can
  name one when you start it.
- **Online features travel with it.** The server list and automatic file
  sending are available in the other games too, to different degrees. Each
  game's page in the documentation says exactly what works.
- **World Tour commentary.** The developer commentary in Duke Nukem 3D: World
  Tour can be switched on and played back.
- **Mix and match (experimental).** The Shadow Warrior game can load and play
  Duke Nukem 3D levels, with Duke's enemies and scenery behaving as they do in
  Duke.

The games other than Duke Nukem 3D are newer additions and have had less
testing.

---

## Play as the enemy

- **Enemy skins.** In multiplayer you can choose to look like one of Duke's
  enemies: Assault Trooper, Assault Captain, Octabrain, Assault Commander,
  Enforcer, Pig Cop or Alien Beast. Other players see and hear you as that
  creature. It walks, attacks, gets hurt and dies with that enemy's own
  animations and voice, and the body left behind matches too. This is purely
  cosmetic and does not change how you play. (Alien Beast needs the Atomic
  Edition.)

---

## Built-in map editor

Open **Map Editor** from the title menu.

- **See what you build.** Switch between a floor-plan view and a textured 3D
  view you can fly through.
- **Draw rooms by clicking corners.** Rooms that share an edge connect
  automatically, and you can extend a new room from any wall.
- **Browse and place.** A panel shows the game's textures and common objects.
  Click to apply a texture or to place an enemy, weapon or item.
- **Edit anything, undo anything.** Select a room, wall or object to change its
  height, texture and settings. Every change can be undone and redone.
- **Ready-made building blocks.** Add pillars, staircases and walkable catwalks
  in a few steps. A side-on view lets you check heights and clearances.
- **Working mechanisms in a few clicks.** Presets set up lifts, doors,
  switches, crusher traps and light switches, and the editor can show what is
  linked to what.
- **Change many things at once.** Select a group of objects and move, shade or
  align them together.
- **Map check.** The editor finds common mistakes, such as a switch that leads
  nowhere, a cramped starting point or a missing texture, and takes you to
  them.
- **Test instantly.** Press F5 to play your map, and press it again to return
  to editing exactly where you left off. "Play From Here" starts you at any
  spot you pick.
- **Classic tools included.** The original-style Build editor is included for
  advanced work, and changes made there flow back into the modern editor.
- **Several games supported.** The editor saves the classic map format and can
  target Duke Nukem 3D 1.3D, Atomic Edition, NAM, NAPALM or WWII GI.

### Building together

- **Live editing.** Host a session and up to 32 people can edit the same map at
  the same time. You see each other's cursors and selections, a shared history
  shows who changed what, and undo works across everyone's changes.
- **AI co-builders.** An AI assistant can join a session as a participant with
  its own cursor and label. It can build rooms and place objects, and its
  changes appear in the shared history and can be undone like anyone else's.

---

## Make your own mods and games

- **Start a mod with one command.** NNBuild can create a ready-to-edit mod
  project in JavaScript or TypeScript, with type hints and a VS Code setup
  included.
- **Script the game.** Mods can react to what happens in the game, show text on
  screen, spawn things, play sounds, change menus and add chat commands.
- **Debug like any other program.** Set breakpoints in VS Code, step through
  your script while the game is paused, and inspect variables. A DevTools
  window in the game shows the script log and lets you try expressions.
- **Files and the network.** Scripts can read and write files, send and receive
  network messages, and fetch data from web addresses.
- **Projects.** Keep the maps, scripts, textures and sounds of a mod together
  in one folder, managed from inside the editor.
- **One-file sharing.** Export a project as a single ZIP file. Players drag the
  ZIP onto the program to play it.
- **Stand-alone games.** A project can bring all of its own artwork, sound and
  rules, so it runs on its own without the original game's files.
- **Scripted map objects.** In the editor's Modern Mode, attach a small script
  to a door, an item or a room to give it behaviour the classic game could not
  do. Projects that use this need NNBuild to run.
- **Prefabs.** Save a group of objects as a reusable piece and stamp copies of
  it wherever you need them.
- **Safety switch.** Scripts inside a downloaded project ZIP do not run unless
  the player allows them.
- **Example game included.** Ashline Relay is a small stand-alone game made as
  an NNBuild project, using free Freedoom artwork. It shows what a project can
  be and can be opened in the editor.

---

## Smoother everyday experience

- **Faster level loading.** In our tests, a level that took around ten seconds
  to load now takes about two.
- **No more creeping input delay.** A single hiccup could leave the controls
  feeling up to a second behind for the rest of the match. That is fixed.
- **Faster joining.** Sending the running game and mod files to a new player is
  many times quicker than before, so large add-ons no longer time out.
- **Six ready-made builds.** Windows (64-bit and 32-bit), Linux (x64 and ARM64)
  and macOS (Apple silicon and Intel).
- **Clearer launch options.** Options have readable long names such as
  `--game`, `--level` and `--dedicated`, next to the short classic ones.
- **Tidier scoreboard.** Only real players are listed, numbered in order.
- **New name and look.** The program is now called NNBuild and has new icons.

---

## Good to know

- NNBuild is entirely vibe-coded: everything it adds on top of NetDuke32 was
  written by AI coding assistants. It is provided as is, with no warranty and no
  liability of any kind. You use it at your own risk, and you are responsible
  for how you use it.
- Joining a match that is already running needs the same kind of build as the
  host: a 64-bit player cannot join a 32-bit host mid-game, or the other way
  round. Use the 64-bit build unless your computer cannot run it.
- NNBuild does not include any game's data. You need your own copy of each
  game you want to play.

---

## Want the details?

The documentation is at <https://nnb.nukemnet.com>:

- [Getting started](https://nnb.nukemnet.com/getting-started/installing.html)
- [Supported games](https://nnb.nukemnet.com/games/overview.html)
- [Playing online](https://nnb.nukemnet.com/multiplayer/playing-online.html)
  and [running a server](https://nnb.nukemnet.com/multiplayer/dedicated-server.html)
- [Map editor](https://nnb.nukemnet.com/editor/overview.html)
- [Modding](https://nnb.nukemnet.com/modding/overview.html)
- [Every launch option](https://nnb.nukemnet.com/reference/command-line.html)
