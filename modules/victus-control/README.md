# HP Victus/Omen Control

A movable native desktop card for XFCE Tweaks ABI 1, with fan and battery-power gauges, themed/vector icons, CPU temperature, a power-mode badge and battery level indicator. Click the card to open a GTK 3 controls panel; drag anywhere on it to move it. There are no additional desktop context-menu entries. Enable or disable **HP Victus/Omen Control** in XFCE Tweaks Manager. Positions and resized dimensions persist per monitor using the host's monitor identifier. Drag the bottom-right corner to resize; a diagonal grip and resize cursor appear over that corner. Drag the body to move it.

The included writable profile currently supports HP board **8BC2**, used by some Victus/Omen models. Unknown boards remain read-only until they receive a separately reviewed profile. X11 is tested; Wayland is unverified.

## Appearance

Open the card, then select the **Appearance** tab. Changes preview immediately and persist in xfconf under `/xfce-tweaks/victus-control/appearance/`:

* Background color, background image chooser and image removal; images scale to cover the card.
* Background opacity, hover-enter opacity and hover-exit opacity, with a brief transition. Text stays fully readable at its configured color.
* Rounded corner radius, border width and border color, including border alpha. Zero width hides the border.
* Text color and adjustable text shadow strength/color, title text, title visibility, top/bottom title placement and left/center/right alignment.
* Width and height, editable for the display whose widget opened the panel. Defaults are 360 × 320, with a minimum 200 × 110. Dragging the bottom-right grip resizes smoothly, respects monitor bounds, and keeps the top-left corner anchored. Typography follows GTK and icons respond to theme changes.
* **Restore default appearance** resets widget styling and size while preserving its position, fan/power settings, and the RGB state.

The layout automatically switches among a full dashboard, compact dual gauges, a wide horizontal strip, narrow stacked gauges, and a small summary card. Gauges scale uniformly; small layouts keep fan mode/request, battery watts, CPU temperature and battery percentage while leaving secondary details in Controls. Sizes are saved under each monitor key, independently of other displays.

The fan gauge shows automatic/maximum mode or the active controller's requested percentage; it is not a calibrated RPM gauge. Battery watts come from `power_now` in microwatts, with current × voltage fallback when available. Charging/external-power state is explicitly marked unavailable for whole-system consumption. The battery gauge shows the kernel-reported percentage. Updates reuse the existing two-second timer; hover animation runs only during a transition. Background images and icons are cached.

## Controls

* Power: coordinated HP firmware and Intel CPU settings. Low power selects firmware `low-power`, CPU `power` preference, and disables turbo. Balanced selects `balanced`, `balance_performance`, and enables turbo. Performance selects `performance`, CPU `performance` preference, and enables turbo. The Intel governor remains `powersave` so energy preferences are effective. Changes are validated before writing, and failures attempt to restore the previous settings. On systems without Intel P-state policies only the firmware profile is available.
* Fans: firmware automatic, maximum, manual 0–100%, or a user-edited CPU temperature curve. The stock driver exposes one shared PWM request, not separate CPU/GPU fan sliders. Firmware may clamp requests. Zero is an allowed request, not a guarantee that firmware will stop the fans. Lower running/restart limits have not been physically calibrated; the thermal override and watchdog remain active. RPM reporting has not been validated as a reliable physical-speed measurement.
* Curves: enter `60:40, 75:40, 85:60, 91:100` (temperature °C : speed %). Two to eight increasing temperatures (25–100°C), nondecreasing speeds (0–100%). The curve can end below 100%: an independent **CPU ≥90°C → firmware maximum-fan mode** rule always takes precedence. Linear interpolation and delayed downward changes reduce oscillation. Click **Start fan curve**; editing alone does not activate it. Status appears next to the primary readings.
* Temperatures: every available hwmon source, including CPU, NVMe and memory sensors. The CPU reading drives curves. GPU temperature is only shown if a kernel hwmon source provides it; this module does not repeatedly launch nvidia-smi.
* Keyboard: static RGB color and lighting on/off using the companion driver. It updates the established zone-0 RGB field while preserving the rest of HP's table. Animations, per-key effects, and independent zone editing are not implemented. Software readback is verified; visual color accuracy requires checking the keyboard.

Curves run in the dedicated root-owned `xfce-tweaks-victus-fans.service`, independently of the panel, widget visibility, and login session. Closing settings does not stop a curve. **Automatic fans**, **Maximum fans**, or applying a power profile stops the service before applying the new mode. Starting manual speed also stops it; manual speed remains tied to the panel heartbeat. The curve service uses one-second temperature checks and an eight-second systemd watchdog; sensor failure, normal service shutdown, or watchdog termination restores firmware automatic control. The watchdog uses SIGKILL followed by `ExecStopPost=... fan-reset`, so even a frozen controller has an out-of-process fallback. Curves are not automatically started at boot; active settings live under root-owned `/run/xfce-tweaks/curve.json`, while the editor retains user preferences in XDG config. Disabling/hiding the widget does not stop this independent curve—use **Automatic fans** first if desired. `uninstall-helper.sh` stops the service before removing it. The root lock prevents concurrent manual and background controllers. RGB and power use firmware state; automatic reapplication at login is not implemented.

## Understanding power readings

The panel shows the actual CPU preference and turbo state, plus battery discharge in watts and an average of up to 30 seconds. The average resets when the firmware profile changes. It is unavailable while charging. Wait for workload and readings to settle when comparing modes: a firmware profile name does not guarantee a particular wattage, and display brightness, applications, GPU activity, and video wallpaper contribute to whole-machine draw. Low power trades performance for efficiency; it is not a hard total-power cap. On 8BC2 the upstream kernel thermal parameter table maps Balanced and Low Power to the same firmware base mode; the added CPU preference and turbo settings provide the distinction. A short live alternating comparison was inconclusive, so improved battery life is not claimed.

The board profile's `cpu_profiles` map in `profiles.json` defines the CPU behavior. Reinstall the root-owned profile after reviewing edits. Settings are applied on request, not continuously enforced; another power manager may change them. Avoid competing profile controllers. See [Linux platform-profile documentation](https://docs.kernel.org/userspace-api/sysfs-platform_profile.html) and [Intel P-state documentation](https://docs.kernel.org/admin-guide/pm/intel_pstate.html).

## Build and installation

From the project root, after building/installing the XFCE Tweaks SDK:

```sh
python3 scripts/tweak-tool.py build modules/victus-control
python3 scripts/tweak-tool.py package modules/victus-control \
  --output packages/victus-control-0.1.0.xfce-tweak
bash modules/victus-control/assets/install-helper.sh
```

Import the package in XFCE Tweaks Manager and enable it. Hardware changes prompt for administrator authentication via polkit. No privilege is given to xfdesktop or its modules. The helper accepts only bounded fan, power, RGB and brightness operations and uses a root-owned board profile. There is no hardware daemon at idle.

Dependencies: public XFCE Tweaks SDK, GTK 3, xfconf, GLib/GModule, libdl, libm; Python 3, PyGObject GTK 3, polkit/pkexec. Fedora driver build additionally needs matching `kernel-devel`, make, gcc, and mokutil. Stock `hp-wmi` remains loaded.

### Optional RGB companion

The small GPL-2.0-or-later `driver/xfce_hp_rgb.c` exposes only keyboard color/brightness. It does not replace hp-wmi or expose arbitrary EC/WMI commands. Loading it performs a read without changing lighting. Its DMI allowlist is HP board 8BC2.

```sh
# Secure Boot: use an existing enrolled signing certificate and its private key.
  HP_RGB_SIGN_KEY=/path/to/private-key \
HP_RGB_SIGN_CERT=/path/to/enrolled-certificate.der \
  bash modules/victus-control/driver/install.sh
# Without Secure Boot, signing variables are optional.
```

The installer builds in a temporary path without spaces (a Kbuild requirement), installs under `/lib/modules/$(uname -r)/extra`, and registers a modules-load entry. **Rebuild after kernel upgrades.** DKMS/akmods automation is not implemented. Keep private signing keys outside this project and packages. Never disable Secure Boot merely to load this module.

Imported packages contain driver source under `source/driver` and helper scripts under `assets`; invoke their shell scripts with `bash`.

### Uninstall

Disable the Tweak and close the controls first. From the project source:

```sh
bash modules/victus-control/assets/uninstall-helper.sh
bash modules/victus-control/driver/uninstall.sh
```

Remove the imported `~/.local/share/xfce-tweaks/modules/victus-control` directory while disabled. Restart xfdesktop before importing a different compiled library with the same ID, as required by the host's resident-library policy. Saved widget positions and curve preferences can be retained or removed separately. Uninstalling does not reset power or keyboard preferences; choose desired settings before removal.

## Extending to another HP laptop

Add a reviewed DMI board entry in `assets/profiles.json` with exact vendor, supported modes, PWM path, minimum request and critical temperature; reinstall the root-owned profiles with install-helper.sh. Do not assume Victus/Omen models share firmware commands. Independently review and extend the kernel DMI allowlist and keyboard table layout for RGB. Unknown boards must fail closed. No generic raw-EC backend is provided.

## Architecture and protocol references

The module uses only the public Tweak SDK and GTK signals. It draws in xfdesktop's existing icon view; it creates no managed window until the user opens controls. Sensor reads run every two seconds while enabled, and stop on detach. GTK theme typography and colors are inherited. Controls are single-instance across monitors. The root fan session exists only for active manual/curve control.

Primary references:

* [Linux hp-wmi v7.2](https://github.com/torvalds/linux/blob/v7.2/drivers/platform/x86/hp/hp-wmi.c): native fan modes/PWM and platform-profile interfaces; GPL-2.0-or-later.
* [hp-omen-extra](https://github.com/yunusemreyl/omen-space/blob/main/driver/hp-omen-extra.c), Yunus Emre YILMAZ, GPL-2.0-or-later: HP BIOS WMI framing and keyboard color table read/modify/write. The new companion credits this work in its source header.
* [OmenCore HP WMI research](https://github.com/theantipopau/omencore/blob/main/src/OmenCore.Core/Hardware/HpWmiBios.cs): cross-check only. The profile preserves the board's observed 128-byte table and edits the documented RGB fields only.

License: GPL-2.0-or-later; see the project LICENSE. No signing keys or prebuilt kernel modules are distributed.

## Verification

```sh
python3 tests/test-victus.py
build-modules/test-abi modules/victus-control/build/libvictus-control.so
python3 tests/test-victus-x11.py
```

Fixture tests cover board restrictions, invalid operations, modes, manual bounds, profile/RGB/brightness, curve interpolation/rejection, thermal override, EOF and heartbeat failover. Isolated X11 tests exercise real host loading, opening controls, dragging, disable/re-enable, desktop window type and restart restoration. Physical multi-display reconnection, long-term thermal behavior, suspend/resume while controlling fans, and visual RGB/brightness confirmation remain manual checks. Never equate firmware command acceptance with verified thermal performance.

Cards can now be dragged across displays with a native preview. Size and placement persist by display, with temporary fallback if the assigned monitor disconnects. See `docs/DISPLAYS-SCALING.md` in the project.
