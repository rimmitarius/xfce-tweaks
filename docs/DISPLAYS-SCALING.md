# Cross-display widgets and video scaling

## Move desktop groups

Unlock a group, then drag its body or title across the display edge. The group transfers as soon as the pointer enters the other native desktop view, and its member icons transfer with it. The drag remains active across the two xfdesktop view windows, so you can continue positioning before releasing. Its dimensions and member files are retained; the group assignment and icon placements persist under the destination's stable monitor ID. Dragging back before release transfers it back. The lower-right resize grip remains reserved for resizing.

The transfer updates group ownership before asking xfdesktop to move native icon handles. This ordering matters because icon placement callbacks can run synchronously during transfer. The code uses root pointer coordinates and GTK pointer capture; it creates no overlay window and does not move files on disk. A locked group must first be unlocked.

Member icon origins follow the group on every pointer motion. The module snapshots each visible icon's local grid offset when dragging starts, updates its native drawing and hit-test position as the group moves, and defers the full native slot layout until release. This keeps icons attached during movement and avoids repeated unplace/re-place jumps.

Group capacity and automatic sizing count icons currently present in the native desktop view, so unavailable or deleted paths do not create a false overflow message. Cell spacing and outer padding are compact while cell size follows XFCE's current icon size. Overflow hints reserve footer space only when it leaves a usable icon row; an undersized group keeps showing the icons that fit and reports remaining pages.

## Move widgets

Drag the body of a widget across a display edge. A native preview appears on the destination desktop. Release to commit its position there. The lower-right grip remains a resize control, not a move handle. Groups retain their body/header drag and **Move to display** context actions; locked groups must be unlocked first.

The widget's size and pointer offset are preserved, and its position is clamped to the destination view. Position, size, and visibility are saved under the existing stable monitor hash. The old display's card is hidden when the move commits, rather than leaving a second copy behind. There remains at most one instance of each card type per display: if the destination already displays that type, the drop repositions that card instead of creating another instance. Appearance/content settings remain shared for that widget type.

When the assigned display disappears, a connected view temporarily exposes the card without rewriting its saved assignment. Reconnecting the original display restores the assigned view. Moving/resizing the fallback deliberately establishes placement on the connected display. Detaching a view during a drag clears preview references. Disabling Desktop Visibility still hides the complete native icon view, including the cards.

Implementation: `modules/common/widget-transfer.h`, exported into the widget modules by `scripts/sync-widget-sources.py`. Pointer events use desktop root coordinates to find a destination view; Cairo rendering stays inside existing xfdesktop windows. GTK's grab preserves the drag across those windows. No overlay window, host/SDK change, polling timer, or background process is involved.

## Scale video to a display

Right-click a desktop → **Video Wallpaper → Scaling**:

- **Fill display (crop, keep proportions)** covers the display without stretching. Cropping is centered.
- **Fit whole video (letterbox)** preserves the whole image and its proportions, with bars where needed.
- **Stretch to display** fills the display without cropping, changing proportions if aspect ratios differ. This remains the default for existing installations until a selection is made.

GStreamer's [aspectratiocrop](https://gstreamer.freedesktop.org/documentation/videocrop/aspectratiocrop.html), part of the already-required Good plug-ins, performs cropping before the GTK sink scales the frame. Allocation changes update the crop ratio for resolution changes. Paused frames are refreshed when cropping changes. The original video is never modified or transcoded. Each display is controlled separately; multiple displays are not stitched into one video canvas.

## Independent videos and playback

Play/Pause and Mute act on the display whose context menu is used. Choose Video saves a separate file for that physical display, without changing the native XFCE wallpaper file. The chooser opens at that display's current video. File overrides also work when native XFCE backdrop prefixes are shared by spanning mode.

The public GObject `monitor` property on the desktop exposes XFCE's physical monitor object; its `identifier` (or connector fallback) supplies a stable key. Files/paused/muted use `/video-wallpaper/displays/<SHA256 monitor identifier>/`. The old global booleans are copied into newly discovered display settings once, then independently saved. Scaling retains the existing native backdrop-prefix key.

## Performance and tests

The module prefers GStreamer's GTK OpenGL sink (`glsinkbin` + `gtkglsink`) and falls back to `gtksink` if OpenGL setup or playback fails. Automatic decode selects an available decoder; the per-display selector can prefer Intel VA-API or NVIDIA NVDEC. CPU and GPU load depend on codec, resolution, driver, and sink support; measure on the target system rather than treating a single setup as a performance guarantee.

The isolated X11 test suite exercises group/widget drags between desktop views, reverse movement, pointer capture, restart persistence, disconnection fallback, video scaling negotiation, independent video selection, and play/pause state. Run focused checks with:

```sh
python3 tests/test-widget-transfer.py
python3 tests/test-group-transfer.py
bash tests/test-video-scaling.sh
bash tests/test-video-monitors.sh
bash scripts/test-x11.sh
```

The native host build targets X11; no Wayland claim is made. Physical monitor hotplug and full logout/reboot restoration need hardware/session testing. See [Installation](INSTALLATION.md) and [Architecture](ARCHITECTURE.md).
