/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "xfce-tweaks.h"
static void draw(GtkWidget *view, cairo_t *cr) {
  PangoLayout *text =
      gtk_widget_create_pango_layout(view, "Hello from a Tweak");
  GtkStyleContext *style = gtk_widget_get_style_context(view);
  gtk_render_layout(style, cr, 12, gtk_widget_get_allocated_height(view) - 32,
                    text);
  g_object_unref(text);
}
static const XtTweak tweak = {.abi_version = XT_ABI_VERSION,
                              .struct_size = sizeof(XtTweak),
                              .id = "hello-desktop",
                              .name = "Hello Desktop",
                              .draw = draw};
G_MODULE_EXPORT const XtTweak *xfce_tweak_query(const XtHost *host) {
  if (!host || host->abi_version != XT_ABI_VERSION ||
      host->struct_size < sizeof(XtHost))
    return NULL;
  return &tweak;
}
