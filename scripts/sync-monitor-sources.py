#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export common hardware monitor implementation to standalone Tweak sources."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
for identifier in ['cpu-monitor','gpu-monitor','memory-monitor','storage-monitor']:
    module=ROOT/'modules'/identifier
    for source in ['monitor-widget.h','monitor-content.h','widget-transfer.h']:
        target=module/source
        if target.is_symlink():target.unlink()
        target.write_bytes((ROOT/'modules/common'/source).read_bytes())
    asset=module/'assets/app.py'
    if asset.is_symlink():asset.unlink()
    asset.write_bytes((ROOT/'modules/common/monitor-settings.py').read_bytes())
