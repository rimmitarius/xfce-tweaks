/* SPDX-License-Identifier: GPL-2.0-or-later
 * Public development ABI 1. Borrowed handles are valid only during callbacks.
 * No xfdesktop private header or structure is part of this interface.
 */
#pragma once
#include <gmodule.h>
#include <gtk/gtk.h>
#include <xfconf/xfconf.h>
#define XT_ABI_VERSION 1
#define XT_CAP_ICON_LAYOUT (1u << 0)
#define XT_CAP_BACKDROP_PROVIDER (1u << 1)

typedef struct {
  GList *items, *selected_items; /* borrowed opaque icon handles */
  gboolean grid_layout, model, definitely_rubber_banding;
  int nrows, ncols, xspacing, yspacing, xmargin, ymargin, slot_padding,
      icon_size;
  double slot_size;
  gboolean label_fg_color_set;
  GdkRGBA label_fg_color;
  GdkRectangle band_rect;
} XtViewState;
typedef struct {
  int row, col;
  gboolean placed, positioned;
  cairo_region_t *icon_slot_region; /* borrowed, read only */
} XtIconState;
typedef struct {
  GtkWidget *source;
  gpointer item;
} XtDrop;
typedef struct {
  guint abi_version;
  gsize struct_size;
  const XtViewState *(*view_state)(GtkWidget *view);
  const XtIconState *(*icon_state)(gpointer item);
  const char *(*icon_id)(GtkWidget *view, gpointer item);
  void (*set_icon_origin)(gpointer item, double x, double y);
  gboolean (*slot_occupied)(GtkWidget *view, int row, int col);
  gboolean (*place_icon)(GtkWidget *view, gpointer item, int row, int col);
  void (*unplace_icon)(GtkWidget *view, gpointer item);
  void (*place_icon_default)(GtkWidget *view, gpointer item);
  void (*transfer_icon)(GtkWidget *source, GtkWidget *destination,
                        gpointer item);
  gboolean (*is_icon_drag)(GtkWidget *view, GdkDragContext *context);
  GPtrArray *(*decode_icon_drop)(GtkSelectionData *data,
                                 guint info); /* XtDrop*, caller frees */
  void (*set_drag_pending)(GtkWidget *view, gboolean pending);
  void (*set_backdrop_widget)(GtkWidget *desktop, GtkWidget *child);
  XfconfChannel *(*settings)(void); /* borrowed xfce4-desktop channel */
} XtHost;
typedef struct {
  guint abi_version;
  gsize struct_size;
  const char *id;
  const char *name;
  void (*attach_view)(GtkWidget *, const char *monitor_id);
  void (*detach_view)(GtkWidget *);
  void (*draw)(GtkWidget *, cairo_t *);
  gboolean (*coords_to_slot)(GtkWidget *, int, int, int *, int *);
  gboolean (*button_press)(GtkWidget *, GdkEventButton *);
  gboolean (*button_release)(GtkWidget *, GdkEventButton *);
  gboolean (*motion)(GtkWidget *, GdkEventMotion *);
  void (*leave)(GtkWidget *);
  void (*band_complete)(GtkWidget *, GdkEventButton *, gboolean);
  void (*icon_dropped)(GtkWidget *destination, GtkWidget *source, gpointer item,
                       int x, int y);
  void (*view_menu)(GtkWidget *, GtkWidget *menu, int x, int y);
  void (*file_renamed)(GFile *, GFile *);
  void (*attach_desktop)(GtkWidget *);
  void (*detach_desktop)(GtkWidget *);
  gboolean (*backdrop_changed)(GtkWidget *, const char *filename,
                               const char *property_prefix);
  void (*desktop_menu)(GtkWidget *, GtkWidget *menu);
  guint64 capabilities; /* Declare exclusive icon-layout/backdrop ownership. */
} XtTweak;
typedef const XtTweak *(*XtQuery)(const XtHost *host);
/* Each module exports exactly this entry point. Other symbols stay private. */
G_MODULE_EXPORT const XtTweak *xfce_tweak_query(const XtHost *host);

/* Optional, additive cooperation between modules; no host ABI change.
 * Register interactive surfaces so empty-desktop gestures respect them.
 * Callbacks must only test bounds and must unregister before data is freed. */
typedef gboolean (*XtSurfaceHitTest)(GtkWidget *, double, double, gpointer);
typedef struct { XtSurfaceHitTest test; gpointer data; } XtSurface;
static inline void xt_surface_register(GtkWidget *view, const char *id,
                                      XtSurfaceHitTest test, gpointer data) {
  GHashTable *surfaces = g_object_get_data(G_OBJECT(view), "xt-interactive-surfaces-1");
  if (!surfaces) {
    surfaces = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_object_set_data_full(G_OBJECT(view), "xt-interactive-surfaces-1", surfaces,
                          (GDestroyNotify)g_hash_table_unref);
  }
  XtSurface *surface = g_new(XtSurface, 1);
  *surface = (XtSurface){test, data};
  g_hash_table_replace(surfaces, g_strdup(id), surface);
}
static inline void xt_surface_unregister(GtkWidget *view, const char *id) {
  GHashTable *surfaces = g_object_get_data(G_OBJECT(view), "xt-interactive-surfaces-1");
  if (surfaces) g_hash_table_remove(surfaces, id);
}
static inline gboolean xt_surface_at(GtkWidget *view, double x, double y) {
  GHashTable *surfaces = g_object_get_data(G_OBJECT(view), "xt-interactive-surfaces-1");
  if (!surfaces) return FALSE;
  GHashTableIter iter; gpointer value;
  g_hash_table_iter_init(&iter, surfaces);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    XtSurface *surface = value;
    if (surface->test(view, x, y, surface->data)) return TRUE;
  }
  return FALSE;
}

/* Per-frame icon opacity providers. Opaque item handles are never dereferenced.
 * Clear a provider before filling a frame and on detach. Unspecified icons are
 * fully opaque; cooperating providers multiply their opacity contributions. */
static inline GHashTable *xt_opacity_providers(GtkWidget *view, gboolean create) {
  GHashTable *providers=g_object_get_data(G_OBJECT(view),"xt-icon-opacity-1");
  if (!providers && create) {
    providers=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_hash_table_unref);
    g_object_set_data_full(G_OBJECT(view),"xt-icon-opacity-1",providers,(GDestroyNotify)g_hash_table_unref);
  }
  return providers;
}
static inline void xt_icon_opacity_clear(GtkWidget *view,const char *owner) {
  GHashTable *providers=xt_opacity_providers(view,FALSE);
  if (providers) { GHashTable *items=g_hash_table_lookup(providers,owner);if(items)g_hash_table_remove_all(items); }
}
static inline void xt_icon_opacity_set(GtkWidget *view,const char *owner,gpointer item,double opacity) {
  if (!(opacity>=0 && opacity<=1)) opacity=1;
  GHashTable *providers=xt_opacity_providers(view,TRUE),*items=g_hash_table_lookup(providers,owner);
  if (!items) {items=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free);g_hash_table_insert(providers,g_strdup(owner),items);}
  double *value=g_new(double,1);*value=opacity;g_hash_table_replace(items,item,value);
}
static inline double xt_icon_opacity(GtkWidget *view,gpointer item) {
  GHashTable *providers=xt_opacity_providers(view,FALSE);double opacity=1;
  if (providers) {GHashTableIter iter;gpointer table;g_hash_table_iter_init(&iter,providers);while(g_hash_table_iter_next(&iter,NULL,&table)){double *value=g_hash_table_lookup(table,item);if(value)opacity*=*value;}}
  return opacity;
}
