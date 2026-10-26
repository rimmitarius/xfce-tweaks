# Verification

The project includes automated unit, ABI, package-import, widget, and isolated X11 desktop checks. The test harness creates a private display, D-Bus session, Xfconf configuration, data directory, and sample desktop files; it does not replace the running desktop or use the user's desktop configuration.

The latest completed full-suite run covered:

| Area | Result |
| --- | --- |
| Fresh host and SDK build from bundled upstream sources | Passed |
| Desktop Groups and Video Wallpaper standalone builds | Passed |
| Group persistence, geometry, movement, resize, opacity, icon assignment, and overflow | Passed |
| Dynamic module ABI and package import validation | Passed |
| Native X11 desktop and GTK manager interactions | Passed |
| Isolated nested Xephyr preview | Passed |
| Exported host patch against the pinned upstream baseline | Applied cleanly |
| Clock, Weather, HP controls, visibility, and hardware monitor module checks | Passed in their focused test runs |

The desktop assertions cover normal file operations, application stacking, context menus, group operations, video playback and scaling, appearance settings, widget placement, import/enable/disable/uninstall behavior, persistence, and restart restoration. Import validation checks archive paths, symlinks, module identifiers, ELF architecture, and ABI metadata; it cannot prove native code safe to load.

Run the suite from the project root with:

```sh
./scripts/test.sh
```

The preview and X11 suites require the test dependencies installed by `scripts/install-test-dependencies.sh`. Test logs and screenshots are generated locally and are excluded from version control.

Not verified: production-session migration, physical monitor hotplug across all hardware, full logout/reboot restoration on multiple distributions, Wayland, crash isolation, or compatibility with xfdesktop releases other than the pinned baseline. See [Architecture](ARCHITECTURE.md) for platform constraints.
