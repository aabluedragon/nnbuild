#!/bin/sh
# Assemble a release archive from an already built tree.
#
# Usage: platform/package.sh <target>
#   target: linux-x64 | linux-arm64 | windows-x64 | windows-x86 | macos-arm64 | macos-x64
#
# Writes dist/nnbuild-<target>.tar.gz (Linux) or dist/nnbuild-<target>.zip.
set -eu

target="${1:?usage: platform/package.sh <target>}"
name="nnbuild-$target"
stage="dist/$name"

mkdir -p dist
if [ -e "$stage" ]; then
    echo "error: $stage already exists; remove dist/ first" >&2
    exit 1
fi
mkdir -p "$stage"

cp package/common/gpl-2.0.txt package/common/buildlic.txt "$stage/"
cat > "$stage/README.txt" <<'EOF'
NNBuild
=======

One program for playing, hosting and building for Duke Nukem 3D and other
Build-engine games: modern online play, a built-in map editor, and mods
written in JavaScript or TypeScript.

This download contains the program only. You also need the data files of
each game you want to play, from your own copy of that game.

Getting started, every launch option, the map editor and modding are all
covered in the documentation:

    https://nnb.nukemnet.com

Source code and new releases:

    https://github.com/aabluedragon/nnbuild

The program file is named "vibebuild32" (the project's earlier name).

NNBuild is free software, based on NetDuke32 and EDuke32. See gpl-2.0.txt and
buildlic.txt for the licence terms.
EOF

case "$target" in
linux-*)
    cp vibebuild32 eduke32.dat "$stage/"
    tar -C dist -czf "dist/$name.tar.gz" "$name"
    ls -l "dist/$name.tar.gz"
    ;;
windows-*)
    cp vibebuild32.exe eduke32.dat "$stage/"
    # Windows text files: CRLF line endings for Notepad on older systems.
    for f in "$stage"/*.txt; do sed -i 's/\r$//; s/$/\r/' "$f"; done
    (cd dist && zip -q -r -9 "$name.zip" "$name")
    ls -l "dist/$name.zip"
    ;;
macos-*)
    app="$stage/NNBuild.app"
    cp -R VibeBuild32.app "$app"
    cp eduke32.dat "$app/Contents/Resources/"
    # Carry the non-system libraries inside the app and re-sign it (ad hoc), so
    # it runs on a Mac that does not have Homebrew.
    dylibbundler -od -b -x "$app/Contents/MacOS/vibebuild32" \
        -d "$app/Contents/Frameworks/" -p @executable_path/../Frameworks/
    codesign --force --deep --sign - "$app"
    ditto -c -k --sequesterRsrc --keepParent "$stage" "dist/$name.zip"
    ls -l "dist/$name.zip"
    ;;
*)
    echo "error: unknown target '$target'" >&2
    exit 1
    ;;
esac
