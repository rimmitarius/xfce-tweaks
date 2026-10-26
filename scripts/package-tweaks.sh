#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$project_dir/packages"
for tweak in desktop-groups video-wallpaper victus-control desktop-peek clock-widget weather-widget \
             cpu-monitor gpu-monitor memory-monitor storage-monitor external-dimming; do
    library="$project_dir/modules/$tweak/build/lib$tweak.so"
    [[ -f "$library" ]] || library="$project_dir/build-modules/lib$tweak.so"
    python3 "$project_dir/scripts/tweak-tool.py" package "$project_dir/modules/$tweak" \
      --library "$library" \
      --output "$project_dir/packages/$tweak-0.1.0.xfce-tweak"
done
