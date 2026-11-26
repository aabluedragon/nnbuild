#!/bin/sh
# Build the lobby program and put it on the public lobby's machine.
#
#   sh docs/deploy/deploy-lobby.sh
#
# NNB_SSH is the command that runs its arguments on that machine and passes
# standard input through. It defaults to "ssh nnb.ms.nukemnet.com".
# NNB_MAKE_ARGS is added to the make command, for example "HAVE_FLAC=0" on a
# machine without the FLAC development files.
#
# The machine runs the lobby with pm2, as the user "nnbuild", from
# /opt/nnbuild-lobby. The first time, set that up by hand:
#
#   useradd --system --no-create-home --home-dir /opt/nnbuild-lobby --shell /usr/sbin/nologin nnbuild
#   mkdir -p /opt/nnbuild-lobby          (then run this script once to upload)
#   pm2 start /opt/nnbuild-lobby/nnbuild-lobby --name NNBuildLobby --cwd /opt/nnbuild-lobby \
#       --interpreter none --uid nnbuild --gid nnbuild
#   pm2 save
#
# The lobby listens on UDP port 23519, IPv4 and IPv6. The host name's web
# address is nginx-nnb.ms.nukemnet.com.conf, next to this file.
set -eu

ssh_cmd="${NNB_SSH:-ssh nnb.ms.nukemnet.com}"
dir="${NNB_LOBBY_DIR:-/opt/nnbuild-lobby}"
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(dirname "$(dirname "$here")")"

# shellcheck disable=SC2086  # NNB_MAKE_ARGS is a list of words
make -C "$repo" nnbuild-lobby ${NNB_MAKE_ARGS:-}

# Upload next to the running copy, swap, then restart the service if it exists.
$ssh_cmd "set -e; cat > '$dir/nnbuild-lobby.new'; chmod 755 '$dir/nnbuild-lobby.new'; \
    mv '$dir/nnbuild-lobby.new' '$dir/nnbuild-lobby'; \
    if pm2 describe NNBuildLobby >/dev/null 2>&1; then pm2 restart NNBuildLobby >/dev/null; echo restarted NNBuildLobby; fi; \
    echo installed '$dir/nnbuild-lobby'" < "$repo/nnbuild-lobby"
