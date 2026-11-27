#!/bin/sh
# Build NNBuild for 32-bit ARM Linux (ARMv7, hard float) on an x86-64 machine.
#
# Run from the top of the source tree, as root, inside a fresh Ubuntu 22.04
# container, so the result needs the same system libraries as the other Linux
# builds (glibc 2.35):
#
#   docker run --rm -v "$PWD":/src -w /src ubuntu:22.04 sh platform/build-linux-armv7l.sh
#
# It installs a cross compiler and the ARM libraries, builds nnbuild and
# nnbuild-lobby, and starts each once under emulation as a check.
set -eu

if ! command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
    export DEBIAN_FRONTEND=noninteractive
    dpkg --add-architecture armhf
    # The standard package servers hold only x86 packages; ARM ones are on "ports".
    sed -i 's/^deb http/deb [arch=amd64,i386] http/' /etc/apt/sources.list
    for suite in jammy jammy-updates jammy-security; do
        echo "deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports $suite main universe"
    done > /etc/apt/sources.list.d/armhf.list
    apt-get update
    apt-get install -y --no-install-recommends \
        make git ca-certificates pkg-config g++-arm-linux-gnueabihf qemu-user \
        libsdl2-dev:armhf libflac-dev:armhf libogg-dev:armhf libvpx-dev:armhf \
        libcurl4-openssl-dev:armhf libgl1-mesa-dev:armhf libglu1-mesa-dev:armhf libasound2-dev:armhf
fi

# The tree belongs to the user outside the container; git refuses it otherwise,
# and the build asks git for the revision number.
git config --global --add safe.directory "$PWD"

# FLAC and VPX are linked statically, as in the other Linux builds.
# pkg-config has to answer for the ARM libraries, not the build machine's.
make -j"$(nproc)" nnbuild nnbuild-lobby PRETTY_OUTPUT=0 \
    CROSS=arm-linux-gnueabihf- ARCH=armv7l PKG_CONFIG=arm-linux-gnueabihf-pkg-config \
    FLAC_LIBS="-l:libFLAC.a -l:libogg.a" VPX_LIBS="-l:libvpx.a" "$@"

file nnbuild nnbuild-lobby 2>/dev/null || true
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy qemu-arm ./nnbuild --help
qemu-arm ./nnbuild-lobby --help
