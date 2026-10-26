#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix=${XFCE_TWEAKS_PREFIX:-"$HOME/.local/lib/xfce-tweaks-modular"}
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
mkdir -p "$prefix" "$project_dir/build-deps"
exec 9>"$prefix/.build-lock"
flock -n -x 9 || { echo "Close the modular preview before rebuilding/installing." >&2; exit 1; }
printf 'development-build\n' > "$prefix/.xfce-tweaks-development-prefix"
(cd "$project_dir/upstream" && sha256sum --check SHA256SUMS)
rpath="-Wl,-rpath,$prefix/lib"
setup() {
    local build_dir=$1 source_dir=$2
    shift 2
    if [[ -f $build_dir/build.ninja ]]; then
        meson setup --reconfigure --clearcache "$build_dir" "$source_dir" "$@"
    else meson setup "$build_dir" "$source_dir" "$@"; fi
}
if [[ ${1:-} != --modules-only ]]; then
    for component in libxfce4ui-4.21.0 libxfce4windowing-4.20.7; do
        if [[ $component == libxfce4ui* ]]; then pc_file=libxfce4ui-2.pc; else pc_file=libxfce4windowing-0.pc; fi
        if [[ ! -f $prefix/lib/pkgconfig/$pc_file ]]; then
            # Dependency sources are bundled, with no runtime dependency on the old project.
            if [[ $component == libxfce4ui* ]]; then
                archive="$project_dir/upstream/$component.tar.xz";options=(-Dglade=disabled)
            else archive="$project_dir/upstream/$component.tar.bz2";options=(-Dtests=false); fi
            if [[ ! -d $project_dir/build-deps/$component ]]; then tar -xf "$archive" -C "$project_dir/build-deps"; fi
            setup "$project_dir/build-deps/$component/build" "$project_dir/build-deps/$component" --prefix="$prefix" --libdir=lib \
              -Dwayland=disabled -Dx11=enabled -Dintrospection=false -Dvala=disabled "-Dc_link_args=$rpath" "${options[@]}"
            meson compile -C "$project_dir/build-deps/$component/build"
            meson install -C "$project_dir/build-deps/$component/build"
        fi
    done
    setup "$project_dir/build-host" "$project_dir/host" --prefix="$prefix" --libdir=lib \
      -Dx11=enabled -Dwayland=disabled -Dvideo-backdrop=false "-Dc_link_args=$rpath"
    meson compile -C "$project_dir/build-host"
    meson install -C "$project_dir/build-host"
fi
python3 "$project_dir/scripts/install-sdk.py" "$prefix"
setup "$project_dir/build-modules" "$project_dir/modules" --prefix="$prefix" --libdir=lib -Dc_link_args=[]
meson compile -C "$project_dir/build-modules"
meson test -C "$project_dir/build-modules" --print-errorlogs
meson install -C "$project_dir/build-modules"
mkdir -p "$prefix/share/xfce-tweaks/manager" "$prefix/bin"
cp "$project_dir/manager/app.py" "$project_dir/manager/registry.py" "$prefix/share/xfce-tweaks/manager/"
python3 - "$prefix" <<'PY'
from pathlib import Path
import shlex,sys
p=Path(sys.argv[1])
launcher=p/'bin/xfce-tweaks-manager'
launcher.write_text('#!/bin/sh\nexec python3 '+shlex.quote(str(p/'share/xfce-tweaks/manager/app.py'))+' --modules '+shlex.quote(str(p/'lib/xfce-tweaks/modules'))+' "$@"\n')
launcher.chmod(0o755)
PY
python3 "$project_dir/scripts/create-launchers.py"
printf '\nBuild complete: %s\nYour running desktop was not changed.\n' "$prefix"
