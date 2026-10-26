# Desktop Visibility and group icon opacity

Tested with the project host based on pristine xfdesktop 4.21.0 in an isolated XFCE X11 session.

Passed:

* All 63 real-desktop X11 integration checks, including the previous 51 checks for file operations, Groups, Video Wallpaper, GTK manager switches/imports, stacking and restoration.
* Group opacity 0 removes assigned icon images and labels. A transparent-background fixture at 40% opacity matched expected pixel blending with mean absolute error 0.006/255. Unassigned icons stayed pixel-identical. Hover enter/exit also changed the assigned icons with a fully transparent group background.
* Empty-desktop double-click hides icons, Groups and HP Control together. A double-click in the former widget area restores them. Locked group surfaces, widget clicks and modified clicks retain normal behavior. Disabling Desktop Visibility while hidden restores items. Files and video pause state remain unchanged.
* Ten Meson targets: group store/layout, module ABI checks, HP hardware/layout/power fixtures, cooperative surface/opacity helpers, and visibility input exclusions. Icon and label double-click hit regions are explicitly excluded by the input unit test.
* Nine registry tests, manager host-presence/discovery tests, and exported host patch application against the pristine baseline.

Tests use private X servers, buses and homes. Run `bash scripts/test-x11.sh` for the full suite, or `XFCE_TWEAKS_TEST_FOCUS=visibility bash scripts/test-x11.sh` for the focused visibility/opacity cases. The harness stages freshly built modules without replacing live libraries.

Limitations: physical monitor hotplug and Wayland remain unverified. Hiding affects the native icon-view layer, not ordinary application/settings windows or XFCE panels. It does not disable modules or hardware control. Hidden state intentionally resets to visible on restart.
