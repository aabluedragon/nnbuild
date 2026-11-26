#!/bin/sh
# Build the documentation site and publish it to the web server.
#
#   sh docs/deploy/deploy.sh
#
# NNB_SSH is the command that runs its arguments on the web server and passes
# standard input through. It defaults to "ssh nnb.nukemnet.com".
# NNB_ROOT is the directory nginx serves (see nginx-nnb.nukemnet.com.conf).
set -eu

ssh_cmd="${NNB_SSH:-ssh nnb.nukemnet.com}"
root="${NNB_ROOT:-/var/www/nnb.nukemnet.com}"
here="$(cd "$(dirname "$0")" && pwd)"
docs="$(dirname "$here")"

mdbook build "$docs"

# Upload next to the live copy, then swap, so visitors never see a half-written site.
tar -C "$docs/book" -czf - . | $ssh_cmd "set -e; rm -rf '$root.new'; mkdir -p '$root.new'; tar --no-same-owner -xzf - -C '$root.new'; \
    if [ -d '$root' ]; then rm -rf '$root.old'; mv '$root' '$root.old'; fi; mv '$root.new' '$root'; rm -rf '$root.old'; \
    chmod -R a+rX '$root'; echo published to '$root'"
