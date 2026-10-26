#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
sandbox=$(mktemp -d -t video-monitors-test-XXXXXX)
trap 'rm -rf -- "$sandbox"' EXIT
mkdir -p "$sandbox/config" "$sandbox/cache" "$sandbox/data"
for color in red blue; do
 ffmpeg -hide_banner -loglevel error -f lavfi -i "color=c=$color:s=320x180:r=12" -t 4 -c:v libvpx -deadline realtime -threads 2 "$sandbox/$color.webm"
done
env HOME="$sandbox" XDG_CONFIG_HOME="$sandbox/config" XDG_CACHE_HOME="$sandbox/cache" XDG_DATA_HOME="$sandbox/data" NO_AT_BRIDGE=1 GIO_USE_VFS=local GST_PLUGIN_FEATURE_RANK='vp8dec:512,nvvp8dec:0' \
 xvfb-run -a -s '-screen 0 1280x720x24' dbus-run-session -- "$root/build-modules/test-video-monitors" "$sandbox/red.webm" "$sandbox/blue.webm"
