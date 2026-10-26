/* SPDX-License-Identifier: GPL-2.0-or-later
 * The only bridge allowed to access native icon-view private structures.
 */
#include "xfdesktop-file-icon-model-filter.h"
#include "xfdesktop-icon.h"
static const XtViewState *xt_view_snapshot(GtkWidget *widget) {
  XfdesktopIconView *v = XFDESKTOP_ICON_VIEW(widget);
  XtViewState *s = g_object_get_data(G_OBJECT(v), "xt-view-snapshot");
  if (!s) {
    s = g_new0(XtViewState, 1);
    g_object_set_data_full(G_OBJECT(v), "xt-view-snapshot", s, g_free);
  }
  XfdesktopIconView *icon_view = v;
  *s = (XtViewState){.items = v->items,
                     .selected_items = v->selected_items,
                     .grid_layout = v->grid_layout != NULL,
                     .model = v->model != NULL,
                     .definitely_rubber_banding = v->definitely_rubber_banding,
                     .nrows = v->nrows,
                     .ncols = v->ncols,
                     .xspacing = v->xspacing,
                     .yspacing = v->yspacing,
                     .xmargin = v->xmargin,
                     .ymargin = v->ymargin,
                     .slot_padding = v->slot_padding,
                     .icon_size = v->icon_size,
                     .slot_size = SLOT_SIZE,
                     .label_fg_color_set = v->label_fg_color_set,
                     .label_fg_color = v->label_fg_color,
                     .band_rect = v->band_rect};
  return s;
}
static const XtIconState *xt_icon_snapshot(gpointer handle) {
  ViewItem *i = handle;
  i->extension_snapshot =
      (XtIconState){.row = i->row,
                    .col = i->col,
                    .placed = i->placed,
                    .positioned = i->extension_positioned,
                    .icon_slot_region = i->icon_slot_region};
  return &i->extension_snapshot;
}
static const char *xt_icon_identifier(GtkWidget *widget, gpointer handle) {
  XfdesktopIconView *v = XFDESKTOP_ICON_VIEW(widget);
  GtkTreeIter iter;
  if (!v->model || !XFDESKTOP_IS_FILE_ICON_MODEL_FILTER(v->model) ||
      !view_item_get_iter(handle, v->model, &iter))
    return NULL;
  XfdesktopFileIcon *icon = xfdesktop_file_icon_model_filter_get_icon(
      XFDESKTOP_FILE_ICON_MODEL_FILTER(v->model), &iter);
  return icon ? xfdesktop_icon_peek_identifier(XFDESKTOP_ICON(icon)) : NULL;
}
static void xt_icon_origin(gpointer handle, double x, double y) {
  ViewItem *i = handle;
  i->extension_positioned = TRUE;
  i->extension_x = x;
  i->extension_y = y;
}
static gboolean xt_slot_occupied(GtkWidget *v, int r, int c) {
  return xfdesktop_icon_view_item_in_slot(XFDESKTOP_ICON_VIEW(v), r, c) != NULL;
}
static gboolean xt_place_icon(GtkWidget *v, gpointer i, int r, int c) {
  return xfdesktop_icon_view_place_item_at(XFDESKTOP_ICON_VIEW(v), i, r, c);
}
static void xt_unplace_icon(GtkWidget *v, gpointer i) {
  xfdesktop_icon_view_unplace_item(XFDESKTOP_ICON_VIEW(v), i);
}
static void xt_place_default(GtkWidget *v, gpointer i) {
  xfdesktop_icon_view_place_item(XFDESKTOP_ICON_VIEW(v), i, FALSE);
}
static void xt_transfer_icon(GtkWidget *source, GtkWidget *dest,
                             gpointer item) {
  XfdesktopIconView *s = XFDESKTOP_ICON_VIEW(source);
  GtkTreeIter iter;
  if (s->model && view_item_get_iter(item, s->model, &iter))
    g_signal_emit(dest, __signals[SIG_ICON_MOVED], 0, source, &iter, -1, -1);
}
static gboolean xt_is_icon_drag(GtkWidget *v, GdkDragContext *context) {
  return gtk_drag_dest_find_target(v, context,
                                   XFDESKTOP_ICON_VIEW(v)->dest_targets) ==
         gdk_atom_intern_static_string(XFDESKTOP_ICON_NAME);
}
static GPtrArray *xt_decode_drop(GtkSelectionData *data, guint info) {
  if (info != TARGET_XFDESKTOP_ICON ||
      gtk_selection_data_get_length(data) !=
          (int)sizeof(XfdesktopDraggedIconList))
    return NULL;
  XfdesktopDraggedIconList *native =
      (gpointer)gtk_selection_data_get_data(data);
  GPtrArray *result = g_ptr_array_new_with_free_func(g_free);
  for (GList *l = native->dragged_icons; l; l = l->next) {
    XfdesktopDraggedIcon *i = l->data;
    XtDrop *d = g_new0(XtDrop, 1);
    d->source = GTK_WIDGET(native->source_icon_view);
    d->item = i->item;
    g_ptr_array_add(result, d);
  }
  return result;
}
static void xt_drag_pending(GtkWidget *v, gboolean b) {
  XFDESKTOP_ICON_VIEW(v)->drag_dropped = b;
}
static XfconfChannel *xt_settings(void) {
  return xfconf_channel_get("xfce4-desktop");
}
const XtHost *xt_icon_host_api(void) {
  static const XtHost api = {.abi_version = XT_ABI_VERSION,
                             .struct_size = sizeof(XtHost),
                             .view_state = xt_view_snapshot,
                             .icon_state = xt_icon_snapshot,
                             .icon_id = xt_icon_identifier,
                             .set_icon_origin = xt_icon_origin,
                             .slot_occupied = xt_slot_occupied,
                             .place_icon = xt_place_icon,
                             .unplace_icon = xt_unplace_icon,
                             .place_icon_default = xt_place_default,
                             .transfer_icon = xt_transfer_icon,
                             .is_icon_drag = xt_is_icon_drag,
                             .decode_icon_drop = xt_decode_drop,
                             .set_drag_pending = xt_drag_pending,
                             .set_backdrop_widget = xt_host_set_backdrop,
                             .settings = xt_settings};
  return &api;
}
void xt_reset_icon_layout(GtkWidget *widget) {
  XfdesktopIconView *v = XFDESKTOP_ICON_VIEW(widget);
  for (GList *l = v->items; l; l = l->next) {
    ViewItem *i = l->data;
    if (i->placed)
      xfdesktop_icon_view_unplace_item(v, i);
    i->extension_positioned = FALSE;
  }
  for (GList *l = v->items; l; l = l->next)
    xfdesktop_icon_view_place_item(v, l->data, FALSE);
  gtk_widget_queue_draw(widget);
}
