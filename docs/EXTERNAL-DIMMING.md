# External Display Dimming

**External Display Dimming** mirrors the laptop's XFCE backlight slider to a connected external display. Toggle the Tweak in XFCE Tweaks Manager. The built-in panel keeps using its normal hardware brightness control; the module scales the external display's existing XRandR RGB gamma ramps by the laptop panel's current `brightness / max_brightness` ratio.

This is a software dimming effect, not monitor backlight control. It does not reduce the monitor's power use, and extremely low laptop brightness also makes the external display very dark. The module automatically selects the first connected external XRandR output; internal laptop panel outputs are skipped.

The module captures the monitor's current color ramps before dimming, scales those values directly, and restores them when disabled. It stores the original ramp as a private XRandR output property while active so restarting xfdesktop does not compound the dimming or discard the original color ramp. It checks the sysfs brightness value every 120 ms and checks display connection changes every 1.5 seconds; it runs only while the Tweak is enabled and a native desktop view exists.

## Build and enable

Build with the normal module build:

```bash
meson setup --reconfigure build-modules modules
meson compile -C build-modules external-dimming
```

The module needs GTK 3, X11, and XRandR development libraries, already used by the XFCE module build. The project installer puts it under the modular prefix. Alternatively, copy `tweak.ini` and `libexternal-dimming.so` into `$XDG_DATA_HOME/xfce-tweaks/modules/external-dimming/`, ask the host to rescan with **Rescan** in the manager, and toggle **External Display Dimming** on. Disable the module in the manager to restore the captured color ramps.

## Limitations

It requires an X11 session and an XRandR gamma ramp; Wayland is unsupported. It does not control the monitor's physical LED backlight or save energy. Only one external output is controlled at a time. If multiple external displays are connected, the first connected output reported by XRandR is selected. DDC/CI hardware control and a per-display selector are not implemented.
