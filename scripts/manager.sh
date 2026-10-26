#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix=${XFCE_TWEAKS_PREFIX:-"$HOME/.local/lib/xfce-tweaks-modular"}
exec python3 "$project_dir/manager/app.py" --modules "$prefix/lib/xfce-tweaks/modules" "$@"
