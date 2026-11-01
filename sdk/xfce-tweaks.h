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
/* Shared desktop-object geometry and alignment snapping. Coordinates are
 * translated to the X11 root desktop so every native module shares one layout. */
typedef struct { GdkRectangle bounds; } XtLayoutObject;
static inline GHashTable *xt_layout_objects(GtkWidget *view, gboolean create) {
  if (!view) return NULL;
  GdkDisplay *display=gtk_widget_get_display(view);
  GHashTable *objects=g_object_get_data(G_OBJECT(display),"xt-layout-objects-1");
  if (!objects && create) {
    objects=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    g_object_set_data_full(G_OBJECT(display),"xt-layout-objects-1",objects,(GDestroyNotify)g_hash_table_unref);
  }
  return objects;
}
static inline void xt_layout_register(GtkWidget *view,const char *id,GdkRectangle local) {
  if (!view || !id || !gtk_widget_get_realized(view)) return;
  GdkWindow *window=gtk_widget_get_window(view);if(!window)return;
  int ox=0,oy=0;gdk_window_get_origin(window,&ox,&oy);
  XtLayoutObject *object=g_new(XtLayoutObject,1);
  object->bounds=(GdkRectangle){ox+local.x,oy+local.y,local.width,local.height};
  g_hash_table_replace(xt_layout_objects(view,TRUE),g_strdup(id),object);
}
static inline void xt_layout_unregister(GtkWidget *view,const char *id) {
  GHashTable *objects=xt_layout_objects(view,FALSE);if(objects&&id)g_hash_table_remove(objects,id);
}
static inline void xt_layout_unregister_prefix(GtkWidget *view,const char *prefix) {
  GHashTable *objects=xt_layout_objects(view,FALSE);if(!objects||!prefix)return;
  GHashTableIter iter;gpointer key;g_hash_table_iter_init(&iter,objects);
  while(g_hash_table_iter_next(&iter,&key,NULL))if(g_str_has_prefix(key,prefix))g_hash_table_iter_remove(&iter);
}
static inline void xt_layout_consider_axis(int candidate,int size,int target,int target_size,int distance,int *best,int *best_distance) {
  int own[3]={candidate,candidate+size/2,candidate+size};
  int peer[3]={target,target+target_size/2,target+target_size};
  for(int i=0;i<3;i++)for(int j=0;j<3;j++){
    int aligned=peer[j]-(own[i]-candidate),delta=ABS(aligned-candidate);
    if(delta<=distance&&delta<*best_distance){*best=aligned;*best_distance=delta;}
  }
}
static inline void xt_layout_snap(GtkWidget *view,const char *id,int *x,int *y,int width,int height,XfconfChannel *channel) {
  if(!view||!id||!x||!y||!channel)return;
  if(!xfconf_channel_get_bool(channel,"/xfce-tweaks/enabled/desktop-peek",FALSE)||
     !xfconf_channel_get_bool(channel,"/xfce-tweaks/desktop-management/snap-enabled",TRUE))return;
  GdkWindow *window=gtk_widget_get_window(view);if(!window)return;
  int ox=0,oy=0;gdk_window_get_origin(window,&ox,&oy);
  int vw=gtk_widget_get_allocated_width(view),vh=gtk_widget_get_allocated_height(view);
  int rx=ox+*x,ry=oy+*y,distance=CLAMP(xfconf_channel_get_int(channel,"/xfce-tweaks/desktop-management/snap-distance",14),2,64);
  gboolean objects_on=xfconf_channel_get_bool(channel,"/xfce-tweaks/desktop-management/snap-to-objects",TRUE);
  gboolean screen_on=xfconf_channel_get_bool(channel,"/xfce-tweaks/desktop-management/snap-to-screen",TRUE);
  int best_x=rx,best_y=ry,dx=distance+1,dy=distance+1;
  if(objects_on){
    GHashTable *objects=xt_layout_objects(view,FALSE);
    if(objects){GHashTableIter iter;gpointer key,value;g_hash_table_iter_init(&iter,objects);
      while(g_hash_table_iter_next(&iter,&key,&value)){if(!g_strcmp0(key,id))continue;GdkRectangle b=((XtLayoutObject*)value)->bounds;
        xt_layout_consider_axis(rx,width,b.x,b.width,distance,&best_x,&dx);
        xt_layout_consider_axis(ry,height,b.y,b.height,distance,&best_y,&dy);
      }
    }
  }
  if(screen_on){
    GdkRectangle edge={ox,oy,vw,vh};
    xt_layout_consider_axis(rx,width,edge.x,edge.width,distance,&best_x,&dx);
    xt_layout_consider_axis(ry,height,edge.y,edge.height,distance,&best_y,&dy);
  }
  gboolean grid_on=xfconf_channel_get_bool(channel,"/xfce-tweaks/desktop-management/snap-to-grid",FALSE);
  if(grid_on){int grid=CLAMP(xfconf_channel_get_int(channel,"/xfce-tweaks/desktop-management/grid-size",16),4,128);
    int gx=((rx+(rx>=0?grid/2:-grid/2))/grid)*grid,gy=((ry+(ry>=0?grid/2:-grid/2))/grid)*grid;
    if(ABS(gx-rx)<=distance&&ABS(gx-rx)<dx){best_x=gx;dx=ABS(gx-rx);}
    if(ABS(gy-ry)<=distance&&ABS(gy-ry)<dy){best_y=gy;dy=ABS(gy-ry);}
  }
  *x=CLAMP(best_x-ox,0,MAX(0,vw-width));*y=CLAMP(best_y-oy,0,MAX(0,vh-height));
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
