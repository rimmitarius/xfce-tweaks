# XFCE Tweaks SDK API reference

The canonical declarations are in [`sdk/xfce-tweaks.h`](../sdk/xfce-tweaks.h). ABI 1 is experimental. Build a native shared library against `dependency('xfce-tweaks-1')`; export only `xfce_tweak_query(const XtHost *)` as the module entry point.

## Module entry point and descriptor

`xfce_tweak_query(host)` validates `host->abi_version` and `host->struct_size`, then returns a static `XtTweak` descriptor. The descriptor repeats its ABI version and size and supplies the same ID as `tweak.ini`. Its optional callbacks are:

| Callback | When to use it |
| --- | --- |
| `attach_view(view, monitor_id)` / `detach_view(view)` | Create/release per-monitor state and connect/disconnect view handlers. |
| `draw(view, cairo)` | Draw into the native icon view using its supplied Cairo context. |
| `coords_to_slot(view, x, y, row, col)` | Convert coordinates to a native desktop icon slot. |
| `button_press`, `button_release`, `motion`, `leave` | Handle only interactions owned by the module; return false to preserve native handling. |
| `band_complete(view, event, empty)` | Respond after the native selection rectangle completes. |
| `icon_dropped(destination, source, item, x, y)` | Update logical module state after a native icon drop. |
| `view_menu(view, menu, x, y)` | Add items to the existing icon context menu. |
| `desktop_menu(desktop, menu)` | Add items to the existing empty-desktop context menu. |
| `file_renamed(old, new)` | Update stored file identity when a desktop file is renamed. |
| `attach_desktop` / `detach_desktop` | Create/release state associated with a native desktop/backdrop. |
| `backdrop_changed(desktop, filename, property_prefix)` | Supply a backdrop; return true when handled, false to keep xfdesktop's still-image path. |

Set `capabilities` to `XT_CAP_ICON_LAYOUT` or `XT_CAP_BACKDROP_PROVIDER` only when the module exclusively owns that function. Enabling a second owner reports a conflict.

## Host operations

The borrowed `XtHost` function table exposes these calls:

- `view_state(view)` returns the current borrowed `XtViewState`, with native items, selection, grid dimensions/spacing/margins, icon size and rubber-band state.
- `icon_state(item)` returns a borrowed read-only `XtIconState` containing row, column, placement state and the native icon-slot region.
- `icon_id(view, item)` returns the current stable desktop item identifier string.
- `set_icon_origin(item, x, y)` records icon origin coordinates.
- `slot_occupied(view, row, col)` checks native slot occupancy.
- `place_icon(view, item, row, col)`, `unplace_icon(view, item)`, and `place_icon_default(view, item)` update native icon layout.
- `transfer_icon(source, destination, item)` uses the host's monitor-aware icon transfer bookkeeping.
- `is_icon_drag(view, context)` and `decode_icon_drop(selection_data, info)` inspect native desktop drag payloads. The caller frees the returned `GPtrArray` and its `XtDrop` records.
- `set_drag_pending(view, pending)` coordinates module drag handling with native selection behavior.
- `set_backdrop_widget(desktop, child)` attaches a GTK child to xfdesktop's existing desktop surface; it does not create a top-level window.
- `settings()` returns the borrowed `xfce4-desktop` Xfconf channel. Store new settings below `/xfce-tweaks/<id>/`.

## Cooperation helpers

`xt_surface_register(view, id, hit_test, data)` and `xt_surface_unregister(view, id)` mark interactive module surfaces. `xt_surface_at(view, x, y)` lets cooperating features avoid treating a widget as empty desktop space. Hit tests must only inspect bounds.

For related icon effects, call `xt_icon_opacity_clear(view, owner)` at the start of a frame, then `xt_icon_opacity_set(view, owner, opaque_item, alpha)` for changed icons. Unspecified items remain at full opacity; multiple owners multiply. Clear the owner again on detach. These header-only helpers are additive and do not change ABI struct layout.

## Ownership and limits

Icon handles, state pointers, regions, menu pointers, and the settings channel are borrowed. Do not retain icon handles across model changes. Copy stable IDs for persistence. All GTK operations run on the main thread. Disconnect signals, timers, idles, drag handlers, pipelines, and dialogs before freeing callback state. Modules run as trusted native code in xfdesktop; the manager validates package structure and ABI but does not sandbox modules. Use the Xephyr preview before enabling a new module in a real session.
