# HP Victus/Omen Control verification

The module's fixture tests exercise the supported HP board profile without writing to laptop hardware. Isolated X11 checks load the real module in a test desktop and cover widget interaction and persistence.

Verified areas:

- Hardware discovery, board allowlisting, supported power modes, bounded fan requests, RGB/brightness validation, and rejection of invalid operations.
- Curve validation/interpolation, high-temperature maximum-fan override, missing-sensor behavior, controller recovery, and automatic-mode restoration after EOF or watchdog failure.
- Native GTK widget rendering, settings, movement and resizing, per-monitor placement, disable/re-enable, and restart restoration in an isolated desktop.
- Safe helper behavior and syntax/build checks for the optional keyboard RGB companion module.

The fixture suite uses fake sysfs data and does not write to real hardware. The X11 suite uses an isolated display, D-Bus session, and temporary home. Run these checks with:

```sh
python3 tests/test-victus.py
python3 tests/test-victus-controls.py
python3 tests/test-victus-x11.py
```

Physical cooling performance, fan RPM calibration, keyboard color accuracy, suspend/resume during fan control, and behavior across kernel upgrades require manual testing on supported hardware. Firmware acceptance of a request does not prove the physical result. Wayland is not supported by this host build.
