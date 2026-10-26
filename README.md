# XFCE Tweaks — modular desktop environment

A native extension host built on **pristine upstream xfdesktop 4.21.0**, with Desktop Groups and Video Wallpaper as its first two independently compiled, dynamically loaded Tweaks. This is an experimental developer release for Fedora 44 / XFCE / X11.

## Try it now

Run `./scripts/preview.py` to start the new desktop and manager in a separate Xephyr window with sample desktop files and a private D-Bus session. Your normal desktop continues running.

Inside the preview:

1. Open **XFCE Tweaks** and use the Desktop Groups and Video Wallpaper switches. Changes take effect without restarting xfdesktop.
2. Select the sample icons, right-click, and use **Desktop Groups** to make and customize a group.
3. Right-click the desktop and use **Video Wallpaper → Choose Video…** to select a video file.
4. Use **Import Tweak…** to install a trusted `.xfce-tweak` package into the preview. New imports start disabled; enable them with their switch. The manager also notices externally imported Tweaks without reopening.

Closing the Xephyr window closes the preview. Preview settings, imported Tweaks, and sample desktop files persist under `~/.local/state/xfce-tweaks-preview/`. This is a separate test desktop and configuration, not a security sandbox: native modules have your user account's normal file permissions.

**XFCE Tweaks Manager** detects the host through a session-scoped D-Bus name. If an older desktop build is running instead, it clearly reports that state and disables the switches rather than showing controls that cannot work.

## Build

The private development build installs under `~/.local/lib/xfce-tweaks-modular/`. For full build, host-patch, live activation and rollback instructions, see [Installation](docs/INSTALLATION.md). The complete SDK calls are in the [API reference](docs/API-REFERENCE.md); [GitHub upload instructions](docs/GITHUB-UPLOAD.md) describe this source bundle. To build and preview it:

```bash
./scripts/install-dependencies.sh
./scripts/install-test-dependencies.sh
./scripts/build.sh
./scripts/preview.py
```

Run as your normal user. Only the dependency helpers use sudo, to install Fedora packages. The build installs private libraries, the host, the SDK, all bundled modules, and the manager. It never overwrites `/usr/bin/xfdesktop`, modifies autostart, or changes the active desktop service. All project/XFCE source inputs are bundled under `upstream/`; Fedora dependency installation requires network access.

Close the preview before rebuilding/installing its prefix. A build lock prevents this installer from overwriting an active preview. To build just the existing modules:

```bash
./scripts/build.sh --modules-only
```

For a different private prefix, set `XFCE_TWEAKS_PREFIX` consistently for build, manager, developer tools, tests and preview.

## Displays and video scaling

Drag unlocked Desktop Groups, HP, Clock and Weather cards across display boundaries to move them, with saved placement. Group icons move with their group while remaining at their original filesystem locations. Right-click the target desktop → **Video Wallpaper → Scaling → Fill display** to fill an ultrawide screen without stretching the video. Fit and Stretch are also available independently per display. Each display now has its own **Choose Video**, **Play/Pause**, **Mute**, and **Video Decode GPU** controls in that context menu. The GPU selector offers Automatic, Intel VA-API, and NVIDIA NVDEC. See [display behavior and tests](docs/DISPLAYS-SCALING.md).

## External display dimming

The optional **External Display Dimming** Tweak mirrors the laptop's XFCE brightness slider to a connected external display using an XRandR gamma ramp. It is software dimming, so it does not lower the monitor's physical backlight power. See [configuration and limitations](docs/EXTERNAL-DIMMING.md).

## Clock and Weather widgets

Two separate, draggable and resizable desktop Tweaks with their own theme controls. Enable **Clock Widget** and/or **Weather Widget** in the manager. Click a widget to configure it; Weather lets you search and change location. See [settings, provider terms, build and testing](docs/CLOCK-WEATHER.md).

## Hardware Monitor widgets

Enable CPU, GPU, Memory, and Storage as four independent native widgets. They share the same appearance and per-monitor positioning controls; GPU telemetry uses NVML where available. Read [hardware data sources and limitations](docs/HARDWARE-MONITORS.md).

## Create your own Tweak

```bash
python3 scripts/tweak-tool.py new my-tweak /path/to/my-tweak
python3 scripts/tweak-tool.py build /path/to/my-tweak
python3 scripts/tweak-tool.py package /path/to/my-tweak
```

Import the resulting `my-tweak-0.1.0.xfce-tweak` through the manager. The template draws a themed label on the native desktop and demonstrates the complete public entry point. Edit `tweak.c`, `tweak.ini`, and `meson.build`; you do not need to edit or rebuild xfdesktop for this workflow.

The Tweaks can also be built individually:

```bash
python3 scripts/tweak-tool.py build modules/desktop-groups
python3 scripts/tweak-tool.py build modules/video-wallpaper
python3 scripts/tweak-tool.py build modules/victus-control
```

The separate `.xfce-tweak` files under [`packages/`](packages/) are ready to import into the manager. `./scripts/package-tweaks.sh` rebuilds all package archives after a source update, including module sources and licenses. Import a package with **Import Tweak…**. New imports start disabled. Use **Uninstall** beside a module to disable it and remove its installed files from the manager; saved settings remain for later reinstallation. Restart the modular desktop before reinstalling the same ID because native libraries stay resident until the host exits.

Replacing a loaded Tweak's binary requires closing and restarting the preview. Enabling, disabling, and importing a new ID do not. Disabled libraries remain mapped until the host exits, avoiding unsafe library unloading while GTK callbacks might still refer to their code.

See [the SDK guide](docs/DEVELOPING.md) for lifecycle, rendering, menus, icon layout, capability declarations, and package details, plus the complete [API reference](docs/API-REFERENCE.md).

## What lives where

| Directory | Purpose |
| --- | --- |
| `host/` | Fresh upstream xfdesktop plus a generic extension bridge and module loader |
| `sdk/` | Public GTK/GLib C interface, with no xfdesktop private headers |
| `modules/desktop-groups/` | Group model, layout, drawing, interactions and configuration UI |
| `modules/video-wallpaper/` | GStreamer playback, looping, menus and file chooser |
| `manager/` | GTK manager, event-driven Xfconf switches and package importer |
| `examples/hello-desktop/` | Minimal standalone Tweak template |
| `upstream/` | Untouched upstream source archives |
| `patches/` | Reviewable host changes against the pristine baseline |
| `tests/` | Model, ABI, import, real desktop, GUI and preview tests |

The archive was checked byte-for-byte against the [official XFCE download](https://archive.xfce.org/src/xfce/xfdesktop/4.21/xfdesktop-4.21.0.tar.xz). Version 4.21.0 is intentionally pinned for this first SDK rather than mixing in another upstream update during the extraction.

## Persistence and module discovery

The manager and host share Xfconf properties in `xfce4-desktop`:

- `/xfce-tweaks/enabled/<id>` — persistent requested enabled state.
- `/xfce-tweaks/status/<id>` — last host-reported module status.
- `/xfce-tweaks/rescan` — import notification; discovers new user modules without restart.

Discovery checks user modules under `$XDG_DATA_HOME/xfce-tweaks/modules/`, optional development paths from `XFCE_TWEAKS_MODULE_PATH`, then the private prefix's `lib/xfce-tweaks/modules/`. Groups keep their existing `xfce4/desktop-groups.ini` format. Video keeps XFCE monitor/workspace wallpaper paths and `/video-wallpaper/paused` / `muted` preferences. Preview XDG directories are separate from your normal configuration.

Native modules are trusted code running inside xfdesktop. Import validates paths, bounds, architecture and manifest ABI without executing the package. Runtime loading also checks the exported ABI, structure sizes and identity. A broken native Tweak can still crash the host. Use the isolated preview for development.

## Tests and limits

See [test results](docs/TEST-RESULTS.md) and [architecture](docs/ARCHITECTURE.md). The tests exercise real native desktop behavior, GUI import, live toggles, dialog teardown and restart restoration. `./scripts/test.sh` reruns the suite without replacing the active desktop.

This is an initial **experimental SDK**, not an upstream XFCE plugin standard. New kinds of desktop integration may still require extending the host API. X11 is implemented and tested; Wayland is not supported. If Video Wallpaper is disabled while XFCE’s selected wallpaper is a video, choose a still image to provide a static background; no previous still-image selection is automatically restored. The video module now prefers the OpenGL GTK sink and falls back to the software GTK sink if OpenGL setup/playback fails. Automatic decode currently selects an available hardware decoder; the per-display selector can prefer Intel VA-API or NVIDIA NVDEC. The old backend's smart pause on window overlap and its complete image-style handling have not been migrated. Multiple physical monitors/scaling and full logout/reboot with this new host still need hardware/session testing. Only one icon-layout provider and one backdrop provider can be enabled at a time; conflicts are reported by the manager.

The optional `scripts/activate-live.py` helper supports a dedicated systemd user service under X11. It backs up startup files, checks host startup, and rolls back on failure. It is not a general installer for unmanaged sessions; use the isolated preview first and review [Installation](docs/INSTALLATION.md) before adapting it to another service setup.

## Remove the development build

Close the preview, then:

```bash
python3 scripts/remove-development-build.py
```

This removes the marked private development prefix and preserves the source project, preview data, imported user modules, and the distribution desktop. Build directories in this project may be deleted separately when no tests or preview are running. The distribution dependencies remain installed.

GPL-2.0-or-later for this project's host modifications, SDK, modules and tools; original upstream/library licenses are preserved. See `LICENSE` and the bundled archives.

HP laptop support: [HP Victus/Omen Control widget](modules/victus-control/README.md) adds power profiles, fan modes/manual requests/curves, temperatures, and keyboard RGB to the modular environment.

[Desktop Visibility](modules/desktop-peek/README.md) adds double-click hide/show for native icons, groups and widgets. Group opacity now fades assigned icon images and labels along with the background, including hover transitions.
