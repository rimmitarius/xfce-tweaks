# Hardware Monitor widgets

Desktop Groups' modular manager has four independent, native monitor cards: **CPU Monitor**, **GPU Monitor**, **Memory Monitor**, and **Storage Monitor**. Each can be enabled or disabled on its own. They render inside xfdesktop, remain below application windows, can be dragged between displays, resize from the lower-right corner, and save per-display position and dimensions.

All four use the same Appearance controls as the other desktop cards: theme/custom colors, background image, background and hover opacity, rounded corners, border, title text/visibility/alignment/placement, font scale, text shadow strength/color, and width/height. Each settings page includes **Restore default appearance**, which resets visual settings and size while leaving the widget's position and monitor assignment intact. Storage Monitor accepts several mount paths (one per line, or comma/semicolon separated), shows each filesystem's used percentage and free/total capacity, and can add multiple paths with its file chooser. The GPU card can show both GPUs or one selected GPU.

## Data sources and limits

- **CPU**: aggregate and per-logical-CPU utilization from `/proc/stat`; average reported frequency from cpufreq sysfs; package temperature from the `coretemp` hwmon interface. Core identifiers follow Linux logical CPU numbering. The widget labels the temperature as unavailable if the sensor is absent.
- **GPU**: NVIDIA utilization, VRAM use, graphics/memory clocks, temperature and power through dynamically loaded NVML (`libnvidia-ml.so.1`). Intel GT frequency and an exposed GPU hwmon temperature are read from i915 sysfs. Standard i915 interfaces do not expose a GPU busy percentage, GPU temperature, or dedicated VRAM usage on all systems, so those readings are labeled unavailable/shared rather than invented. Driver/board support can make individual NVML fields unavailable.
- **Memory**: physical total, available, used and swap from `/proc/meminfo`.
- **Storage**: total/available/used for up to eight selected mount paths using `statvfs`; read/write throughput aggregates top-level physical block devices from `/proc/diskstats`, so those rates describe system-wide disk I/O rather than only selected mounts.

Each module samples only its own metrics once per second and stops its timer when disabled. NVIDIA telemetry is queried directly through NVML; it does not spawn `nvidia-smi` each second. Wayland is not supported by this XFCE host build.

## Build and import

```sh
python3 scripts/sync-monitor-sources.py
python3 scripts/tweak-tool.py build modules/cpu-monitor
python3 scripts/tweak-tool.py package modules/cpu-monitor
```

Repeat for `gpu-monitor`, `memory-monitor`, and `storage-monitor`. Import the resulting `packages/<id>-0.1.0.xfce-tweak` files through XFCE Tweaks Manager, then enable each module independently. The repository's aggregate Meson build also includes all four.
