#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ! -d $project_dir/tests/example-tweak ]]; then
    python3 "$project_dir/scripts/tweak-tool.py" new example-tweak "$project_dir/tests/example-tweak"
fi
python3 "$project_dir/scripts/tweak-tool.py" build "$project_dir/tests/example-tweak"
python3 "$project_dir/scripts/tweak-tool.py" package "$project_dir/tests/example-tweak"
python3 "$project_dir/tests/test_registry.py"
python3 "$project_dir/tests/test-manager-presence.py"
meson test -C "$project_dir/build-modules" --print-errorlogs
"$project_dir/scripts/test-x11.sh" "$project_dir/build-host/src/xfdesktop"
python3 "$project_dir/tests/test-preview.py"
