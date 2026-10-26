#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
for command in Xvfb xdotool dbus-run-session dbus-update-activation-environment xfconf-query python3 xfwm4 xprop; do
    command -v "$command" >/dev/null || { echo "Missing test dependency: $command" >&2; exit 1; }
done
sandbox=$(mktemp -d -t desktop-groups-x11-XXXXXX)
cleanup() {
    # Portal services exit asynchronously after their private bus goes away.
    for attempt in 1 2 3; do
        if rm -rf -- "$sandbox" 2>/dev/null; then return; fi
        sleep 0.2
    done
    echo "Test sandbox retained at $sandbox" >&2
}
trap cleanup EXIT
mkdir -p "$sandbox/home/Desktop" "$sandbox/config" "$sandbox/cache" "$sandbox/data" "$sandbox/runtime"
chmod 700 "$sandbox/runtime"
if [[ -z ${XFCE_TWEAKS_MODULE_PATH:-} ]]; then
    mkdir -p "$sandbox/modules"
    for identifier in desktop-groups video-wallpaper victus-control desktop-peek; do
        library="$root/build-modules/lib$identifier.so"
        if [[ -f $library ]]; then
            mkdir -p "$sandbox/modules/$identifier"
            cp "$root/modules/$identifier/tweak.ini" "$library" "$sandbox/modules/$identifier/"
            if [[ -d $root/modules/$identifier/assets ]]; then
                cp -r "$root/modules/$identifier/assets" "$sandbox/modules/$identifier/"
            fi
        fi
    done
    export XFCE_TWEAKS_MODULE_PATH="$sandbox/modules:${XFCE_TWEAKS_PREFIX:-$HOME/.local/lib/xfce-tweaks-modular}/lib/xfce-tweaks/modules"
fi
export XDG_CONFIG_HOME="$sandbox/config" XDG_CACHE_HOME="$sandbox/cache"
export XDG_DATA_HOME="$sandbox/data" XDG_RUNTIME_DIR="$sandbox/runtime" NO_AT_BRIDGE=1 GIO_USE_VFS=local
export LC_ALL=C.UTF-8
export DESKTOP_GROUPS_TEST_SANDBOX="$sandbox"
unset DBUS_SESSION_BUS_ADDRESS SESSION_MANAGER WAYLAND_DISPLAY DISPLAY
printf 'XDG_DESKTOP_DIR="$HOME/Desktop"\n' > "$XDG_CONFIG_HOME/user-dirs.dirs"
env HOME="$sandbox/home" dbus-run-session -- python3 "$root/scripts/test-x11.py" "$root" "${1:-$root/build-host/src/xfdesktop}"
