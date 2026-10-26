# Developing a Tweak

## Quick workflow

Build the development prefix once with `scripts/build.sh`. It installs `xfce-tweaks-1.pc` and `include/xfce-tweaks-1/xfce-tweaks.h` alongside the extension-enabled host. The SDK requires GTK 3, GModule and Xfconf; feature-specific libraries belong in the Tweak's own Meson project.

```bash
python3 scripts/tweak-tool.py new my-tweak /tmp/my-tweak
python3 scripts/tweak-tool.py build /tmp/my-tweak
python3 scripts/tweak-tool.py package /tmp/my-tweak
```

The package can be imported and switched on inside the preview. You can use your own editor and build system; the helper is a convenience. An independent Meson project needs only `dependency('xfce-tweaks-1')` and a `shared_module()` target. There is no link against xfdesktop or dependency on its source headers.

## Public entry point

Export `xfce_tweak_query(const XtHost *)`, returning a static `XtTweak` descriptor. Check the host's `abi_version` and `struct_size` before accessing the function table. Set your descriptor's version, size, manifest-matching ID, and name. All callbacks are optional.

ABI 1 is the first development contract, not a promise of long-term binary compatibility yet. The loader rejects a wrong major ABI, undersized descriptor or identity mismatch. Future public releases should keep append-only minor evolution or increment the ABI for incompatible changes. No private native structure layout is shared.

## Callback lifecycle

The loader discovers manifests without running code. Enabled modules are loaded and queried lazily with GModule. For each existing native desktop/view it then calls the applicable `attach_desktop` / `attach_view` callbacks. Newly connected monitors receive the same attachment lifecycle.

Disable calls the matching detach callbacks, removes the module from event dispatch, and reports `disabled`. A module must release timers, idle callbacks, signal handlers, media pipelines and widgets it created. An icon-layout provider's native icon positions are reset when disabled; saved logical group membership remains the module's responsibility. Re-enable attaches again to the existing desktops/views and may reload persisted settings.

Libraries remain resident until the host process exits. This is intentional: GTK menus and asynchronous callbacks must never point into unloaded code. Rebuild/replacement of an existing ID requires restarting the preview. Hot switching and importing a new ID require neither host rebuild nor restart.

GTK callbacks and host operations run on the GTK main thread. Marshal any worker results back onto that thread. Opaque icon handles and borrowed snapshots are only valid while the underlying native model items exist; do not retain them across model mutations. Copy stable identifiers if you need persistent references. Host operations can synchronously emit native model changes.

If a module enters a nested GTK dialog loop, detach may occur while that loop is running. Close its dialogs and defer freeing callback state until their call stacks unwind. Desktop Groups and Video Wallpaper demonstrate this handling. Native toplevel destruction also requires releasing any per-desktop playback state before the GTK container is destroyed.

## Hooks and operations

- `draw` runs inside the native icon view, before native icons are drawn. Use the supplied Cairo context and the widget's GTK style, not a new desktop window.
- Pointer, rubber-band and coordinate hooks allow an icon-layout provider to consume only the interactions it owns. Returning false preserves native handling.
- `view_menu` and `desktop_menu` extend existing native menus. Modules own their added widgets and callbacks and must clean them up on detach.
- `file_renamed` communicates native desktop file renames without moving files on behalf of the module.
- `view_state`, `icon_state`, and `icon_id` expose read-only snapshots and opaque item handles.
- Placement, origin and monitor-transfer operations retain the native host's icon bookkeeping, rendering, navigation and actual file operations.
- `backdrop_changed` receives the filename and Xfconf prefix chosen by native monitor/workspace resolution. Returning true supplies the backdrop; otherwise native still-image handling continues.
- `set_backdrop_widget` attaches a GTK widget to a layer inside the existing desktop window, behind icons, with pointer events passing through. It creates no top-level application window.
- `settings` returns the native `xfce4-desktop` Xfconf channel. Use an ID-specific property namespace for new settings. Existing compatibility keys for the two migrated modules are retained intentionally.

Set `XT_CAP_ICON_LAYOUT` if you control icon placement, or `XT_CAP_BACKDROP_PROVIDER` if you supply backdrop widgets. These capabilities are exclusive: enabling a second provider of the same capability reports a conflict instead of letting two modules fight over the desktop. Modules which merely draw or add menus can coexist without those flags. Capabilities describe cooperation, not security restrictions on trusted C code.

## Manifest and package

`tweak.ini` at the package root:

```ini
[Tweak]
Id=my-tweak
Name=My Tweak
Description=What the user can do with it.
Version=0.1.0
ABI=1
Library=libmy-tweak.so
DefaultEnabled=false
```

IDs contain lowercase letters, numbers and hyphens and start with a letter. Library names must be local `.so` filenames. A `.xfce-tweak` is a ZIP containing that manifest, the compiled library, and optionally source/license files. `tweak-tool.py package` adds the module sources, build file and license under `source/`.

Imports are limited to 128 archive entries and 32 MiB uncompressed, reject traversal, absolute paths, links, duplicate entries and special files, and validate ELF architecture/type. Import uses a temporary directory and a same-filesystem rename. Temporary import directories are excluded from discovery. Packages cannot silently overwrite an existing ID. New imports are explicitly disabled before notifying the live host to rescan.

The host independently validates the module identity and ABI at runtime. A native module is not sandboxed or crash-isolated. Distribute source and licenses, build for the target architecture/distribution, and test in the preview first.

## Debugging and tests

`XFCE_TWEAKS_MODULE_PATH` accepts colon-separated module roots containing `<id>/tweak.ini` and its library. User modules have priority at fresh host startup; an already resident module cannot be replaced by rescanning. Inspect `/xfce-tweaks/status/<id>` in Xfconf for `active`, `disabled`, `load-error`, `incompatible`, or a provider conflict. Status is the last host report, not a media playback health check.

`./scripts/test.sh` builds the SDK example, runs import and ABI tests, exercises real desktop/manager interactions under Xvfb/private D-Bus, and tests the interactive Xephyr preview in a separate X server. It does not activate the host in the user's real session.

Host API changes belong in `sdk/xfce-tweaks.h`, the host's generic adapter/dispatcher, tests, and documentation. Feature policy belongs in a module. If a new feature needs an unexposed native operation, extend that boundary rather than including xfdesktop private headers in a Tweak.

### Cooperating desktop surfaces and per-icon opacity

ABI 1 now has optional header-only helpers backed by per-view GObject data. They add no fields to the host/module ABI structs. Interactive view-rendered modules should register `xt_surface_register(view, id, hit_test, data)` on attach and call `xt_surface_unregister` before freeing data. Hit tests must only check bounds and must not mutate registrations. Desktop Visibility uses these to recognize genuinely empty desktop independently of module load order.

An icon-effect provider clears `xt_icon_opacity_clear(view, id)` at the start of each draw, then calls `xt_icon_opacity_set(view, id, item, alpha)` for affected opaque icon handles. Clear again on detach; never dereference item handles. Unspecified icons remain fully opaque. Multiple providers multiply their contributions. The native renderer composites the icon and label together within a bounded Cairo group only when opacity is below one. Backgrounds, membership and hover policy stay in the module. Desktop Groups supplies the current normal/hover fade value for its assigned icons. Updating older hosts requires the included generic renderer patch; old modules remain ABI-compatible.
