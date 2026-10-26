#include "dg-layout.h"
#include "dg-store.h"
#include "xfce-tweaks.h"
#include <math.h>
#define ENABLE_X11 1
/* GtkWidget and opaque icon handles are the only native objects exposed. */
typedef GtkWidget XfdesktopIconView;
typedef void ViewItem;
static const XtHost *host;
#define SLOT_SIZE (host->view_state(GTK_WIDGET(icon_view))->slot_size)

#ifdef ENABLE_X11
#include <gdk/gdkx.h>
#endif

typedef struct {
  XfdesktopIconView *view;
  char *monitor;
  DgGroup *drag, *hover;
  double start_x, start_y, start_w, start_h, pointer_x, pointer_y;
  gboolean resizing, band_valid, in_layout, pointer_grab;
  double pointer_root_x,pointer_root_y;
  GHashTable *drag_offsets;
  GdkRectangle band;
  guint idle;
  int last_rows, last_cols, last_count, last_size, last_width, last_height,
      last_title, last_cell, last_gap;
  GHashTable *images, *pages, *fades;
  guint tick;
  gboolean drag_pending, retired;
  guint dialog_depth;
  GList *owned_widgets;
} DgView;
typedef struct {
  double from, value, target;
  gint64 start;
} DgFade;
static DgStore *dg_store;
static GList *dg_views;
static DgView *dg_state(XfdesktopIconView *view);
static void dg_queue(DgView *v);
static void dg_end_drag(DgView*v){
  if(v->pointer_grab){gdk_seat_ungrab(gdk_display_get_default_seat(gtk_widget_get_display(v->view)));v->pointer_grab=FALSE;}
  if(gtk_widget_has_grab(v->view))gtk_grab_remove(v->view);
  v->drag=NULL;
  if(v->drag_offsets)g_hash_table_remove_all(v->drag_offsets);
}
static DgView*dg_active_drag(void){for(GList*l=dg_views;l;l=l->next){DgView*v=l->data;if(v->drag)return v;}return NULL;}
static DgView*dg_monitor_view(const char*monitor){for(GList*l=dg_views;l;l=l->next){DgView*v=l->data;if(!g_strcmp0(v->monitor,monitor))return v;}return NULL;}
static DgView*dg_view_at_root(double x,double y){
 for(GList*l=dg_views;l;l=l->next){DgView*v=l->data;if(!gtk_widget_get_realized(v->view)||!gtk_widget_get_visible(v->view))continue;
  int ox,oy;gdk_window_get_origin(gtk_widget_get_window(v->view),&ox,&oy);
  if(x>=ox&&y>=oy&&x<ox+gtk_widget_get_allocated_width(v->view)&&y<oy+gtk_widget_get_allocated_height(v->view))return v;
 }return NULL;
}

static void dg_free(gpointer data);
static gboolean dg_free_idle(gpointer data) {
  dg_free(data);
  return G_SOURCE_REMOVE;
}
static void dg_owned_destroyed(GtkWidget *w, DgView *v) {
  v->owned_widgets = g_list_remove(v->owned_widgets, w);
}
static void dg_own(DgView *v, GtkWidget *w) {
  v->owned_widgets = g_list_prepend(v->owned_widgets, w);
  g_signal_connect(w, "destroy", G_CALLBACK(dg_owned_destroyed), v);
}
static int dg_run(DgView *v, GtkWidget *dialog) {
  dg_own(v, dialog);
  v->dialog_depth++;
  int response = gtk_dialog_run(GTK_DIALOG(dialog));
  v->dialog_depth--;
  if (v->retired && !v->dialog_depth)
    g_idle_add(dg_free_idle, v);
  return response;
}

static gboolean dg_tick(GtkWidget *widget, GdkFrameClock *clock,
                        gpointer data) {
  DgView *v = data;
  gboolean active = FALSE;
  GHashTableIter it;
  gpointer value;
  g_hash_table_iter_init(&it, v->fades);
  while (g_hash_table_iter_next(&it, NULL, &value)) {
    DgFade *f = value;
    double t = CLAMP((g_get_monotonic_time() - f->start) / 150000., 0, 1);
    f->value = f->from + (f->target - f->from) * t;
    if (t < 1)
      active = TRUE;
  }
  gtk_widget_queue_draw(widget);
  if (!active)
    v->tick = 0;
  return active ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}
static void dg_fade(DgView *v, DgGroup *g, double target) {
  DgFade *f = g_hash_table_lookup(v->fades, g->id);
  if (!f) {
    f = g_new0(DgFade, 1);
    f->value = g->opacity;
    g_hash_table_insert(v->fades, g_strdup(g->id), f);
  }
  f->from = f->value;
  f->target = target;
  f->start = g_get_monotonic_time();
  if (!v->tick)
    v->tick =
        gtk_widget_add_tick_callback(GTK_WIDGET(v->view), dg_tick, v, NULL);
}
static void dg_hover(XfdesktopIconView *view, DgGroup *g) {
  DgView *v = dg_state(view);
  if (!v || v->hover == g)
    return;
  if (v->hover)
    dg_fade(v, v->hover, v->hover->hover_exit);
  v->hover = g;
  if (g)
    dg_fade(v, g, g->hover_enter);
}

static void dg_customize(DgView *v, DgGroup *g);
static void dg_name(DgView *v, DgGroup *g);
static ViewItem *dg_find(DgView *v, const char *id);
static DgView *dg_state(XfdesktopIconView *view) {
  return g_object_get_data(G_OBJECT(view), "desktop-groups");
}
static const char *dg_item_id(XfdesktopIconView *view, ViewItem *item) {
  return host->icon_id(view, item);
}
static void dg_save(void) {
  GError *e = NULL;
  if (!dg_store_save(dg_store, &e)) {
    g_warning("Desktop Groups: %s", e->message);
    g_clear_error(&e);
  }
}
static void dg_refresh(void) {
  for (GList *l = dg_views; l; l = l->next) {
    DgView *v = l->data;
    dg_queue(v);
    gtk_widget_queue_draw(GTK_WIDGET(v->view));
  }
}
/* Keep saved coordinates untouched when a display temporarily becomes smaller.
 */
static GdkRectangle dg_rect(DgView *v, DgGroup *g) {
  int w = gtk_widget_get_allocated_width(GTK_WIDGET(v->view)),
      h = gtk_widget_get_allocated_height(GTK_WIDGET(v->view));
  GdkRectangle r = {CLAMP(g->x, 0, MAX(0, w - 96)),
                    CLAMP(g->y, 0, MAX(0, h - 64)), MIN(g->width, MAX(96, w)),
                    MIN(g->height, MAX(64, h))};
  r.width = MIN(r.width, MAX(96, w - r.x));
  r.height = MIN(r.height, MAX(64, h - r.y));
  return r;
}
static DgGroup *dg_at(DgView *v, double x, double y) {
  for (int i = (int)dg_store->groups->len - 1; i >= 0; i--) {
    DgGroup *g = g_ptr_array_index(dg_store->groups, i);
    if (g_strcmp0(v->monitor, g->monitor))
      continue;
    GdkRectangle r = dg_rect(v, g);
    if (x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height)
      return g;
  }
  return NULL;
}
/* Group-local placement shares xfdesktop's themed cell size, but never its
 * screen-wide grid origin or distributed leftover spacing. */
#define DG_PADDING 4
#define DG_GAP 4
static void dg_member_counts(DgView *v, DgGroup *g, int *total,
                             int *placed) {
  *total = 0;
  *placed = 0;
  for (GList *l = host->view_state(v->view)->items; l; l = l->next) {
    ViewItem *item = l->data;
    if (dg_store_owner(dg_store, dg_item_id(v->view, item)) != g)
      continue;
    (*total)++;
    if (host->icon_state(item)->placed)
      (*placed)++;
  }
}
static PangoLayout *dg_title_layout(DgView *v, const char *text) {
  PangoLayout *p = gtk_widget_create_pango_layout(GTK_WIDGET(v->view), text);
  const PangoFontDescription *theme =
      pango_context_get_font_description(pango_layout_get_context(p));
  PangoFontDescription *font = pango_font_description_copy(theme);
  double size = pango_font_description_get_size(font) * 1.35;
  if (pango_font_description_get_size_is_absolute(font))
    pango_font_description_set_absolute_size(font, size);
  else
    pango_font_description_set_size(font, round(size));
  pango_font_description_set_weight(
      font,
      MAX(PANGO_WEIGHT_SEMIBOLD, pango_font_description_get_weight(font)));
  pango_layout_set_font_description(p, font);
  pango_font_description_free(font);
  return p;
}
static int dg_title_height(DgView *v) {
  PangoLayout *p = dg_title_layout(v, "Ag");
  int h;
  pango_layout_get_pixel_size(p, NULL, &h);
  g_object_unref(p);
  return h + 12;
}
static DgGrid dg_geometry(DgView *v, DgGroup *g) {
  XfdesktopIconView *icon_view = v->view;
  GdkRectangle r = dg_rect(v, g);
  int title = g->title_visible ? dg_title_height(v) : 0;
  int cell = MAX(1, (int)ceil(SLOT_SIZE));
  int gap = DG_GAP;
  int live, placed;
  dg_member_counts(v, g, &live, &placed);
  DgGrid grid = dg_grid_compute(r.x, r.y, r.width, r.height, cell, gap,
                                DG_PADDING, title, g->title_bottom, 0);
  if (live > grid.columns * grid.rows) {
    DgGrid with_footer = dg_grid_compute(r.x, r.y, r.width, r.height, cell,
                                         gap, DG_PADDING, title,
                                         g->title_bottom, 24);
    /* Keep at least one usable row; the footer must not make every icon
     * disappear from a group that can otherwise display one. */
    if (with_footer.columns > 0 && with_footer.rows > 0)
      grid = with_footer;
  }
  grid.columns = MIN(grid.columns, host->view_state(icon_view)->ncols);
  grid.rows = MIN(grid.rows, host->view_state(icon_view)->nrows);
  return grid;
}
static void dg_grid_bounds(DgView *v, DgGroup *g, int *r0, int *c0, int *r1,
                           int *c1) {
  XfdesktopIconView *icon_view = v->view;
  DgGrid grid = dg_geometry(v, g);
  /* Logical cells retain native keyboard navigation and position bookkeeping.
   * Their visual origins are relative to the group, independently of these
   * bookkeeping coordinates. Clamp the anchor for groups near monitor edges. */
  *c0 = CLAMP(
      (int)floor((grid.x - host->view_state(icon_view)->xmargin) /
                 (SLOT_SIZE + host->view_state(icon_view)->xspacing)),
      0, MAX(0, host->view_state(icon_view)->ncols - MAX(1, grid.columns)));
  *r0 =
      CLAMP((int)floor((grid.y - host->view_state(icon_view)->ymargin) /
                       (SLOT_SIZE + host->view_state(icon_view)->yspacing)),
            0, MAX(0, host->view_state(icon_view)->nrows - MAX(1, grid.rows)));
  *c1 = *c0 + grid.columns - 1;
  *r1 = *r0 + grid.rows - 1;
}
static void dg_capture_drag_offsets(DgView *v, DgGroup *g) {
  g_hash_table_remove_all(v->drag_offsets);
  int r0, c0, r1, c1;
  dg_grid_bounds(v, g, &r0, &c0, &r1, &c1);
  GHashTableIter it;
  gpointer id;
  g_hash_table_iter_init(&it, g->members);
  while (g_hash_table_iter_next(&it, &id, NULL)) {
    ViewItem *item = dg_find(v, id);
    if (!item)
      continue;
    const XtIconState *state = host->icon_state(item);
    if (!state || !state->placed)
      continue;
    DgMember *offset = g_new(DgMember, 1);
    offset->row = state->row - r0;
    offset->col = state->col - c0;
    g_hash_table_insert(v->drag_offsets, g_strdup(id), offset);
  }
}
static void dg_sync_drag_icons(DgView *v, DgGroup *g) {
  int r0, c0, r1, c1;
  dg_grid_bounds(v, g, &r0, &c0, &r1, &c1);
  DgGrid grid = dg_geometry(v, g);
  GHashTableIter it;
  gpointer id, value;
  g_hash_table_iter_init(&it, v->drag_offsets);
  while (g_hash_table_iter_next(&it, &id, &value)) {
    ViewItem *item = dg_find(v, id);
    if (!item)
      continue;
    DgMember *offset = value;
    int local_row = CLAMP(offset->row, 0, MAX(0, grid.rows - 1));
    int local_col = CLAMP(offset->col, 0, MAX(0, grid.columns - 1));
    int row = r0 + local_row, col = c0 + local_col;
    const XtIconState *state = host->icon_state(item);
    if (!state->placed && row <= r1 && col <= c1) {
      gboolean placed = FALSE;
      if (!host->slot_occupied(GTK_WIDGET(v->view), row, col)) {
        host->set_icon_origin(item, grid.x + local_col * grid.pitch,
                              grid.y + local_row * grid.pitch);
        placed = host->place_icon(GTK_WIDGET(v->view), item, row, col);
      }
      for (int rr = r0; !placed && rr <= r1; ++rr)
        for (int cc = c0; !placed && cc <= c1; ++cc)
          if (!host->slot_occupied(GTK_WIDGET(v->view), rr, cc)) {
            local_row = rr - r0;
            local_col = cc - c0;
            host->set_icon_origin(item, grid.x + local_col * grid.pitch,
                                  grid.y + local_row * grid.pitch);
            placed = host->place_icon(GTK_WIDGET(v->view), item, rr, cc);
          }
    } else if (state->placed) {
      host->set_icon_origin(item, grid.x + local_col * grid.pitch,
                            grid.y + local_row * grid.pitch);
    }
  }
}
static gboolean dg_place(DgView *v, DgGroup *g, ViewItem *item, int row,
                         int col, int r0, int c0) {
  XfdesktopIconView *icon_view = v->view;
  if (host->slot_occupied(icon_view, row, col))
    return FALSE;
  DgGrid grid = dg_geometry(v, g);
  host->set_icon_origin(item, grid.x + (col - c0) * grid.pitch,
                        grid.y + (row - r0) * grid.pitch);
  return host->place_icon(icon_view, item, row, col);
}
static gboolean dg_reserved(DgView *v, int row, int col) {
  XfdesktopIconView *icon_view = v->view;
  GdkRectangle slot = {
      host->view_state(icon_view)->xmargin +
          col * (SLOT_SIZE + host->view_state(icon_view)->xspacing),
      host->view_state(icon_view)->ymargin +
          row * (SLOT_SIZE + host->view_state(icon_view)->yspacing),
      SLOT_SIZE, SLOT_SIZE};
  for (guint i = 0; i < dg_store->groups->len; i++) {
    DgGroup *g = g_ptr_array_index(dg_store->groups, i);
    if (g_strcmp0(g->monitor, v->monitor))
      continue;
    GdkRectangle rect = dg_rect(v, g), intersection;
    if (gdk_rectangle_intersect(&slot, &rect, &intersection))
      return TRUE;
    int r0, c0, r1, c1;
    dg_grid_bounds(v, g, &r0, &c0, &r1, &c1);
    if (row >= r0 && row <= r1 && col >= c0 && col <= c1)
      return TRUE;
  }
  return FALSE;
}
static gboolean dg_coords_to_slot(XfdesktopIconView *icon_view, int x, int y,
                                  int *row, int *col) {
  DgView *v = dg_state(icon_view);
  if (!v)
    return FALSE;
  /* Actual native hit regions take priority over empty cell space. */
  for (GList *l = host->view_state(icon_view)->items; l; l = l->next) {
    ViewItem *item = l->data;
    if (host->icon_state(item)->placed && host->icon_state(item)->positioned &&
        cairo_region_contains_point(host->icon_state(item)->icon_slot_region, x,
                                    y)) {
      if (row)
        *row = host->icon_state(item)->row;
      if (col)
        *col = host->icon_state(item)->col;
      return TRUE;
    }
  }
  DgGroup *g = dg_at(v, x, y);
  if (!g)
    return FALSE;
  DgGrid grid = dg_geometry(v, g);
  int local_col = (x - grid.x) / grid.pitch,
      local_row = (y - grid.y) / grid.pitch;
  if (x < grid.x || y < grid.y || local_col >= grid.columns ||
      local_row >= grid.rows)
    return FALSE;
  int r0, c0, r1, c1;
  dg_grid_bounds(v, g, &r0, &c0, &r1, &c1);
  if (row)
    *row = r0 + local_row;
  if (col)
    *col = c0 + local_col;
  return TRUE;
}
static gboolean dg_layout(gpointer data) {
  DgView *v = data;
  XfdesktopIconView *icon_view = v->view;
  v->idle = 0;
  if (!host->view_state(icon_view)->grid_layout ||
      host->view_state(icon_view)->nrows < 1 ||
      host->view_state(icon_view)->ncols < 1)
    return G_SOURCE_REMOVE;
  /* When a saved display returns, let the native manager move its member
   * icons back from XFCE's fallback display. Snapshot identifiers because
   * each signal can synchronously remove a row from the source model. */
  GList *returning = NULL;
  for (GList *l = host->view_state(icon_view)->items; l; l = l->next) {
    const char *id = dg_item_id(icon_view, l->data);
    DgGroup *g = dg_store_owner(dg_store, id);
    if (g && g_strcmp0(g->monitor, v->monitor))
      returning = g_list_prepend(returning, g_strdup(id));
  }
  for (GList *l = returning; l; l = l->next) {
    DgGroup *g = dg_store_owner(dg_store, l->data);
    for (GList *other = dg_views; g && other; other = other->next) {
      DgView *dest = other->data;
      if (host->view_state(dest->view)->model &&
          !g_strcmp0(dest->monitor, g->monitor)) {
        ViewItem *item = dg_find(v, l->data);
        if (item)
          host->transfer_icon(icon_view, dest->view, item);
        break;
      }
    }
  }
  g_list_free_full(returning, g_free);
  v->in_layout = TRUE;
  int aw = gtk_widget_get_allocated_width(GTK_WIDGET(icon_view)),
      ah = gtk_widget_get_allocated_height(GTK_WIDGET(icon_view));
  int ax = 12, ay = 12, line_h = 0;
  for (guint i = 0; i < dg_store->groups->len; i++) {
    DgGroup *g = g_ptr_array_index(dg_store->groups, i);
    if (g_strcmp0(g->monitor, v->monitor))
      continue;
    if (g->auto_size) {
      int n = 0;
      for (GList *l = host->view_state(icon_view)->items; l; l = l->next)
        if (dg_store_owner(dg_store, dg_item_id(icon_view, l->data)) == g)
          n++;
      int cols = MAX(1, (int)ceil(sqrt(MAX(1, n))));
      int cell = MAX(1, (int)ceil(SLOT_SIZE)), gap = DG_GAP;
      cols = MIN(cols, MAX(1, (aw - DG_PADDING * 2 + gap) / (cell + gap)));
      cols = MIN(cols, MAX(1, host->view_state(icon_view)->ncols));
      int width, height;
      dg_grid_required(n, cols, cell, gap, DG_PADDING,
                       g->title_visible ? dg_title_height(v) : 0, &width,
                       &height);
      if (g->title_visible && *g->title) {
        PangoLayout *title = dg_title_layout(v, g->title);
        int title_width;
        pango_layout_get_pixel_size(title, &title_width, NULL);
        width = MAX(width, title_width + 24);
        g_object_unref(title);
      }
      g->width = MIN(aw, MAX(96, width));
      g->height = MIN(ah, MAX(64, height));
    }
    if (dg_store->auto_arrange && !g->locked) {
      if (ax + g->width > aw && ax > 12) {
        ax = 12;
        ay += line_h + 12;
        line_h = 0;
      }
      g->x = ax;
      g->y = MIN(ay, MAX(0, ah - g->height));
      ax += g->width + 12;
      line_h = MAX(line_h, g->height);
    }
  }
  /* Vacate member slots and any ungrouped icon slots needed by groups. */
  for (GList *l = host->view_state(icon_view)->items; l; l = l->next) {
    ViewItem *item = l->data;
    DgGroup *owner = dg_store_owner(dg_store, dg_item_id(icon_view, item));
    gboolean vacate = owner && !g_strcmp0(owner->monitor, v->monitor);
    if (!owner && host->icon_state(item)->placed &&
        dg_reserved(v, host->icon_state(item)->row,
                    host->icon_state(item)->col))
      vacate = TRUE;
    if (vacate && host->icon_state(item)->placed)
      host->unplace_icon(icon_view, item);
  }
  for (guint i = 0; i < dg_store->groups->len; i++) {
    DgGroup *g = g_ptr_array_index(dg_store->groups, i);
    if (g_strcmp0(g->monitor, v->monitor))
      continue;
    int r0, c0, r1, c1;
    dg_grid_bounds(v, g, &r0, &c0, &r1, &c1);
    int capacity = MAX(0, r1 - r0 + 1) * MAX(0, c1 - c0 + 1), live = 0;
    GList *ids = g_hash_table_get_keys(g->members);
    ids = g_list_sort(ids, (GCompareFunc)g_strcmp0);
    for (GList *l = ids; l; l = l->next)
      if (dg_find(v, l->data))
        live++;
    int page = GPOINTER_TO_INT(g_hash_table_lookup(v->pages, g->id));
    page = CLAMP(page, 0, capacity ? MAX(0, (live - 1) / capacity) : 0);
    g_hash_table_replace(v->pages, g_strdup(g->id), GINT_TO_POINTER(page));
    int index = 0;
    for (GList *l = ids; l; l = l->next) {
      ViewItem *item = dg_find(v, l->data);
      if (!item)
        continue;
      int ordinal = index++;
      if (!capacity || ordinal < page * capacity ||
          ordinal >= (page + 1) * capacity)
        continue;
      DgMember *m = g_hash_table_lookup(g->members, l->data);
      gboolean placed = FALSE;
      if (!g->auto_icons && r0 + m->row <= r1 && c0 + m->col <= c1)
        placed = dg_place(v, g, item, r0 + m->row, c0 + m->col, r0, c0);
      for (int row = r0; !placed && row <= r1; row++)
        for (int col = c0; !placed && col <= c1; col++)
          placed = dg_place(v, g, item, row, col, r0, c0);
    }
    g_list_free(ids);
  }
  /* Release ungrouped icons, including icons hidden on another group page. */
  for (GList *l = host->view_state(icon_view)->items; l; l = l->next) {
    ViewItem *item = l->data;
    if (host->icon_state(item)->placed ||
        dg_store_owner(dg_store, dg_item_id(icon_view, item)))
      continue;
    gboolean placed = FALSE;
    for (int col = 0; !placed && col < host->view_state(icon_view)->ncols;
         col++)
      for (int row = 0; !placed && row < host->view_state(icon_view)->nrows;
           row++) {
        gboolean reserved = dg_reserved(v, row, col);
        if (!reserved)
          placed = host->place_icon(icon_view, item, row, col);
      }
    if (!placed)
      host->place_icon_default(icon_view, item);
  }
  v->in_layout = FALSE;
  gtk_widget_queue_draw(GTK_WIDGET(icon_view));
  return G_SOURCE_REMOVE;
}
static void dg_flush_layouts(void) {
  gboolean pending = FALSE;
  for (GList *l = dg_views; l; l = l->next) {
    DgView *v = l->data;
    if (v->idle) {
      g_source_remove(v->idle);
      v->idle = 0;
      pending = TRUE;
    }
  }
  if (pending)
    for (GList *l = dg_views; l; l = l->next)
      dg_layout(l->data);
}
static void dg_queue(DgView *v) {
  DgView *drag = dg_active_drag();
  if (drag && !drag->resizing)
    return;
  if (!v->retired && !v->idle && !v->in_layout)
    v->idle = g_idle_add(dg_layout, v);
}
static void dg_round(cairo_t *cr, GdkRectangle r, double radius) {
  double a = MIN(radius, MIN(r.width, r.height) / 2.);
  double x = r.x, y = r.y, w = r.width, h = r.height;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - a, y + a, a, -G_PI / 2, 0);
  cairo_arc(cr, x + w - a, y + h - a, a, 0, G_PI / 2);
  cairo_arc(cr, x + a, y + h - a, a, G_PI / 2, G_PI);
  cairo_arc(cr, x + a, y + a, a, G_PI, 3 * G_PI / 2);
  cairo_close_path(cr);
}
static void dg_draw(XfdesktopIconView *icon_view, cairo_t *cr) {
  DgView *v = dg_state(icon_view);
  if (!v)
    return;
  int count = g_list_length(host->view_state(icon_view)->items);
  int width = gtk_widget_get_allocated_width(GTK_WIDGET(icon_view));
  int height = gtk_widget_get_allocated_height(GTK_WIDGET(icon_view));
  int title = dg_title_height(v);
  int cell = ceil(SLOT_SIZE), gap = DG_GAP;
  if (v->last_rows != host->view_state(icon_view)->nrows ||
      v->last_cols != host->view_state(icon_view)->ncols ||
      v->last_count != count ||
      v->last_size != host->view_state(icon_view)->icon_size ||
      v->last_width != width || v->last_height != height ||
      v->last_title != title || v->last_cell != cell || v->last_gap != gap) {
    v->last_cell = cell;
    v->last_gap = gap;
    v->last_width = width;
    v->last_height = height;
    v->last_title = title;
    if (v->last_count == -1)
      dg_refresh();
    v->last_rows = host->view_state(icon_view)->nrows;
    v->last_cols = host->view_state(icon_view)->ncols;
    v->last_count = count;
    v->last_size = host->view_state(icon_view)->icon_size;
    dg_queue(v);
  }
  xt_icon_opacity_clear(GTK_WIDGET(icon_view), "desktop-groups");
  for (GList *l = host->view_state(icon_view)->items; l; l = l->next) {
    if (!host->icon_state(l->data)->placed) continue;
    DgGroup *g = dg_store_owner(dg_store, dg_item_id(icon_view, l->data));
    if (!g || g_strcmp0(v->monitor, g->monitor)) continue;
    DgFade *fade = g_hash_table_lookup(v->fades, g->id);
    xt_icon_opacity_set(GTK_WIDGET(icon_view), "desktop-groups", l->data,
                        CLAMP(fade ? fade->value : g->opacity, 0, 1));
  }
  for (guint i = 0; i < dg_store->groups->len; i++) {
    DgGroup *g = g_ptr_array_index(dg_store->groups, i);
    if (g_strcmp0(v->monitor, g->monitor))
      continue;
    GdkRectangle r = dg_rect(v, g);
    GdkRGBA color;
    if (!gdk_rgba_parse(&color, g->color))
      gdk_rgba_parse(&color, "#202630");
    DgFade *fade = g_hash_table_lookup(v->fades, g->id);
    double opacity = fade ? fade->value : g->opacity;
    cairo_save(cr);
    dg_round(cr, r, g->radius);
    cairo_clip(cr);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue,
                          opacity * color.alpha);
    cairo_paint(cr);
    if (g->image && *g->image) {
      GdkPixbuf *pix = g_hash_table_lookup(v->images, g->image);
      if (!pix) {
        pix =
            gdk_pixbuf_new_from_file_at_scale(g->image, 2048, 2048, TRUE, NULL);
        if (pix)
          g_hash_table_insert(v->images, g_strdup(g->image), pix);
        /* Cache a failed load too: no disk I/O on subsequent frames. */
        else {
          pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
          gdk_pixbuf_fill(pix, 0);
          g_hash_table_insert(v->images, g_strdup(g->image), pix);
        }
      }
      double scale = MAX((double)r.width / gdk_pixbuf_get_width(pix),
                         (double)r.height / gdk_pixbuf_get_height(pix));
      cairo_save(cr);
      cairo_translate(
          cr, r.x + (r.width - gdk_pixbuf_get_width(pix) * scale) / 2,
          r.y + (r.height - gdk_pixbuf_get_height(pix) * scale) / 2);
      cairo_scale(cr, scale, scale);
      gdk_cairo_set_source_pixbuf(cr, pix, 0, 0);
      cairo_paint_with_alpha(cr, opacity);
      cairo_restore(cr);
    }
    cairo_restore(cr);
    if (g->border > 0) {
      cairo_save(cr);
      dg_round(cr, r, g->radius);
      cairo_set_source_rgba(cr, 1, 1, 1, .3);
      cairo_set_line_width(cr, g->border);
      cairo_stroke(cr);
      cairo_restore(cr);
    }
    if (g->title_visible) {
      PangoLayout *p = dg_title_layout(v, g->title);
      pango_layout_set_width(p, MAX(1, r.width - 24) * PANGO_SCALE);
      pango_layout_set_ellipsize(p, PANGO_ELLIPSIZE_END);
      pango_layout_set_alignment(p, g->alignment == 1   ? PANGO_ALIGN_CENTER
                                    : g->alignment == 2 ? PANGO_ALIGN_RIGHT
                                                        : PANGO_ALIGN_LEFT);
      GtkStyleContext *ctx =
          gtk_widget_get_style_context(GTK_WIDGET(icon_view));
      gtk_style_context_save(ctx);
      gtk_style_context_add_class(ctx, GTK_STYLE_CLASS_CELL);
      gtk_style_context_add_class(ctx, GTK_STYLE_CLASS_LABEL);
      if (host->view_state(icon_view)->label_fg_color_set) {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(
            attrs,
            pango_attr_foreground_new(
                host->view_state(icon_view)->label_fg_color.red * 65535,
                host->view_state(icon_view)->label_fg_color.green * 65535,
                host->view_state(icon_view)->label_fg_color.blue * 65535));
        pango_layout_set_attributes(p, attrs);
        pango_attr_list_unref(attrs);
      }
      double title_x = r.x + 12;
      double title_y = g->title_bottom ? r.y + r.height - dg_title_height(v) + 5 : r.y + 5;
      if (g->text_shadow > 0) {
        GdkRGBA shadow = {0, 0, 0, 1};
        gdk_rgba_parse(&shadow, g->text_shadow_color);
        shadow.alpha *= CLAMP(g->text_shadow, 0, 1);
        PangoAttrList *saved_attrs = pango_layout_get_attributes(p);
        pango_layout_set_attributes(p, NULL);
        gdk_cairo_set_source_rgba(cr, &shadow);
        for (int sy = -1; sy <= 1; sy++) for (int sx = -1; sx <= 1; sx++) if (sx || sy) {
          cairo_move_to(cr, title_x + sx, title_y + sy);
          pango_cairo_show_layout(cr, p);
        }
        pango_layout_set_attributes(p, saved_attrs);
      }
      gtk_render_layout(ctx, cr, title_x, title_y, p);
      gtk_style_context_restore(ctx);
      g_object_unref(p);
    }
    int live, placed;
    dg_member_counts(v, g, &live, &placed);
    if (live > placed) {
      int r0, c0, r1, c1;
      dg_grid_bounds(v, g, &r0, &c0, &r1, &c1);
      int cap = MAX(0, r1 - r0 + 1) * MAX(0, c1 - c0 + 1);
      int pages = cap > 0 ? (live + cap - 1) / cap : 0;
      int page = GPOINTER_TO_INT(g_hash_table_lookup(v->pages, g->id));
      page = pages > 0 ? CLAMP(page, 0, pages - 1) : 0;
      char *text = pages > 1 ? g_strdup_printf("%d/%d  •  Scroll", page + 1,
                                               pages)
                             : g_strdup("Resize to show icons");
        PangoLayout *p =
            gtk_widget_create_pango_layout(GTK_WIDGET(icon_view), text);
        GtkStyleContext *ctx =
            gtk_widget_get_style_context(GTK_WIDGET(icon_view));
        gtk_style_context_save(ctx);
        gtk_style_context_add_class(ctx, GTK_STYLE_CLASS_CELL);
        gtk_style_context_add_class(ctx, GTK_STYLE_CLASS_LABEL);
        gtk_render_layout(
            ctx, cr, r.x + 8,
            r.y + r.height -
                (g->title_visible && g->title_bottom ? dg_title_height(v) : 0) -
                24,
            p);
        gtk_style_context_restore(ctx);
        g_object_unref(p);
        g_free(text);
      }
    if (!g->locked) {
      cairo_save(cr);
      cairo_set_source_rgba(cr, 1, 1, 1, .45);
      cairo_move_to(cr, r.x + r.width - 12, r.y + r.height - 4);
      cairo_line_to(cr, r.x + r.width - 4, r.y + r.height - 12);
      cairo_stroke(cr);
      cairo_restore(cr);
    }
  }
}
static gboolean dg_scroll(GtkWidget *widget, GdkEventScroll *event,
                          gpointer data) {
  DgView *v = data;
  DgGroup *g = dg_at(v, event->x, event->y);
  if (!g)
    return FALSE;
  double dx = 0, dy = 0;
  int delta = 0;
  if (event->direction == GDK_SCROLL_DOWN ||
      event->direction == GDK_SCROLL_RIGHT)
    delta = 1;
  else if (event->direction == GDK_SCROLL_UP ||
           event->direction == GDK_SCROLL_LEFT)
    delta = -1;
  else if (gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy) &&
           fabs(dy) > .2)
    delta = dy > 0 ? 1 : -1;
  int page = GPOINTER_TO_INT(g_hash_table_lookup(v->pages, g->id));
  g_hash_table_replace(v->pages, g_strdup(g->id),
                       GINT_TO_POINTER(MAX(0, page + delta)));
  dg_queue(v);
  return TRUE;
}
static void dg_free(gpointer data) {
  DgView *v = data;
  if (!v->retired) {
    xt_surface_unregister(GTK_WIDGET(v->view), "desktop-groups");
    dg_end_drag(v);
    xt_icon_opacity_clear(GTK_WIDGET(v->view), "desktop-groups");
    v->retired = TRUE;
    if (v->idle) {
      g_source_remove(v->idle);
      v->idle = 0;
    }
    if (v->tick) {
      gtk_widget_remove_tick_callback(v->view, v->tick);
      v->tick = 0;
    }
    dg_views = g_list_remove(dg_views, v);
    GList *widgets = g_list_copy(v->owned_widgets);
    for (GList *l = widgets; l; l = l->next) {
      if (GTK_IS_DIALOG(l->data))
        gtk_dialog_response(GTK_DIALOG(l->data), GTK_RESPONSE_CANCEL);
      else
        gtk_widget_destroy(l->data);
    }
    g_list_free(widgets);
  }
  if (v->dialog_depth)
    return;
  g_hash_table_unref(v->pages);
  g_hash_table_unref(v->fades);
  g_hash_table_unref(v->drag_offsets);
  g_hash_table_unref(v->images);
  g_free(v->monitor);
  g_free(v);
  if (!dg_views) {
    dg_store_free(dg_store);
    dg_store = NULL;
  }
}
static gboolean dg_drag_motion_cb(GtkWidget *, GdkDragContext *, int, int,
                                  guint, gpointer);
static gboolean dg_drag_drop_cb(GtkWidget *, GdkDragContext *, int, int, guint,
                                gpointer);
static void dg_drag_received_cb(GtkWidget *, GdkDragContext *, int, int,
                                GtkSelectionData *, guint, guint, gpointer);
static gboolean dg_surface(GtkWidget *view, double x, double y, gpointer data) { return dg_at(data, x, y) != NULL; }
void xfdesktop_icon_view_groups_enable(XfdesktopIconView *view,
                                       const char *monitor) {
#ifdef ENABLE_X11
  if (!GDK_IS_X11_DISPLAY(gtk_widget_get_display(GTK_WIDGET(view))))
    return;
#else
  return;
#endif
  if (dg_state(view))
    return;
  if (!dg_store) {
    char *path = g_build_filename(g_get_user_config_dir(), "xfce4",
                                  "desktop-groups.ini", NULL);
    GError *e = NULL;
    dg_store = dg_store_load(path, &e);
    g_free(path);
    if (e) {
      g_warning("Desktop Groups: %s (original configuration preserved)",
                e->message);
      g_clear_error(&e);
    }
  }
  DgView *v = g_new0(DgView, 1);
  v->view = view;
  v->monitor = g_strdup(monitor);
  v->last_count = -1;
  v->pages = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  v->fades = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  v->drag_offsets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  v->images =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
  gtk_widget_add_events(GTK_WIDGET(view),
                        GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
  g_signal_connect(view, "drag-motion", G_CALLBACK(dg_drag_motion_cb), v);
  g_signal_connect(view, "drag-drop", G_CALLBACK(dg_drag_drop_cb), v);
  g_signal_connect(view, "drag-data-received", G_CALLBACK(dg_drag_received_cb),
                   v);
  g_signal_connect(view, "scroll-event", G_CALLBACK(dg_scroll), v);
  dg_views = g_list_prepend(dg_views, v);
  g_object_set_data_full(G_OBJECT(view), "desktop-groups", v, dg_free);
  xt_surface_register(GTK_WIDGET(view), "desktop-groups", dg_surface, v);
}

typedef struct {
  DgView *v;
  DgGroup *g;
  GtkWidget *title, *image, *color, *shadow_color, *opacity, *enter, *exit, *width, *height,
      *radius, *border;
  GtkWidget *text_shadow;
  GtkWidget *visible, *bottom, *locked, *size, *icons, *alignment;
} DgEditor;
static void dg_editor_changed(GtkWidget *widget, gpointer data) {
  DgEditor *e = data;
  DgGroup *g = e->g;
  GdkRGBA color;
  g_free(g->title);
  g->title = g_strdup(gtk_entry_get_text(GTK_ENTRY(e->title)));
  g_free(g->image);
  g->image = g_strdup(gtk_entry_get_text(GTK_ENTRY(e->image)));
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(e->color), &color);
  g_free(g->color);
  g->color = gdk_rgba_to_string(&color);
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(e->shadow_color), &color);
  g_free(g->text_shadow_color);
  g->text_shadow_color = gdk_rgba_to_string(&color);
#define VAL(f, w) g->f = gtk_spin_button_get_value(GTK_SPIN_BUTTON(e->w))
  VAL(opacity, opacity);
  VAL(hover_enter, enter);
  VAL(hover_exit, exit);
  VAL(width, width);
  VAL(height, height);
  VAL(radius, radius);
  VAL(border, border);
  VAL(text_shadow, text_shadow);
#undef VAL
#define CHECK(f, w) g->f = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(e->w))
  CHECK(title_visible, visible);
  CHECK(title_bottom, bottom);
  CHECK(locked, locked);
  CHECK(auto_size, size);
  CHECK(auto_icons, icons);
#undef CHECK
  g->alignment = gtk_combo_box_get_active(GTK_COMBO_BOX(e->alignment));
  g_hash_table_remove_all(e->v->images);
  g_hash_table_remove_all(e->v->fades);
  dg_refresh();
}
static void dg_editor_restore_defaults(GtkButton *button, gpointer data) {
  DgEditor *e = data;
  GdkRGBA color;
  gtk_entry_set_text(GTK_ENTRY(e->image), "");
  gdk_rgba_parse(&color, "#202630");
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(e->color), &color);
  gdk_rgba_parse(&color, "#000000");
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(e->shadow_color), &color);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->opacity), .45);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->enter), .7);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->exit), .45);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->width), 360);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->height), 260);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->radius), 12);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->border), 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->text_shadow), .35);
  gtk_combo_box_set_active(GTK_COMBO_BOX(e->alignment), 1);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(e->visible), TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(e->bottom), FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(e->size), TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(e->icons), TRUE);
  (void)button;
}
static GtkWidget *dg_editor_row(GtkWidget *grid, const char *label,
                                GtkWidget *control, int row) {
  GtkWidget *l = gtk_label_new(label);
  gtk_widget_set_halign(l, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), control, 1, row, 1, 1);
  return control;
}
static GtkWidget *dg_spin(double value, double low, double high, double step) {
  GtkWidget *s = gtk_spin_button_new_with_range(low, high, step);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), value);
  return s;
}
static GtkWidget *dg_check(const char *text, gboolean value) {
  GtkWidget *w = gtk_check_button_new_with_label(text);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), value);
  return w;
}
static void dg_browse_image(GtkButton *button, gpointer data) {
  DgEditor *e = data;
  GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
  GtkWidget *chooser = gtk_file_chooser_dialog_new(
      "Choose Group Background", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(chooser), TRUE);
  gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(chooser), TRUE);
  GtkFileFilter *images = gtk_file_filter_new();
  gtk_file_filter_set_name(images, "Images");
  gtk_file_filter_add_pixbuf_formats(images);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), images);
  GtkFileFilter *all = gtk_file_filter_new();
  gtk_file_filter_set_name(all, "All files");
  gtk_file_filter_add_pattern(all, "*");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), all);
  const char *current = gtk_entry_get_text(GTK_ENTRY(e->image));
  if (*current)
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), current);
  if (dg_run(e->v, chooser) == GTK_RESPONSE_ACCEPT) {
    char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    if (filename) {
      gtk_entry_set_text(GTK_ENTRY(e->image), filename);
      g_free(filename);
    }
  }
  gtk_widget_destroy(chooser);
}
static void dg_customize(DgView *v, DgGroup *g) {
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      g == dg_store->defaults ? "Desktop Groups — New Group Defaults"
                              : "Customize Desktop Group",
      GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(v->view))),
      GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, "_Close",
      GTK_RESPONSE_CLOSE, NULL);
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
  gtk_container_set_border_width(GTK_CONTAINER(grid), 16);
  GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_container_add(GTK_CONTAINER(scroller), grid);
  gtk_container_add(
      GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroller);
  GdkRectangle workarea = {0, 0, 800, 800};
  GdkMonitor *monitor = gdk_display_get_monitor_at_window(
      gtk_widget_get_display(GTK_WIDGET(v->view)),
      gtk_widget_get_window(GTK_WIDGET(v->view)));
  if (monitor)
    gdk_monitor_get_workarea(monitor, &workarea);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 460,
                              MIN(720, MAX(300, workarea.height - 100)));
  DgEditor e = {.v = v, .g = g};
  int row = 0;
  e.title = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(e.title), g->title);
  dg_editor_row(grid, "Title", e.title, row++);
  e.image = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(e.image), g->image);
  gtk_entry_set_placeholder_text(GTK_ENTRY(e.image),
                                 "Absolute image path (optional)");
  GtkWidget *image_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *browse = gtk_button_new_with_label("Browse…");
  gtk_box_pack_start(GTK_BOX(image_box), e.image, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(image_box), browse, FALSE, FALSE, 0);
  g_signal_connect(browse, "clicked", G_CALLBACK(dg_browse_image), &e);
  dg_editor_row(grid, "Background image", image_box, row++);
  GdkRGBA color;
  if (!gdk_rgba_parse(&color, g->color))
    gdk_rgba_parse(&color, "#202630");
  e.color = gtk_color_button_new_with_rgba(&color);
  dg_editor_row(grid, "Background color", e.color, row++);
  if (!gdk_rgba_parse(&color, g->text_shadow_color)) gdk_rgba_parse(&color, "#000000");
  e.shadow_color = gtk_color_button_new_with_rgba(&color);
  gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(e.shadow_color), TRUE);
  dg_editor_row(grid, "Title shadow color", e.shadow_color, row++);
#define SPIN(w, label, field, low, high, step)                                 \
  e.w = dg_editor_row(grid, label, dg_spin(g->field, low, high, step), row++)
  SPIN(opacity, "Group opacity", opacity, 0, 1, .05);
  SPIN(enter, "Hover enter opacity", hover_enter, 0, 1, .05);
  SPIN(exit, "Hover exit opacity", hover_exit, 0, 1, .05);
  SPIN(width, "Width (logical pixels)", width, 96, 32768, 8);
  SPIN(height, "Height (logical pixels)", height, 64, 32768, 8);
  SPIN(radius, "Corner radius", radius, 0, 100, 1);
  SPIN(border, "Border width", border, 0, 20, 1);
  SPIN(text_shadow, "Title shadow strength", text_shadow, 0, 1, .05);
#undef SPIN
  e.alignment = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(e.alignment), "Left");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(e.alignment), "Center");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(e.alignment), "Right");
  gtk_combo_box_set_active(GTK_COMBO_BOX(e.alignment), g->alignment);
  dg_editor_row(grid, "Title alignment", e.alignment, row++);
#define CHECK(w, label, field)                                                 \
  e.w = dg_check(label, g->field);                                             \
  gtk_grid_attach(GTK_GRID(grid), e.w, 0, row++, 2, 1)
  CHECK(visible, "Show title", title_visible);
  CHECK(bottom, "Title at bottom", title_bottom);
  CHECK(locked, "Lock position", locked);
  CHECK(size, "Size automatically to contents", auto_size);
  CHECK(icons, "Arrange icons automatically", auto_icons);
#undef CHECK
  GtkWidget *changes[] = {e.title, e.image, e.alignment};
  for (guint i = 0; i < G_N_ELEMENTS(changes); i++)
    g_signal_connect(changes[i], "changed", G_CALLBACK(dg_editor_changed), &e);
  GtkWidget *spins[] = {e.opacity, e.enter,  e.exit,  e.width,
                        e.height,  e.radius, e.border, e.text_shadow};
  for (guint i = 0; i < G_N_ELEMENTS(spins); i++)
    g_signal_connect(spins[i], "value-changed", G_CALLBACK(dg_editor_changed),
                     &e);
  GtkWidget *checks[] = {e.visible, e.bottom, e.locked, e.size, e.icons};
  for (guint i = 0; i < G_N_ELEMENTS(checks); i++)
    g_signal_connect(checks[i], "toggled", G_CALLBACK(dg_editor_changed), &e);
  g_signal_connect(e.color, "color-set", G_CALLBACK(dg_editor_changed), &e);
  g_signal_connect(e.shadow_color, "color-set", G_CALLBACK(dg_editor_changed), &e);
  GtkWidget *restore = gtk_button_new_with_label("Restore default appearance");
  gtk_grid_attach(GTK_GRID(grid), restore, 0, row++, 2, 1);
  g_signal_connect(restore, "clicked", G_CALLBACK(dg_editor_restore_defaults), &e);
  gtk_widget_show_all(dialog);
  dg_run(v, dialog);
  dg_save();
  gtk_widget_destroy(dialog);
}
typedef struct {
  DgView *v;
  DgGroup *g;
} DgName;
static void dg_title_changed(GtkEntry *entry, gpointer data) {
  DgName *n = data;
  g_free(n->g->title);
  n->g->title = g_strdup(gtk_entry_get_text(entry));
  dg_queue(n->v);
  gtk_widget_queue_draw(GTK_WIDGET(n->v->view));
}
static void dg_title_closed(GtkWidget *popover, gpointer data) {
  dg_save();
  gtk_widget_destroy(popover);
}
static void dg_title_activate(GtkEntry *entry, gpointer popover) {
  gtk_popover_popdown(GTK_POPOVER(popover));
}
static void dg_name(DgView *v, DgGroup *g) {
  GtkWidget *popover = gtk_popover_new(GTK_WIDGET(v->view));
  dg_own(v, popover);
  GdkRectangle r = dg_rect(v, g);
  r.height = dg_title_height(v);
  gtk_popover_set_pointing_to(GTK_POPOVER(popover), &r);
  GtkWidget *entry = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(entry), g->title);
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Untitled group");
  gtk_container_add(GTK_CONTAINER(popover), entry);
  DgName *n = g_new(DgName, 1);
  n->v = v;
  n->g = g;
  g_object_set_data_full(G_OBJECT(popover), "name", n, g_free);
  g_signal_connect(entry, "changed", G_CALLBACK(dg_title_changed), n);
  g_signal_connect(entry, "activate", G_CALLBACK(dg_title_activate), popover);
  g_signal_connect(popover, "closed", G_CALLBACK(dg_title_closed), NULL);
  gtk_widget_show_all(popover);
  gtk_popover_popup(GTK_POPOVER(popover));
  gtk_widget_grab_focus(entry);
}
typedef struct {
  DgView *v;
  char *group_id, *dest_monitor;
  int action, x, y;
} DgAction;
enum {
  DG_CREATE,
  DG_SELECTION,
  DG_RENAME,
  DG_CUSTOMIZE,
  DG_LOCK,
  DG_ICONS,
  DG_RESIZE,
  DG_DELETE,
  DG_DEFAULTS,
  DG_AUTO,
  DG_QUICK,
  DG_TRANSFER
};
static ViewItem *dg_find(DgView *v, const char *id) {
  for (GList *l = host->view_state(v->view)->items; l; l = l->next)
    if (!g_strcmp0(id, dg_item_id(v->view, l->data)))
      return l->data;
  return NULL;
}
static void dg_transfer(DgView *source, DgGroup *g, const char *monitor) {
  DgView *dest = NULL;
  for (GList *l = dg_views; l; l = l->next) {
    DgView *v = l->data;
    if (!g_strcmp0(v->monitor, monitor))
      dest = v;
  }
  if (!dest || dest == source)
    return;
  /* Row removal/insertion can synchronously ask for group ownership. Publish
   * the destination before moving any opaque native icon handles. */
  g_free(g->monitor);g->monitor=g_strdup(monitor);
  g->x=CLAMP(g->x,0,MAX(0,gtk_widget_get_allocated_width(dest->view)-MIN(g->width,gtk_widget_get_allocated_width(dest->view))));
  g->y=CLAMP(g->y,0,MAX(0,gtk_widget_get_allocated_height(dest->view)-MIN(g->height,gtk_widget_get_allocated_height(dest->view))));
  GList *ids = g_hash_table_get_keys(g->members);
  for (GList *l = ids; l; l = l->next) {
    ViewItem *item = dg_find(source, l->data);
    if (item)host->transfer_icon(source->view, dest->view, item);
  }
  g_list_free(ids);
  dg_save();
  dg_refresh();
}
static void dg_action_free(gpointer data, GClosure *closure) {
  DgAction *a = data;
  g_free(a->group_id);
  g_free(a->dest_monitor);
  g_free(a);
}
static void dg_action(GtkMenuItem *mi, gpointer data) {
  DgAction *a = data;
  DgView *v = a->v;
  DgGroup *g = NULL;
  for (guint i = 0; i < dg_store->groups->len; i++) {
    DgGroup *candidate = g_ptr_array_index(dg_store->groups, i);
    if (!g_strcmp0(candidate->id, a->group_id))
      g = candidate;
  }
  if (a->action == DG_CREATE || a->action == DG_SELECTION) {
    g = dg_group_new(dg_store->defaults, v->monitor);
    g->x = a->x;
    g->y = a->y;
    if (a->action == DG_CREATE && v->band_valid) {
      g->x = v->band.x;
      g->y = v->band.y;
      g->width = MAX(96, v->band.width);
      g->height = MAX(64, v->band.height);
      g->auto_size = FALSE;
    }
    g_ptr_array_add(dg_store->groups, g);
    if (a->action == DG_SELECTION) {
      int minx = G_MAXINT, miny = G_MAXINT;
      for (GList *l = host->view_state(v->view)->selected_items; l;
           l = l->next) {
        ViewItem *item = l->data;
        GdkRectangle r;
        cairo_region_get_extents(host->icon_state(item)->icon_slot_region, &r);
        minx = MIN(minx, r.x);
        miny = MIN(miny, r.y);
        dg_store_assign(dg_store, g, dg_item_id(v->view, item), 0, 0);
      }
      if (minx != G_MAXINT) {
        g->x = MAX(0, minx - 12);
        g->y = MAX(0, miny - dg_title_height(v));
      }
    }
    v->band_valid = FALSE;
    dg_save();
    dg_refresh();
    dg_name(v, g);
    return;
  }
  if (a->action == DG_DEFAULTS) {
    dg_customize(v, dg_store->defaults);
    return;
  }
  if (a->action == DG_AUTO) {
    dg_store->auto_arrange = !dg_store->auto_arrange;
    dg_save();
    dg_refresh();
    return;
  }
  if (a->action == DG_QUICK) {
    dg_store->quick_create = !dg_store->quick_create;
    dg_save();
    return;
  }
  if (!g)
    return;
  switch (a->action) {
  case DG_RENAME:
    dg_name(v, g);
    return;
  case DG_CUSTOMIZE:
    dg_customize(v, g);
    return;
  case DG_LOCK:
    g->locked = !g->locked;
    break;
  case DG_ICONS:
    g->auto_icons = TRUE;
    break;
  case DG_RESIZE:
    g->auto_size = FALSE;
    dg_customize(v, g);
    return;
  case DG_DELETE:
    for (GList *l = dg_views; l; l = l->next) {
      DgView *other = l->data;
      if (other->hover == g)
        other->hover = NULL;
      if (other->drag == g)
        dg_end_drag(other);
    }
    dg_store_remove(dg_store, g);
    break;
  case DG_TRANSFER:
    dg_transfer(v, g, a->dest_monitor);
    return;
  default:
    break;
  }
  dg_save();
  dg_refresh();
}
static GtkWidget *dg_menu_item(DgView *v, GtkWidget *menu, DgGroup *g,
                               const char *label, int action, int x, int y) {
  GtkWidget *mi = gtk_menu_item_new_with_label(label);
  DgAction *a = g_new0(DgAction, 1);
  a->v = v;
  a->group_id = g ? g_strdup(g->id) : NULL;
  a->action = action;
  a->x = x;
  a->y = y;
  g_signal_connect_data(mi, "activate", G_CALLBACK(dg_action), a,
                        dg_action_free, 0);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
  gtk_widget_show(mi);
  return mi;
}
static void dg_populate_menu(XfdesktopIconView *view, GtkWidget *menu, int x,
                             int y) {
  DgView *v = dg_state(view);
  if (!v || !dg_store->writable)
    return;
  int n = g_list_length(host->view_state(view)->selected_items);
  DgGroup *g = dg_at(v, x, y);
  if (n)
    dg_menu_item(v, menu, NULL,
                 n == 1 ? "Make a group with this icon"
                        : "Make a group with these icons",
                 DG_SELECTION, x, y);
  if (g && !n) {
    dg_menu_item(v, menu, g, "Rename Group", DG_RENAME, x, y);
    dg_menu_item(v, menu, g, "Customize Group…", DG_CUSTOMIZE, x, y);
    dg_menu_item(v, menu, g,
                 g->locked ? "Unlock Group Position" : "Lock Group Position",
                 DG_LOCK, x, y);
    dg_menu_item(v, menu, g, "Auto Arrange Icons", DG_ICONS, x, y);
    dg_menu_item(v, menu, g, "Resize Group…", DG_RESIZE, x, y);
    for (GList *l = dg_views; l; l = l->next) {
      DgView *dest = l->data;
      if (dest == v)
        continue;
      char *label = g_strdup_printf("Move to display %s", dest->monitor);
      GtkWidget *mi = dg_menu_item(v, menu, g, label, DG_TRANSFER, x, y);
      g_free(label);
      /* The action closure owns a stable display identifier. */
      DgAction *a = g_new0(DgAction, 1);
      a->v = v;
      a->group_id = g_strdup(g->id);
      a->dest_monitor = g_strdup(dest->monitor);
      a->action = DG_TRANSFER;
      g_signal_handlers_disconnect_matched(mi, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                           G_CALLBACK(dg_action), NULL);
      g_signal_connect_data(mi, "activate", G_CALLBACK(dg_action), a,
                            dg_action_free, 0);
    }
    dg_menu_item(v, menu, g, "Ungroup Icons", DG_DELETE, x, y);
    dg_menu_item(v, menu, g, "Delete Group", DG_DELETE, x, y);
  } else if (!n)
    dg_menu_item(v, menu, NULL,
                 v->band_valid ? "Place group here" : "Create Desktop Group",
                 DG_CREATE, x, y);
  dg_menu_item(v, menu, NULL, "Desktop Groups Settings…", DG_DEFAULTS, x, y);
  dg_menu_item(v, menu, NULL,
               dg_store->auto_arrange ? "Use Freeform Group Positions"
                                      : "Automatically Arrange Groups",
               DG_AUTO, x, y);
  dg_menu_item(v, menu, NULL,
               dg_store->quick_create ? "Require Ctrl for Quick Group Creation"
                                      : "Enable Quick Group Creation",
               DG_QUICK, x, y);
}
void xfdesktop_icon_view_groups_menu(XfdesktopIconView *view, GtkWidget *menu,
                                     int x, int y) {
  DgView *v = dg_state(view);
  if (!v || !dg_store->writable)
    return;
  GtkWidget *submenu = gtk_menu_new();
  dg_populate_menu(view, submenu, x, y);
  GtkWidget *item = gtk_menu_item_new_with_label("Desktop Groups");
  dg_own(v, item);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  gtk_widget_show_all(item);
}
static void dg_popup(DgView *v, GdkEvent *event, int x, int y) {
  GtkWidget *menu = gtk_menu_new();
  dg_own(v, menu);
  dg_populate_menu(v->view, menu, x, y);
  g_signal_connect_swapped(menu, "selection-done",
                           G_CALLBACK(gtk_widget_destroy), menu);
  gtk_widget_show_all(menu);
  gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
}
static gboolean dg_press(XfdesktopIconView *view, GdkEventButton *event) {
  DgView *v = dg_state(view);
  if (!v)
    return FALSE;
  DgGroup *g = dg_at(v, event->x, event->y);
  if (!g)
    return FALSE;
  /* Icons keep native click, selection, launcher and file menus. */
  for (GList *l = host->view_state(view)->items; l; l = l->next) {
    ViewItem *item = l->data;
    if (host->icon_state(item)->placed &&
        cairo_region_contains_point(host->icon_state(item)->icon_slot_region,
                                    event->x, event->y))
      return FALSE;
  }
  if (event->button == 3)
    return FALSE; /* Native desktop menu appends group actions. */
  GdkRectangle r = dg_rect(v, g);
  int title = dg_title_height(v);
  gboolean on_title = g->title_bottom ? event->y > r.y + r.height - title
                                      : event->y < r.y + title;
  gboolean resize =
      event->x > r.x + r.width - 16 && event->y > r.y + r.height - 16;
  if (event->button == 1 && event->type == GDK_2BUTTON_PRESS && on_title) {
    dg_end_drag(v);
    dg_name(v, g);
    return TRUE;
  }
  if (event->button == 1 &&
      (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == 0 && !g->locked &&
      !dg_store->auto_arrange) {
    if (!resize)
      dg_flush_layouts();
    r = dg_rect(v, g);
    resize = event->x > r.x + r.width - 16 &&
             event->y > r.y + r.height - 16;
    v->drag = g;
    v->resizing = resize;
    v->start_x = r.x;
    v->start_y = r.y;
    v->start_w = r.width;
    v->start_h = r.height;
    v->pointer_x = event->x;
    v->pointer_y = event->y;
    v->pointer_root_x=event->x_root;v->pointer_root_y=event->y_root;
    if(!resize)dg_capture_drag_offsets(v,g);
    GdkSeat*seat=gdk_display_get_default_seat(gtk_widget_get_display(view));
    v->pointer_grab=gdk_seat_grab(seat,gtk_widget_get_window(view),GDK_SEAT_CAPABILITY_POINTER,FALSE,NULL,(GdkEvent*)event,NULL,NULL)==GDK_GRAB_SUCCESS;
    gtk_grab_add(GTK_WIDGET(view));
    return TRUE;
  }
  return FALSE;
}
static void dg_drag_to(DgView*v,double root_x,double root_y){
  DgGroup*g=v->drag;
  if(v->resizing){
    g->auto_size=FALSE;g->width=MAX(96,v->start_w+root_x-v->pointer_root_x);g->height=MAX(64,v->start_h+root_y-v->pointer_root_y);
  }else{
    DgView*from=dg_monitor_view(g->monitor),*dest=dg_view_at_root(root_x,root_y);
    if(!dest)dest=from?from:v;
    int ox,oy;gdk_window_get_origin(gtk_widget_get_window(dest->view),&ox,&oy);
    g->x=CLAMP(root_x-ox-(v->pointer_x-v->start_x),0,MAX(0,gtk_widget_get_allocated_width(dest->view)-MIN(g->width,gtk_widget_get_allocated_width(dest->view))));
    g->y=CLAMP(root_y-oy-(v->pointer_y-v->start_y),0,MAX(0,gtk_widget_get_allocated_height(dest->view)-MIN(g->height,gtk_widget_get_allocated_height(dest->view))));
    if(from && from!=dest){dg_hover(from->view,NULL);dg_transfer(from,g,dest->monitor);}
    else if(!from){if(dest!=v)dg_transfer(v,g,dest->monitor);else{g_free(g->monitor);g->monitor=g_strdup(dest->monitor);dg_save();}}
    dg_hover(dest->view,g);
    DgView *owner=dg_monitor_view(g->monitor);
    dg_sync_drag_icons(owner?owner:dest,g);
  }
  dg_refresh();
}
static gboolean dg_motion(XfdesktopIconView *view, GdkEventMotion *event) {
  DgView*v=dg_active_drag();
  if(!v){if(dg_state(view))dg_hover(view,dg_at(dg_state(view),event->x,event->y));return FALSE;}
  dg_drag_to(v,event->x_root,event->y_root);return TRUE;
}
static gboolean dg_release(XfdesktopIconView *view, GdkEventButton *event) {
  DgView *v = dg_state(view),*active=dg_active_drag();
  if(active && event->button==1){
    dg_drag_to(active,event->x_root,event->y_root);dg_end_drag(active);dg_save();dg_refresh();return TRUE;
  }
  if(!v)return FALSE;
  if (host->view_state(view)->definitely_rubber_banding && event->button == 1 &&
      host->view_state(view)->band_rect.width > 24 &&
      host->view_state(view)->band_rect.height > 24) {
    v->band = host->view_state(view)->band_rect;
    v->band_valid = TRUE;
  }
  return FALSE;
}
static void dg_band_menu(XfdesktopIconView *view, GdkEventButton *event,
                         gboolean was_band) {
  DgView *v = dg_state(view);
  if (v && was_band && v->band_valid &&
      (dg_store->quick_create || (event->state & GDK_CONTROL_MASK)))
    dg_popup(v, (GdkEvent *)event, event->x, event->y);
}
/* Called before native icon-moved signals: logical membership follows the drop,
 * while xfdesktop remains responsible for all actual file and icon operations.
 */
static void dg_dropped(XfdesktopIconView *view, XfdesktopIconView *source,
                       gpointer item, int x, int y) {
  DgView *v = dg_state(view), *sv = dg_state(source);
  if (!v || !sv)
    return;
  const char *id = dg_item_id(source, item);
  DgGroup *g = dg_at(v, x, y);
  int row = 0, col = 0;
  if (g) {
    DgGrid grid = dg_geometry(v, g);
    row = CLAMP((y - grid.y) / grid.pitch, 0, MAX(0, grid.rows - 1));
    col = CLAMP((x - grid.x) / grid.pitch, 0, MAX(0, grid.columns - 1));
  }
  dg_store_assign(dg_store, g, id, row, col);
  dg_save();
  dg_refresh();
}

static void dg_leave(XfdesktopIconView *view) { dg_hover(view, NULL); }
/* Only empty group surfaces intercept native icon drags. Drops onto a folder or
 * launcher retain xfdesktop's original file-operation behavior. */
static gboolean dg_drop_surface(DgView *v, int x, int y) {
  if (!dg_at(v, x, y))
    return FALSE;
  for (GList *l = host->view_state(v->view)->items; l; l = l->next) {
    ViewItem *item = l->data;
    if (host->icon_state(item)->placed &&
        cairo_region_contains_point(host->icon_state(item)->icon_slot_region, x,
                                    y))
      return FALSE;
  }
  return TRUE;
}
static gboolean dg_drag_motion_cb(GtkWidget *widget, GdkDragContext *context,
                                  int x, int y, guint time, gpointer data) {
  DgView *v = data;
  if (!host->is_icon_drag(v->view, context) || !dg_drop_surface(v, x, y))
    return FALSE;
  gdk_drag_status(context, GDK_ACTION_MOVE, time);
  return TRUE;
}
static gboolean dg_drag_drop_cb(GtkWidget *widget, GdkDragContext *context,
                                int x, int y, guint time, gpointer data) {
  DgView *v = data;
  if (!host->is_icon_drag(v->view, context) || !dg_drop_surface(v, x, y))
    return FALSE;
  v->drag_pending = TRUE;
  host->set_drag_pending(v->view, TRUE);
  gtk_drag_get_data(widget, context,
                    gdk_atom_intern_static_string("XFDESKTOP_ICON"), time);
  return TRUE;
}
static void dg_drag_received_cb(GtkWidget *widget, GdkDragContext *context,
                                int x, int y, GtkSelectionData *data,
                                guint info, guint time, gpointer user_data) {
  DgView *v = user_data;
  DgGroup *g = dg_at(v, x, y);
  if (!v->drag_pending || !g || !dg_drop_surface(v, x, y))
    return;
  GPtrArray *icons = host->decode_icon_drop(data, info);
  if (!icons)
    return;
  for (guint i = 0; i < icons->len; i++) {
    XtDrop *drop = g_ptr_array_index(icons, i);
    const char *id = dg_item_id(drop->source, drop->item);
    DgGrid grid = dg_geometry(v, g);
    dg_store_assign(
        dg_store, g, id,
        CLAMP((y - grid.y) / grid.pitch, 0, MAX(0, grid.rows - 1)),
        CLAMP((x - grid.x) / grid.pitch, 0, MAX(0, grid.columns - 1)));
    if (drop->source != v->view)
      host->transfer_icon(drop->source, v->view, drop->item);
  }
  g_ptr_array_unref(icons);
  v->drag_pending = FALSE;
  host->set_drag_pending(v->view, FALSE);
  dg_save();
  dg_refresh();
  gtk_drag_finish(context, TRUE, FALSE, time);
  g_signal_stop_emission_by_name(widget, "drag-data-received");
}

void xfdesktop_icon_view_groups_file_renamed(GFile *old_file, GFile *new_file) {
  if (!dg_store)
    return;
  char *old_id = g_file_get_path(old_file), *new_id = g_file_get_path(new_file);
  if (!old_id)
    old_id = g_file_get_uri(old_file);
  if (!new_id)
    new_id = g_file_get_uri(new_file);
  if (dg_store_owner(dg_store, old_id)) {
    dg_store_rename(dg_store, old_id, new_id);
    dg_save();
    dg_refresh();
  }
  g_free(old_id);
  g_free(new_id);
}

static void dg_detach(GtkWidget *view) {
  DgView *v = dg_state(view);
  if (!v)
    return;
  if (gtk_widget_has_grab(view))
    gtk_grab_remove(view);
  g_signal_handlers_disconnect_by_data(view, v);
  g_object_set_data(G_OBJECT(view), "desktop-groups", NULL);
}
static const XtTweak tweak = {.abi_version = XT_ABI_VERSION,
                              .struct_size = sizeof(XtTweak),
                              .id = "desktop-groups",
                              .name = "Desktop Groups",
                              .capabilities = XT_CAP_ICON_LAYOUT,
                              .attach_view = xfdesktop_icon_view_groups_enable,
                              .detach_view = dg_detach,
                              .draw = dg_draw,
                              .coords_to_slot = dg_coords_to_slot,
                              .button_press = dg_press,
                              .button_release = dg_release,
                              .motion = dg_motion,
                              .leave = dg_leave,
                              .band_complete = dg_band_menu,
                              .icon_dropped = dg_dropped,
                              .view_menu = xfdesktop_icon_view_groups_menu,
                              .file_renamed =
                                  xfdesktop_icon_view_groups_file_renamed};
G_MODULE_EXPORT const XtTweak *xfce_tweak_query(const XtHost *api) {
  if (!api || api->abi_version != XT_ABI_VERSION ||
      api->struct_size < sizeof(XtHost))
    return NULL;
  host = api;
  return &tweak;
}
