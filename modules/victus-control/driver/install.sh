#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
stage=$(mktemp -d /tmp/xfce-hp-rgb.XXXXXXXX)
trap 'rm -rf -- "$stage"' EXIT
cp -- "$source_dir/Makefile" "$source_dir/xfce_hp_rgb.c" "$stage/"
make -C "/lib/modules/$(uname -r)/build" M="$stage" modules
if [[ -n ${HP_RGB_SIGN_KEY:-} && -n ${HP_RGB_SIGN_CERT:-} ]]; then
 sudo "/lib/modules/$(uname -r)/build/scripts/sign-file" sha256 "$HP_RGB_SIGN_KEY" "$HP_RGB_SIGN_CERT" "$stage/xfce_hp_rgb.ko"
elif mokutil --sb-state 2>/dev/null | rg -q 'SecureBoot enabled'; then
 echo 'Secure Boot requires HP_RGB_SIGN_KEY and HP_RGB_SIGN_CERT for an enrolled signing certificate.' >&2
 exit 1
fi
sudo install -D -m 0644 "$stage/xfce_hp_rgb.ko" "/lib/modules/$(uname -r)/extra/xfce_hp_rgb.ko"
sudo depmod -a
sudo modprobe xfce_hp_rgb
printf '%s\n' xfce_hp_rgb | sudo tee /etc/modules-load.d/xfce-hp-rgb.conf >/dev/null
