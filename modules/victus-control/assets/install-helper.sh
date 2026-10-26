#!/usr/bin/env bash
set -euo pipefail
assets_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sudo install -d -o root -g root -m 0755 /usr/share/xfce-tweaks
sudo install -o root -g root -m 0644 "$assets_dir/profiles.json" /usr/share/xfce-tweaks/hp-hardware-profiles.json
sudo install -o root -g root -m 0755 "$assets_dir/hardware.py" /usr/libexec/xfce-tweaks-victus-helper
sudo install -o root -g root -m 0644 "$assets_dir/org.xfce.tweaks.victus.policy" /usr/share/polkit-1/actions/org.xfce.tweaks.victus.policy

sudo install -o root -g root -m 0644 "$assets_dir/xfce-tweaks-victus-fans.service" /usr/lib/systemd/system/xfce-tweaks-victus-fans.service
sudo systemctl daemon-reload
