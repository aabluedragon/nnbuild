The first public NNBuild release. NNBuild is one program for playing, hosting
and building for Duke Nukem 3D and other Build-engine games.

**Documentation:** https://nnb.nukemnet.com

## Downloads

| System | File | Needs |
| --- | --- | --- |
| Windows, 64-bit | `nnbuild-windows-x64.zip` | Windows 10 or later |
| Windows, 32-bit | `nnbuild-windows-x86.zip` | Windows 10 or later |
| Linux, x64 | `nnbuild-linux-x64.tar.gz` | glibc 2.35 or later, SDL2 |
| Linux, ARM64 | `nnbuild-linux-arm64.tar.gz` | glibc 2.35 or later, SDL2 |
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

- The program file is named `vibebuild32`, the project's earlier name.
- Joining a match that is already running needs the same kind of build as the
  host: a 64-bit player cannot join a 32-bit host mid-game, or the other way
  round. Use the 64-bit build unless your computer cannot run it.
- The builds are not code-signed. Windows and macOS will ask for confirmation
  the first time you run them; the [Downloads](https://nnb.nukemnet.com/downloads.html)
  page explains how to proceed.
- The games other than Duke Nukem 3D are newer additions and have had less
  testing. Each game's page lists what works.
