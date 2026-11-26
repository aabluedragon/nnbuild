# Downloads

Ready-made builds are published on GitHub Releases. Each link below always
points at the newest release.

| System | Download |
| --- | --- |
| Windows, 64-bit | [nnbuild-windows-x64.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-windows-x64.zip) |
| Windows, 32-bit | [nnbuild-windows-x86.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-windows-x86.zip) |
| Linux, x64 | [nnbuild-linux-x64.tar.gz](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-linux-x64.tar.gz) |
| Linux, ARM64 | [nnbuild-linux-arm64.tar.gz](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-linux-arm64.tar.gz) |
| macOS, Apple silicon (M1 and later) | [nnbuild-macos-arm64.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-macos-arm64.zip) |
| macOS, Intel | [nnbuild-macos-x64.zip](https://github.com/aabluedragon/nnbuild/releases/latest/download/nnbuild-macos-x64.zip) |

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

### Linux

Unpack the archive and run `./vibebuild32` from a terminal. The program uses
the system's SDL2 library. On Debian or Ubuntu, install it with:

```sh
sudo apt install libsdl2-2.0-0
```

### macOS

Unpack the ZIP and move the app to your Applications folder. The app is not
notarized by Apple, so the first time you open it, right-click the app and
choose **Open**, then confirm.

## Source code

The source is on [GitHub](https://github.com/aabluedragon/nnbuild). To compile
it yourself, see [Building from source](building/from-source.md).
