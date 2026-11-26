# Building from source

NNBuild is built with GNU Make. The target is named `nnbuild` and produces a
program file of the same name.

```sh
git clone https://github.com/aabluedragon/nnbuild.git
cd nnbuild
make -j8 nnbuild
```

A plain `make nnbuild` builds every game runtime into the one program. To
leave one out, pass `WITH_SW=0`, `WITH_BLOOD=0`, `WITH_RR=0` or
`WITH_EXHUMED=0`.

The release builds are produced by the workflow in
`.github/workflows/build.yml`, which is the reference for the exact commands
on each platform.

## Linux

Install the compiler and libraries. On Debian or Ubuntu:

```sh
sudo apt install build-essential nasm pkg-config \
    libsdl2-dev libflac-dev libogg-dev libvpx-dev libcurl4-openssl-dev \
    libgl1-mesa-dev libglu1-mesa-dev libasound2-dev
```

Then build:

```sh
make -j"$(nproc)" nnbuild
```

Useful options:

| Option | Effect |
| --- | --- |
| `HAVE_FLAC=0` | Build without FLAC audio support; the FLAC library is then not needed. |
| `USE_LIBVPX=0` | Build without VP8 video support; the VPX library is then not needed. |
| `FLAC_LIBS="-l:libFLAC.a -l:libogg.a"` | Link FLAC statically, so the program runs on distributions that ship a different FLAC version. |
| `VPX_LIBS="-l:libvpx.a"` | The same for VPX. |
| `RELEASE=0` | Debug build. |

The curl library is only needed for building. At run time it is loaded when a
script first calls `fetch`; without it, `fetch` reports an error and everything
else works.

## Windows

The Windows builds are cross-compiled on Linux with MinGW-w64. The libraries
they need are already in the repository under `platform/Windows`.

On Ubuntu, for the 64-bit build:

```sh
sudo apt install make nasm g++-mingw-w64-x86-64-posix
sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
make -j"$(nproc)" PLATFORM=WINDOWS CROSS=x86_64-w64-mingw32- nnbuild
```

For the 32-bit build, use `g++-mingw-w64-i686-posix` and
`CROSS=i686-w64-mingw32-`. Give each build its own object directory if you
build both from one checkout, for example `obj=obj-win32`.

The POSIX-threads flavour of MinGW (the `-posix` packages) is required.

## macOS

Install the Xcode command line tools and the libraries with Homebrew:

```sh
xcode-select --install
brew install sdl2 flac libogg libvpx
make -j"$(sysctl -n hw.ncpu)" nnbuild
```

The build also creates an application bundle next to the program file.

## Packaging

`platform/package.sh` assembles a release archive from a finished build:

```sh
sh platform/package.sh linux-x64
```

Targets are `linux-x64`, `linux-arm64`, `windows-x64`, `windows-x86`,
`macos-arm64` and `macos-x64`. The archive is written to `dist/`. On macOS the
script needs `dylibbundler` (`brew install dylibbundler`) to carry the
libraries inside the application bundle.

## The lobby program

`nnbuild-lobby` is the [lobby server](../multiplayer/lobby-server.md) as a
small program of its own. It needs only the standard C and C++ libraries:

```sh
make nnbuild-lobby
```

## The documentation site

This site is in the `docs/` directory and is built with
[mdBook](https://rust-lang.github.io/mdBook/):

```sh
mdbook serve docs    # preview at http://localhost:3000
mdbook build docs    # write the site to docs/book
```

## How several games fit in one program

Each extra game (Shadow Warrior, Blood, Redneck Rampage, Exhumed) is compiled
on its own and then prelinked into a single object file. Any symbol that an
earlier game also defines is made private to that object, so the games do not
interfere with each other. A small launcher picks the game at start-up and
routes the engine's callbacks to it.

The prelink step differs by platform: macOS uses its linker's
`-unexported_symbols_list`; Linux uses a relocatable link followed by
`objcopy --localize-symbols`; Windows does the same and additionally renames
some sections (see `platform/pe-private-sections.sh`). Because this step needs
real machine code, link-time optimisation is switched off for this target
everywhere except macOS.
