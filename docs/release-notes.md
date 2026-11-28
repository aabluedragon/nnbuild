NNBuild is one program for playing, hosting and building for Duke Nukem 3D and
other Build-engine games.

**Documentation:** https://nnb.nukemnet.com

NNBuild's releases are numbered from 1.0.0. The earlier 1.3.1 and 1.3.2 came
before 1.0.0, and every later release contains everything they had.

## New in this release

Two fixes for bugs that players reported.

- **USER MAP in the menu works again.** In Duke Nukem 3D, choosing USER MAP
  after NEW GAME did nothing: the menu stayed on the episode list. It now
  opens the list of map files. The same entry in a running match's game setup
  (Escape, NEW GAME, USER MAP) sent the host to the single-player episode
  list; that is fixed too, and the chosen map is sent to the other players as
  before.
- **A match with 16 slots can be joined.** Nobody could join a host that was
  started with 16 slots: the joining player only saw "Waiting for the server -
  retrying". NukemNet starts a dedicated server room for 15 players this way
  (15 players and the server). The fault was on the joining side, so each
  player needs this release, not only the host.
- A slot count above 16 is now treated as 16.

## Downloads

| System | File | Needs |
| --- | --- | --- |
| Windows, 64-bit | `nnbuild-windows-x64.zip` | Windows 10 or later |
| Windows, 32-bit | `nnbuild-windows-x86.zip` | Windows XP or later |
| Linux, x64 | `nnbuild-linux-x64.tar.gz` | glibc 2.35 or later, SDL2 |
| Linux, ARM64 | `nnbuild-linux-arm64.tar.gz` | glibc 2.35 or later, SDL2 |
| Linux, ARMv7 (32-bit ARM) | `nnbuild-linux-armv7l.tar.gz` | glibc 2.35 or later, SDL2 |
| macOS, Apple silicon | `nnbuild-macos-arm64.zip` | macOS 15 or later |
| macOS, Intel | `nnbuild-macos-x64.zip` | macOS 14 or later |

The downloads contain the program only. You also need the data files of each
game you want to play, from your own copy of that game. See
[Installing](https://nnb.nukemnet.com/getting-started/installing.html).

## What is in it

- **Online play:** join a match in progress, rejoin with your score, a server
  list, automatic map and mod downloads, lag voting and automatic recovery.
- **Server hosting:** dedicated server, kick and ban, map rotation, scripts.
- **Several games in one program:** Duke Nukem 3D, Ion Fury, NAM, WWII GI,
  Shadow Warrior, Blood, Redneck Rampage and Exhumed/PowerSlave.
- **Built-in map editor** with live shared editing and AI assistant support.
- **JavaScript and TypeScript mods** with a project generator and VS Code
  debugging.

The full list is on the [Features](https://nnb.nukemnet.com/features.html) page.

## Good to know

- **Disclaimer:** NNBuild is entirely vibe-coded: everything it adds on top of NetDuke32 was
  written by AI coding assistants. It is provided as is, with no warranty and no
  liability of any kind. You use it at your own risk, and you are responsible
  for how you use it.
- The program file is named `nnbuild` (`nnbuild.exe` on Windows).
- Joining a match that is already running needs the same kind of build as the
  host: a 64-bit player cannot join a 32-bit host mid-game, or the other way
  round. Use the 64-bit build unless your computer cannot run it.
- The builds are not code-signed. Windows and macOS will ask for confirmation
  the first time you run them; the [Downloads](https://nnb.nukemnet.com/downloads.html)
  page explains how to proceed.
- The games other than Duke Nukem 3D are newer additions and have had less
  testing. Each game's page lists what works.
