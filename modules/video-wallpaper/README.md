# Video Wallpaper

Native XFCE Tweaks module (ABI 1), GPL-2.0-or-later. Each desktop has its own GStreamer player behind normal applications and desktop icons.

Right-click **the display you want to change**, then open **Video Wallpaper**:

* **Play / Pause** affects only that display.
* **Mute** affects only that display's video audio.
* **Choose Video…** selects that display's file. Other displays retain theirs. Playback state is retained when selecting a new file, so a paused display stays paused.
* **Scaling** offers Fill (center crop without distortion), Fit (whole frame with bars), and Stretch. Each native display/workspace backdrop retains its scaling preference.
* **Video Decode GPU** prefers Automatic, Intel VA-API, or NVIDIA NVDEC for that display. Unsupported codecs fall back to a software decoder.

Video, playback and mute settings persist by the physical display identifier provided by XFCE, rather than temporary monitor indices. Choosing a new primary display does not change those assignments. When a display reconnects, it reuses its preferences. Until a custom video is selected in this module, its existing XFCE backdrop file remains the source. Explicit video choices are saved separately and take priority over native backdrop files, so even XFCE's shared/spanning background mode can use different videos on each display. Disabling the Tweak leaves XFCE's original wallpaper preferences intact.

Configuration is in the `xfce4-desktop` Xfconf channel:

* `/video-wallpaper/displays/<SHA256 stable monitor ID>/file`: chosen video path.
* The same prefix's `/paused` and `/muted`: independent booleans.
* The same prefix's `/gpu`: preferred decoder (`auto`, `intel`, or `nvidia`).
* `/video-wallpaper/scaling/<SHA256 native backdrop prefix>`: scaling mode.

On first use, a display copies the old global paused/muted preferences as initial defaults, preserving existing behavior. Subsequent changes and restarts use its own values; the old global keys no longer represent every display's current state. Legacy hosts without a monitor object fall back to the native backdrop prefix for identity.

Dependencies: existing GTK3/GLib/Xfconf SDK and GStreamer playbin, videocrop, and GTK sinks. The module prefers `gtkglsink` wrapped in `glsinkbin` for OpenGL presentation, then falls back to `gtksink`. Hardware decoding depends on installed VA-API/NVDEC plugins and codec support. No additional service or host patch is required.

Build independently from the project root:

```sh
python3 scripts/tweak-tool.py build modules/video-wallpaper
python3 scripts/tweak-tool.py package modules/video-wallpaper
```

Disable through XFCE Tweaks Manager. Never overwrite a mapped `.so`; replace it atomically and restart the desktop manager to load an updated version. See the project `docs/DISPLAYS-SCALING.md` for installation/test details.
