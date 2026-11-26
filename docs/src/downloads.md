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
| Windows, 32-bit | [nnbuild-windows-x86.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-windows-x86.zip) | Windows 10 or later |
| Linux, x64 | [nnbuild-linux-x64.tar.gz](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-linux-x64.tar.gz) | A distribution from 2022 or later, with SDL2 |
| Linux, ARM64 | [nnbuild-linux-arm64.tar.gz](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-linux-arm64.tar.gz) | A distribution from 2022 or later, with SDL2 |
| macOS, Apple silicon (M1 and later) | [nnbuild-macos-arm64.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-macos-arm64.zip) | macOS 15 or later |
| macOS, Intel | [nnbuild-macos-x64.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-macos-x64.zip) | macOS 14 or later |

Pick the 64-bit Windows build unless your computer cannot run it. The two
Windows builds cannot [join each other's matches in
progress](multiplayer/playing-online.md#32-bit-and-64-bit-builds).

All releases, with their notes, are listed on the
[releases page](https://github.com/aabluedragon/nnbuild/releases).

The downloads contain the program only. You also need the data files of each
game you want to play, from your own copy of that game. See
[Installing](getting-started/installing.md).

## After downloading

### Windows

Unpack the ZIP into a folder of its own and run `vibebuild32.exe`. Windows may
show a "Windows protected your PC" notice because the program is not signed;
choose **More info**, then **Run anyway**.

The Windows builds are made for Windows 10 and later. Older versions of
Windows have not been tried.

### Linux

Unpack the archive and run `./vibebuild32` from a terminal. The program uses
the system's SDL2 library. On Debian or Ubuntu, install it with:

```sh
sudo apt install libsdl2-2.0-0
```

The builds are made on Ubuntu 22.04, so they need a distribution at least that
new (glibc 2.35), for example Ubuntu 22.04, Debian 12 or Fedora 36. On an older
system, [build from source](building/from-source.md).

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
