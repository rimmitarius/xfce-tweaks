#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
install_deps=1
launch_preview=1

usage() {
  cat <<'EOF'
Build XFCE Tweaks in a private user prefix and optionally open the isolated preview.

Usage: scripts/install-preview.sh [--skip-dependencies] [--no-preview]

Automatic dependency installation is currently available on Fedora (dnf).
On other distributions, install the prerequisites in docs/INSTALLATION.md,
then run this script with --skip-dependencies.
EOF
}

while (($#)); do
  case "$1" in
    --skip-dependencies) install_deps=0 ;;
    --no-preview) launch_preview=0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

if ((install_deps)); then
  distro_id=
  if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    distro_id=${ID:-}
  fi
  if [[ $distro_id != fedora ]]; then
    cat >&2 <<EOF
Automatic dependency installation is only configured for Fedora.
Install the packages listed in docs/INSTALLATION.md with your distribution's
package manager, then retry with: $0 --skip-dependencies
EOF
    exit 1
  fi
  printf '%s\n' 'This will install Fedora build and isolated-preview dependencies using dnf.'
  read -r -p 'Continue? [y/N] ' answer
  case "$answer" in y|Y|yes|YES) ;; *) echo 'Cancelled.'; exit 1 ;; esac
  "$project_dir/scripts/install-dependencies.sh"
  "$project_dir/scripts/install-test-dependencies.sh"
fi

"$project_dir/scripts/build.sh"
if ((launch_preview)); then
  exec "$project_dir/scripts/preview.py"
fi
