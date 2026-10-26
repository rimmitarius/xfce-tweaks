#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
sandbox=$(mktemp -d -t video-scale-test-XXXXXX)
trap 'rm -rf -- "$sandbox"' EXIT
mkdir -p "$sandbox/config" "$sandbox/cache" "$sandbox/data"
env HOME="$sandbox" XDG_CONFIG_HOME="$sandbox/config" XDG_CACHE_HOME="$sandbox/cache" XDG_DATA_HOME="$sandbox/data" NO_AT_BRIDGE=1 GIO_USE_VFS=local \
 xvfb-run -a -s '-screen 0 2560x1080x24' dbus-run-session -- "$root/build-modules/test-video-scaling"
