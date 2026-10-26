# Desktop Visibility

Double-click empty desktop with the left mouse button to hide desktop icons, Groups and native widgets. Double-click again anywhere on the exposed desktop to reveal them. The wallpaper stays visible, including a playing or paused video wallpaper in its current state. Application windows and XFCE panels are unaffected.

Enable **Desktop Visibility** in XFCE Tweaks Manager. The desktop context menu also has a **Desktop Visibility → Show/Hide desktop items** action. Disabling the Tweak restores items immediately. Hidden is temporary: restarting xfdesktop or re-enabling the Tweak always starts with items visible. Coordinates, group membership and files are never changed by this feature.

Double-clicks on files and interactive group/widget surfaces retain their normal actions. Shift/Ctrl/Alt/Super-modified clicks and selection drags do not toggle visibility. The gesture uses GTK's configured double-click timing and distance. It applies to all connected desktop icon views together. Hiding presentation does not disable the modules or stop active hardware controls; their settings windows remain ordinary application windows.

## Build and install

From the project root:

```sh
python3 scripts/install-sdk.py
python3 scripts/tweak-tool.py build modules/desktop-peek
python3 scripts/tweak-tool.py package modules/desktop-peek \
  --output packages/desktop-peek-0.1.0.xfce-tweak
```

Import the package through XFCE Tweaks Manager, then enable it. Dependencies: public XFCE Tweaks SDK, GTK 3, GLib/GModule, xfconf. No root helper, daemon, timer or extra desktop window is required. Disable it before removing its imported directory. Resident-library updates require restarting the desktop as described in the SDK guide.

## Architecture

The Tweak hides the existing GTK icon view, so native icons, group decorations and widget rendering disappear together and hidden icons cannot accidentally launch. A desktop-level GTK event handler receives the reveal gesture while the icon view is hidden. Native event state is released before hiding. New views shown during the hidden state are kept hidden; detach restores only views this module hid. Video/backdrop widgets are on a separate layer.

The additive public SDK surface registry lets interactive modules reserve their hit regions. Desktop Groups and HP Control register their bounds, including locked groups, so behavior is independent of module load order. Existing host ABI 1 structs remain unchanged. Future interactive Tweaks should use `xt_surface_register` and unregister before destroying their per-view state.

Runtime state is exposed in the xfce4-desktop channel as `/xfce-tweaks/desktop-peek/hidden`; it resets to false at restart. No group or desktop-file configuration is rewritten.

## Verification and limitations

Run `bash scripts/test-x11.sh` for the full suite, or prefix it with `XFCE_TWEAKS_TEST_FOCUS=visibility` for focused checks. [Recorded results](../../docs/DESKTOP-VISIBILITY-TEST-RESULTS.md).

Native X11 integration tests cover hide/show, simultaneous icon/group/widget visibility, modifiers, group/widget interactions, disabling while hidden, unchanged files and unchanged video pause state. SDK tests cover surface lifetime and cooperative icon alpha. Physical monitor hotplug and Wayland remain unverified. Desktop objects drawn outside the native icon view must implement their own visibility cooperation; this feature currently covers all desktop objects shipped by this project. Items are temporarily unavailable as drag/drop targets while hidden.

GPL-2.0-or-later; see LICENSE.
