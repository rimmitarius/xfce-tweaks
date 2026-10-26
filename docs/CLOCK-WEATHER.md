# Clock and Weather widgets

Two independent native Tweaks: `clock-widget` and `weather-widget`. Enable either through XFCE Tweaks Manager. Click a card to open its own GTK settings; drag its body to move it and the lower-right grip to resize it. They draw in xfdesktop's icon view, create no managed widget windows, stay behind applications, and cooperate with Desktop Visibility. Initial positions are near the lower-right desktop area; move them as desired.

Clock settings: analog/digital display, 12/24-hour time, optional seconds, and system or IANA time zone (for example `Europe/London`). The clock reads wall time, so system clock/time-zone changes are reflected without restarting. A one-second GLib timer checks time, but redraws only on minute changes unless seconds are enabled.

Weather settings: search a city or postal code, select a specific match, then **Use selected location**. Change the location whenever desired. No location is assumed or discovered through IP geolocation. Choose Celsius/km/h or Fahrenheit/mph. The card shows current temperature, condition, feels-like temperature, humidity, wind, and three days of low/high temperatures. Smaller cards omit forecast/detail rows. Conditions are model estimates, not guaranteed local sensor readings.

Both have system-theme, Midnight, Paper and Glass presets; custom background/text/border colors, background image chooser, normal/hover opacity, rounded corners, border width, text scale, and adjustable text shadow strength/color. **Restore default appearance** resets the visual style and size while keeping the card's desktop position and the clock/weather content settings. Shadow strength can be set to zero. Background images use centered cover scaling. Settings update immediately. Background opacity affects the card surface; typography and weather symbols inherit GTK and icon themes.

## Service and privacy

Weather uses [Open-Meteo](https://open-meteo.com/en/docs), with [geocoding](https://open-meteo.com/en/docs/geocoding-api). The public service requires no API key for noncommercial use; [commercial use requires the provider's commercial service](https://open-meteo.com/en/pricing). Data attribution is displayed in the card/settings. Forecast requests send selected coordinates and units. Searches send the entered city/postal code. There are no requests before a location is selected, and disabling Weather stops its timers and owned request process. No always-running weather daemon is installed.

A short-lived Python standard-library helper performs bounded HTTPS requests outside xfdesktop, with a 12-second network timeout and 1 MiB response limit. Normal refresh interval is 15 minutes, shared across monitors. Cached successful responses are written atomically under `$XDG_CACHE_HOME/xfce-tweaks/weather/` (default `~/.cache`). Network failure shows cached results with an explicit stale warning, or an unavailable state. Failures retry at the next interval. Changing location/units cancels an in-flight old request. Search runs in a worker thread in the settings process, keeping GTK responsive.

Settings are under `/xfce-tweaks/<id>/` in the `xfce4-desktop` Xfconf channel. Appearance and content are per Tweak; positions/sizes use stable monitor IDs hashed under `monitors/`. Saved coordinates are clamped for display without overwriting the original when screen geometry changes. Each connected monitor initially receives one instance. Dragging across displays moves a card to the destination and hides its origin instance; see `DISPLAYS-SCALING.md`. Multiple independent clocks/weather locations on the same monitor are not implemented; the selected location is shared across Weather instances.

## Build, packages, removal

From the project root:

```sh
python3 scripts/tweak-tool.py build modules/clock-widget
python3 scripts/tweak-tool.py build modules/weather-widget
bash scripts/package-tweaks.sh
```

Import `packages/clock-widget-0.1.0.xfce-tweak` and `packages/weather-widget-0.1.0.xfce-tweak` through the manager; enable each separately. New modules use the existing SDK and require no host rebuild. The full Meson build also installs both. Dependencies are the existing GTK3/GLib/Xfconf SDK, libdl/libm, Python3/PyGObject GTK3/Xfconf, standard CA certificates, and time-zone data. No new network library dependency.

Disable either Tweak through the manager to remove it from the desktop. To uninstall a user-imported module completely, disable it, restart xfdesktop to release its mapped library, then remove only its directory from `~/.local/share/xfce-tweaks/modules/`. Retained Xfconf settings/cache are harmless and allow reinstalling with the same layout. Never overwrite a library that is still mapped by xfdesktop.

The common implementation lives in `modules/common/{widget.h,content.h,settings.py}`. Run `python3 scripts/sync-widget-sources.py` after editing it. That exports real files into each module so packages are self-contained and independently buildable. A test verifies those exports match. They reuse the established HP card's appearance and pointer interaction conventions; the HP module itself is unchanged.

## Verification

- 13 Meson test targets pass, including both ABI checks and five weather-client tests (WMO conditions, units/formatting, malformed numbers, atomic cache reuse/offline fallback, shared source consistency).
- `python3 tests/test-clock-weather-x11.py`: private Xvfb + D-Bus desktop; independent appearance changes, both native GTK settings windows, body dragging and corner resizing, persisted per-monitor size, independent disable/re-enable, analog rendering, double-click desktop hide/restore, host restart restoration.
- Both standalone SDK builds succeed; both importable packages are produced.
- Open-Meteo geocoding and forecast request formats are covered by the weather client checks; these checks do not set a location in the user's XFCE configuration.
- Native no-window architecture is shared with the previously tested HP widget. Physical monitor disconnect/reconnect and full logout/login are not newly verified for these widgets. Wayland is not supported/tested by this host build.
