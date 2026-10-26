#!/usr/bin/env bash
set -euo pipefail
if [[ -f /usr/lib/systemd/system/xfce-tweaks-victus-fans.service ]]; then
    sudo systemctl stop xfce-tweaks-victus-fans.service
fi
sudo python3 - <<'PYTHON'
from pathlib import Path
for name in ['/usr/share/xfce-tweaks/hp-hardware-profiles.json','/usr/libexec/xfce-tweaks-victus-helper','/usr/share/polkit-1/actions/org.xfce.tweaks.victus.policy','/usr/lib/systemd/system/xfce-tweaks-victus-fans.service','/run/xfce-tweaks/curve.json','/run/xfce-tweaks/fan-status.json','/run/xfce-tweaks/fan-request']:
    Path(name).unlink(missing_ok=True)
PYTHON

sudo systemctl daemon-reload
