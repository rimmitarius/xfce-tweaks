# Build and install XFCE Tweaks

XFCE Tweaks builds a private, modular `xfdesktop` under your home directory. It does not overwrite the distribution's desktop manager or change the active session unless you separately use the optional activation helper.

## Platform support

The project is intended for Linux distributions with XFCE running on **X11**. Fedora 44 is the only distribution tested so far. Other distributions may work when their system libraries meet the minimum versions below, but compatibility has not been verified. The current host and widgets do not support Wayland.

The source baseline is upstream `xfdesktop` 4.21.0. The build compiles private copies of `libxfce4ui` 4.21.0 and `libxfce4windowing` 4.20.7 from bundled source archives. Other XFCE libraries and system dependencies come from the distribution. A distribution that ships older `xfconf` or `libxfce4util` may need newer packages or a source build of that dependency before this project will compile.

## Build requirements

Install a C toolchain, Meson, Ninja, pkg-config, gettext, Python 3, GTK 3 and the development packages that provide these pkg-config modules. Package names vary between distributions; use the module names to find the matching development packages in your package manager.

| pkg-config module / component | Minimum version or use |
| --- | --- |
| `glib-2.0`, `gio-unix-2.0` | 2.72 |
| `gtk+-3.0`, `gdk-x11-3.0` | 3.24 |
| `libxfce4util-1.0` | 4.18 |
| `libxfconf-0` | 4.19.3 |
| `libxfce4kbd-private-3` | 4.18 |
| `libxfce4windowing-0`, `libxfce4windowing-x11-0`, `libxfce4windowingui-0` | 4.20.6; the build supplies 4.20.7 privately |
| X11 development libraries | Xlib, Xext, XRandR, Xinerama, SM |
| `yaml-0.1` | 0.2.5; used for desktop file icons |
| `gstreamer-1.0` | 1.18; required by the video wallpaper module |
| GStreamer GTK sink plugin | runtime requirement for video playback |
| PyGObject with GTK 3 introspection | required by the Tweaks Manager |

The upstream host can use `garcon-1` and `garcon-gtk3-1` for the Applications menu, `thunarx-3` for Thunar integration, and `libnotify` for notifications when available. The default Meson feature settings make some of these optional. Desktop functionality also expects a working XFCE session with `xfconf`, `xfce4-session`, a window manager, and (for normal file operations) a file manager.

For Fedora, the repository includes convenience helpers:

```sh
./scripts/install-dependencies.sh
./scripts/install-test-dependencies.sh   # optional Xephyr/Xvfb preview and test tools
```

These helpers use `dnf` and `sudo`; do not use them on other distributions. On another system, install the equivalent packages with its package manager, checking the pkg-config modules and minimum versions above. The test helper's extra tools are Xephyr, Xvfb, xdotool, xfwm4, X11 utilities, PyGObject, and FFmpeg.

## Build and preview

After installing the system dependencies, run these commands as your normal user:

```sh
./scripts/build.sh
./scripts/preview.py
```

Fedora users can use the guided helper, which prompts before installing Fedora's build and preview packages and then builds and opens the isolated preview:

```sh
./scripts/install-preview.sh
```

On other distributions, install the prerequisites manually, then use `./scripts/install-preview.sh --skip-dependencies`. Pass `--no-preview` to build without launching Xephyr. The helper only automates dependency installation on Fedora; it does not imply that other distributions have been verified.

The build verifies the checksums of the bundled upstream archives, compiles the private host and dependencies, installs the SDK, Tweaks Manager and modules under `~/.local/lib/xfce-tweaks-modular/`, and creates local launchers. It does not replace `/usr/bin/xfdesktop`, edit autostart, or change the active desktop service. The build script invokes Meson's module checks as part of its normal build.

The preview launches the modular desktop in Xephyr with private Xfconf, D-Bus, and desktop files. Close the preview before rebuilding its prefix. To rebuild only existing modules:

```sh
./scripts/build.sh --modules-only
```

For a different private prefix, set `XFCE_TWEAKS_PREFIX` consistently for build, manager, developer tools, and preview. The build itself uses the bundled source archives; installing system dependencies through a package manager requires network access.

## Optional host patch workflow

The source tree in `host/` already contains the modular extension bridge. `patches/0001-native-extension-host.patch` is the reviewable delta against the pristine upstream baseline imported in commit `c492a6af7626ecfea64197aaca3fce7248f1dca0`. To reproduce it in a clean baseline checkout:

```sh
git worktree add --detach /tmp/xfce-tweaks-baseline c492a6af7626ecfea64197aaca3fce7248f1dca0
./scripts/apply-host-patch.sh /tmp/xfce-tweaks-baseline
```

The helper checks the exact baseline and clean worktree before applying. Do not apply the patch to a distribution source tree with local changes. Review and adapt the patch for the target upstream release before proposing it upstream.

## Activating this host in an existing session

The optional activation helper supports a dedicated systemd user service that already starts the XFCE desktop. Supply that service's name; for example:

```sh
python3 scripts/activate-live.py activate --unit my-xfdesktop.service
```

Rollback is available with:

```sh
python3 scripts/activate-live.py rollback --unit my-xfdesktop.service
```

The helper requires an existing running `systemd --user` service, X11, and a build in the private prefix. It does not set up a service or change ordinary XFCE session autostart. Other session setups need their own integration. Review [architecture and platform limits](ARCHITECTURE.md) before adapting it.

## Building standalone Tweak packages

After building, make or refresh all separate `.xfce-tweak` imports with:

```sh
./scripts/package-tweaks.sh
```

Packages are written to `packages/`, one archive per Tweak, and can be imported through XFCE Tweaks Manager. Developers can also build/package one module with `scripts/tweak-tool.py`; see [Developing a Tweak](DEVELOPING.md) and the [SDK API reference](API-REFERENCE.md).

## Uninstallation

In XFCE Tweaks Manager, use **Uninstall** next to a module and confirm. The manager disables it first, removes its installed module directory, and retains its saved Xfconf settings. The operation is limited to the user module directory and the configured private build prefix. A library already mapped by xfdesktop remains resident until xfdesktop exits; restart the modular desktop before reimporting the same ID.

To remove the entire private development prefix, close the preview and use `python3 scripts/remove-development-build.py`. This keeps the source repository, imported user Tweaks, and existing distribution desktop intact.
