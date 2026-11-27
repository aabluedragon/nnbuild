# Downloads

Ready-made builds are published on GitHub Releases. Each link below always
points at the newest release.

> **Disclaimer.** NNBuild is entirely vibe-coded: everything it adds on top of NetDuke32 was
> written by AI coding assistants. It is provided as is, with no warranty and no
> liability of any kind. You use it at your own risk, and you are responsible
> for how you use it.

| System | Download | Needs |
| --- | --- | --- |
| Windows, 64-bit | [nnbuild-windows-x64.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-windows-x64.zip) | Windows 10 or later |
| Windows, 32-bit | [nnbuild-windows-x86.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-windows-x86.zip) | Windows XP or later ([details](#windows)) |
| Linux, x64 | [nnbuild-linux-x64.tar.gz](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-linux-x64.tar.gz) | A distribution from 2022 or later, with SDL2 |
| Linux, ARM64 | [nnbuild-linux-arm64.tar.gz](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-linux-arm64.tar.gz) | A distribution from 2022 or later, with SDL2 |
| Linux, ARMv7 (32-bit ARM) | [nnbuild-linux-armv7l.tar.gz](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-linux-armv7l.tar.gz) | A 32-bit distribution from 2022 or later, with SDL2 |
| macOS, Apple silicon (M1 and later) | [nnbuild-macos-arm64.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-macos-arm64.zip) | macOS 15 or later |
| macOS, Intel | [nnbuild-macos-x64.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-macos-x64.zip) | macOS 14 or later |

Pick a 64-bit build unless your computer cannot run it. A 32-bit build and a
64-bit build cannot [join each other's matches in
progress](multiplayer/playing-online.md#32-bit-and-64-bit-builds). The 32-bit
builds are the 32-bit Windows one and the ARMv7 Linux one.

All releases, with their notes, are listed on the
[releases page](https://github.com/aabluedragon/nnbuild/releases).

The downloads contain the program only. You also need the data files of each
game you want to play, from your own copy of that game. See
[Installing](getting-started/installing.md).

## After downloading

### Windows

Unpack the ZIP into a folder of its own and run `nnbuild.exe`. Windows may
show a "Windows protected your PC" notice because the program is not signed;
choose **More info**, then **Run anyway**.

The 64-bit build is made for Windows 10 and later.

The 32-bit build is made to start on Windows XP and later as well. It uses
only what Windows XP provides, and stands in itself for the few newer Windows
functions the program calls. It has not yet been tried on a real Windows XP,
Vista, 7 or 8 computer, so expect rough edges there.

### Linux

Unpack the archive and run `./nnbuild` from a terminal. The program uses
the system's SDL2 library. On Debian or Ubuntu, install it with:

```sh
sudo apt install libsdl2-2.0-0
```

The builds are made on Ubuntu 22.04, so they need a distribution at least that
new (glibc 2.35), for example Ubuntu 22.04, Debian 12 or Fedora 36. On an older
system, [build from source](building/from-source.md).

The ARMv7 build is for ARM computers with a 32-bit system and hardware
floating point ("armhf"), for example 32-bit Raspberry Pi OS 12. It needs the
32-bit SDL2 library. It has been checked under emulation, not on an ARM
computer.

Scripts that use `fetch` also need the system's libcurl, which most
distributions install by default.

### macOS

Unpack the ZIP and move **NNBuild** to your Applications folder. The app is not
notarized by Apple, so macOS refuses to open it the first time:

1. Open the app once. macOS says it cannot verify the app. Choose **Done**.
2. Open **System Settings**, then **Privacy & Security**.
3. Scroll down to the message about NNBuild and choose **Open Anyway**.
4. Confirm with your password or Touch ID.

You only need to do this once. If you prefer the terminal, this does the same:

```sh
xattr -dr com.apple.quarantine /Applications/NNBuild.app
```

The Apple silicon build needs macOS 15 or later, and the Intel build needs
macOS 14 or later. On an older macOS, [build from
source](building/from-source.md).

## Source code

The source is on [GitHub](https://github.com/aabluedragon/nnbuild). To compile
it yourself, see [Building from source](building/from-source.md).
