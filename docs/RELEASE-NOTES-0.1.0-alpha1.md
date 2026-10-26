# XFCE Tweaks 0.1.0 Alpha 1

This is the first public source release of XFCE Tweaks, an experimental modular desktop host and SDK built from upstream `xfdesktop` 4.21.0.

## Getting started

On Fedora, clone or download this release and run:

```sh
./scripts/install-preview.sh
```

The guided helper asks before installing Fedora packages, builds into a private user prefix, and opens the isolated Xephyr preview. It does not replace the active desktop manager.

On another distribution, first install the dependency equivalents described in [Build and install](INSTALLATION.md), then run:

```sh
./scripts/install-preview.sh --skip-dependencies
```

Fedora 44 with XFCE on X11 is the only distribution tested for this alpha. Other distributions are not verified and may need newer XFCE libraries. Wayland is not supported by this host build. Review the [README](../README.md), [installation guide](INSTALLATION.md), and [architecture limits](ARCHITECTURE.md) before using it in a live session.

## Included

- Modular xfdesktop host and public Tweak SDK
- XFCE Tweaks Manager with import, enable/disable, and uninstall controls
- Desktop Groups, Video Wallpaper, clock/weather, and hardware monitor modules
- Isolated Xephyr preview, importable Tweak modules, and build documentation

This alpha is for evaluation and development. Native modules run inside xfdesktop with the user's permissions; use the isolated preview when trying unfamiliar modules.

The checked-in `.xfce-tweak` packages contain x86-64 Linux shared libraries built in the Fedora development environment. They may not work on other CPU architectures or systems with incompatible library versions; rebuild modules for your target system when needed.
