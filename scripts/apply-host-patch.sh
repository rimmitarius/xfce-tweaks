#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 /path/to/clean-xfce-tweaks-baseline" >&2
    echo "The target must be this project's pristine baseline checkout (commit c492a6a)." >&2
    exit 2
fi
target=$(cd -- "$1" && pwd)
expected=c492a6af7626ecfea64197aaca3fce7248f1dca0
actual=$(git -C "$target" rev-parse HEAD 2>/dev/null || true)
if [[ $actual != "$expected" ]]; then
    echo "Refusing to patch: expected baseline $expected, found ${actual:-no Git checkout}." >&2
    exit 1
fi
if [[ -n $(git -C "$target" status --porcelain) ]]; then
    echo "Refusing to patch a modified checkout; start from a clean baseline." >&2
    exit 1
fi
patch_file="$project_dir/patches/0001-native-extension-host.patch"
git -C "$target" apply --check "$patch_file"
git -C "$target" apply "$patch_file"
echo "Applied the modular host and SDK patch to $target. Review it, then build using docs/INSTALLATION.md."
