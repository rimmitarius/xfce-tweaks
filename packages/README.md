# Importable XFCE Tweaks

Each `.xfce-tweak` archive is a standalone manager import for ABI 1. Import it with **XFCE Tweaks → Import Tweak…**. The manager keeps module source under your user data directory, starts new imports disabled, and can later uninstall them.

| Archive | Tweak |
| --- | --- |
| `desktop-groups-0.1.0.xfce-tweak` | Native desktop icon groups |
| `video-wallpaper-0.1.0.xfce-tweak` | Per-display GStreamer video wallpaper |
| `desktop-peek-0.1.0.xfce-tweak` | Double-click desktop visibility toggle |
| `clock-widget-0.1.0.xfce-tweak` | Desktop time widget |
| `weather-widget-0.1.0.xfce-tweak` | Desktop weather widget |
| `cpu-monitor-0.1.0.xfce-tweak` | CPU/per-core and package temperature monitor |
| `gpu-monitor-0.1.0.xfce-tweak` | GPU/VRAM/clock/temperature monitor |
| `memory-monitor-0.1.0.xfce-tweak` | RAM and swap monitor |
| `storage-monitor-0.1.0.xfce-tweak` | Multi-mount storage monitor |
| `victus-control-0.1.0.xfce-tweak` | HP Victus/Omen controls |
| `external-dimming-0.1.0.xfce-tweak` | XRandR software dimming for external displays |

Refresh the archives from source with `./scripts/package-tweaks.sh` after `./scripts/build.sh`. These native modules execute in-process inside xfdesktop; import only packages you trust. Source and license files are included in each archive.

The packager strips debug data and rejects embedded RPATH/RUNPATH entries, so local source/build directories and private development prefixes are not carried into published binaries.
