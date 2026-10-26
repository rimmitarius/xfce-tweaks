/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "xfce-tweaks-host.h"
#include "xfce-desktop.h"
#include <string.h>
#ifndef XT_MODULE_DIR
#define XT_MODULE_DIR "/usr/local/lib/xfce-tweaks/modules"
#endif

typedef struct {
  char *id, *library;
  const XtTweak *api;
  GModule *module;
  gboolean enabled, default_enabled;
} Entry;
static GPtrArray *entries;
static GList *views, *desktops;
static XfconfChannel *channel;
static void update(Entry *e, gboolean enabled);
static void scan(const char *root);
static void status(Entry *e, const char *text) {
  char *p = g_strdup_printf("/xfce-tweaks/status/%s", e->id);
  xfconf_channel_set_string(channel, p, text);
  g_free(p);
}
static void changed(XfconfChannel *c, const char *property, const GValue *value,
                    gpointer data) {
  if (!strcmp(property, "/xfce-tweaks/rescan")) {
    char *user =
        g_build_filename(g_get_user_data_dir(), "xfce-tweaks", "modules", NULL);
    scan(user);
    g_free(user);
    for (guint i = 0; i < entries->len; i++) {
      Entry *e = g_ptr_array_index(entries, i);
      char *p = g_strdup_printf("/xfce-tweaks/enabled/%s", e->id);
      update(e, xfconf_channel_get_bool(c, p, e->default_enabled));
      g_free(p);
    }
    return;
  }
  if (!g_str_has_prefix(property, "/xfce-tweaks/enabled/"))
    return;
  const char *id = property + strlen("/xfce-tweaks/enabled/");
  for (guint i = 0; i < entries->len; i++) {
    Entry *e = g_ptr_array_index(entries, i);
    if (!strcmp(e->id, id))
      update(e, xfconf_channel_get_bool(c, property, e->default_enabled));
  }
}
static void scan(const char *root) {
  GDir *dir = g_dir_open(root, 0, NULL);
  if (!dir)
    return;
  const char *name;
  while ((name = g_dir_read_name(dir))) {
    if (name[0] == '.')
      continue;
    char *path = g_build_filename(root, name, "tweak.ini", NULL);
    GKeyFile *k = g_key_file_new();
    if (g_key_file_load_from_file(k, path, 0, NULL)) {
      char *id = g_key_file_get_string(k, "Tweak", "Id", NULL);
      char *lib = g_key_file_get_string(k, "Tweak", "Library", NULL);
      int abi = g_key_file_get_integer(k, "Tweak", "ABI", NULL);
      gboolean duplicate = FALSE;
      for (guint i = 0; i < entries->len; i++)
        if (!g_strcmp0(((Entry *)g_ptr_array_index(entries, i))->id, id))
          duplicate = TRUE;
      if (id && lib && abi == XT_ABI_VERSION &&
          g_regex_match_simple("^[a-z][a-z0-9-]{0,63}$", id, 0, 0) &&
          !strchr(lib, '/') && g_str_has_suffix(lib, ".so") && !duplicate) {
        Entry *e = g_new0(Entry, 1);
        e->id = g_strdup(id);
        e->library = g_build_filename(root, name, lib, NULL);
        e->default_enabled =
            g_key_file_get_boolean(k, "Tweak", "DefaultEnabled", NULL);
        g_ptr_array_add(entries, e);
      }
      g_free(id);
      g_free(lib);
    }
    g_key_file_unref(k);
    g_free(path);
  }
  g_dir_close(dir);
}
static void init(void) {
  if (entries)
    return;
  entries = g_ptr_array_new();
  channel = xfconf_channel_get("xfce4-desktop");
  /* Presence is scoped to this session bus, never inferred from stale settings. */
  g_bus_own_name(G_BUS_TYPE_SESSION, "org.xfce.Tweaks", G_BUS_NAME_OWNER_FLAGS_NONE,
                 NULL, NULL, NULL, NULL, NULL);
  char *user =
      g_build_filename(g_get_user_data_dir(), "xfce-tweaks", "modules", NULL);
  scan(user);
  g_free(user);
  const char *override = g_getenv("XFCE_TWEAKS_MODULE_PATH");
  if (override) {
    char **dirs = g_strsplit(override, G_SEARCHPATH_SEPARATOR_S, -1);
    for (guint i = 0; dirs[i]; i++)
      scan(dirs[i]);
    g_strfreev(dirs);
  }
  scan(XT_MODULE_DIR);
  g_signal_connect(channel, "property-changed", G_CALLBACK(changed), NULL);
  for (guint i = 0; i < entries->len; i++) {
    Entry *e = g_ptr_array_index(entries, i);
    char *p = g_strdup_printf("/xfce-tweaks/enabled/%s", e->id);
    update(e, xfconf_channel_get_bool(channel, p, e->default_enabled));
    g_free(p);
  }
}
static gboolean load(Entry *e) {
  if (e->api)
    return TRUE;
  e->module = g_module_open(e->library, G_MODULE_BIND_LOCAL);
  if (!e->module) {
    g_warning("Tweak %s: %s", e->id, g_module_error());
    status(e, "load-error");
    return FALSE;
  }
  XtQuery query = NULL;
  if (!g_module_symbol(e->module, "xfce_tweak_query", (gpointer *)&query))
    goto bad;
  const XtTweak *api = query(xt_icon_host_api());
  if (!api || api->abi_version != XT_ABI_VERSION ||
      api->struct_size < sizeof(XtTweak) || g_strcmp0(api->id, e->id))
    goto bad;
  e->api = api;
  g_module_make_resident(e->module);
  return TRUE;
bad:
  g_warning("Tweak %s has an incompatible ABI or identity", e->id);
  status(e, "incompatible");
  g_module_close(e->module);
  e->module = NULL;
  return FALSE;
}
static void update(Entry *e, gboolean enabled) {
  if (e->enabled == enabled) {
    status(e, enabled ? "active" : "disabled");
    return;
  }
  if (enabled && !load(e))
    return;
  if (enabled)
    for (guint i = 0; i < entries->len; i++) {
      Entry *other = g_ptr_array_index(entries, i);
      if (other != e && other->enabled &&
          (other->api->capabilities & e->api->capabilities &
           (XT_CAP_ICON_LAYOUT | XT_CAP_BACKDROP_PROVIDER))) {
        char *message = g_strdup_printf("conflict: %s", other->id);
        status(e, message);
        g_free(message);
        return;
      }
    }
  e->enabled = enabled;
  for (GList *l = views; l; l = l->next) {
    GtkWidget *v = l->data;
    if (enabled) {
      if (e->api->attach_view)
        e->api->attach_view(v, g_object_get_data(G_OBJECT(v), "xt-monitor"));
    } else {
      if (e->api->detach_view)
        e->api->detach_view(v);
      if (e->api->capabilities & XT_CAP_ICON_LAYOUT)
        xt_reset_icon_layout(v);
    }
    gtk_widget_queue_draw(v);
  }
  for (GList *l = desktops; l; l = l->next) {
    GtkWidget *d = l->data;
    if (enabled) {
      if (e->api->attach_desktop)
        e->api->attach_desktop(d);
    } else if (e->api->detach_desktop)
      e->api->detach_desktop(d);
    if (gtk_widget_get_realized(d))
      xfce_desktop_refresh(XFCE_DESKTOP(d));
  }
  status(e, enabled ? "active" : "disabled");
}
static void view_gone(gpointer data, GObject *object) {
  views = g_list_remove(views, object);
}
static void desktop_gone(gpointer data, GObject *object) {
  desktops = g_list_remove(desktops, object);
}
static void desktop_destroyed(GtkWidget *d, gpointer data) {
  for (guint i = 0; i < entries->len; i++) {
    Entry *e = g_ptr_array_index(entries, i);
    if (e->enabled && e->api->detach_desktop)
      e->api->detach_desktop(d);
  }
  desktops = g_list_remove(desktops, d);
}
void xt_attach_view(GtkWidget *v, const char *monitor) {
  init();
  if (g_list_find(views, v))
    return;
  views = g_list_prepend(views, v);
  g_object_weak_ref(G_OBJECT(v), view_gone, NULL);
  g_object_set_data_full(G_OBJECT(v), "xt-monitor", g_strdup(monitor), g_free);
  for (guint i = 0; i < entries->len; i++) {
    Entry *e = g_ptr_array_index(entries, i);
    if (e->enabled && e->api->attach_view)
      e->api->attach_view(v, monitor);
  }
}
void xt_attach_desktop(GtkWidget *d) {
  init();
  if (g_list_find(desktops, d))
    return;
  g_signal_connect(d, "destroy", G_CALLBACK(desktop_destroyed), NULL);
  desktops = g_list_prepend(desktops, d);
  g_object_weak_ref(G_OBJECT(d), desktop_gone, NULL);
  for (guint i = 0; i < entries->len; i++) {
    Entry *e = g_ptr_array_index(entries, i);
    if (e->enabled && e->api->attach_desktop)
      e->api->attach_desktop(d);
  }
}
#define EACH(member, call)                                                     \
  do {                                                                         \
    init();                                                                    \
    for (guint n = 0; n < entries->len; n++) {                                 \
      Entry *e = g_ptr_array_index(entries, n);                                \
      if (e->enabled && e->api->member)                                        \
        e->api->member call;                                                   \
    }                                                                          \
  } while (0)
#define FIRST(member, call)                                                    \
  do {                                                                         \
    init();                                                                    \
    for (guint n = 0; n < entries->len; n++) {                                 \
      Entry *e = g_ptr_array_index(entries, n);                                \
      if (e->enabled && e->api->member && e->api->member call)                 \
        return TRUE;                                                           \
    }                                                                          \
    return FALSE;                                                              \
  } while (0)
void xt_view_menu(GtkWidget *v, GtkWidget *m, int x, int y) {
  EACH(view_menu, (v, m, x, y));
}
void xt_file_renamed(GFile *a, GFile *b) { EACH(file_renamed, (a, b)); }
void xt_draw(GtkWidget *v, cairo_t *c) { EACH(draw, (v, c)); }
gboolean xt_coords_to_slot(GtkWidget *v, int x, int y, int *r, int *c) {
  FIRST(coords_to_slot, (v, x, y, r, c));
}
gboolean xt_press(GtkWidget *v, GdkEventButton *b) {
  FIRST(button_press, (v, b));
}
gboolean xt_release(GtkWidget *v, GdkEventButton *b) {
  FIRST(button_release, (v, b));
}
gboolean xt_motion(GtkWidget *v, GdkEventMotion *b) { FIRST(motion, (v, b)); }
void xt_leave(GtkWidget *v) { EACH(leave, (v)); }
void xt_band_menu(GtkWidget *v, GdkEventButton *b, gboolean w) {
  EACH(band_complete, (v, b, w));
}
void xt_dropped(GtkWidget *v, GtkWidget *s, gpointer i, int x, int y) {
  EACH(icon_dropped, (v, s, i, x, y));
}
gboolean xt_backdrop_changed(GtkWidget *d, const char *f, const char *p) {
  FIRST(backdrop_changed, (d, f, p));
}
static void open_manager(GtkMenuItem *item, gpointer data) {
  char *argv[] = {XT_MANAGER_PATH, NULL};
  GError *error = NULL;
  if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
                     &error)) {
    g_warning("Cannot launch XFCE Tweaks: %s", error->message);
    g_clear_error(&error);
  }
}
void xt_desktop_menu(GtkWidget *d, GtkWidget *m) {
  EACH(desktop_menu, (d, m));
  GtkWidget *item = gtk_menu_item_new_with_label("XFCE Tweaks…");
  g_signal_connect(item, "activate", G_CALLBACK(open_manager), NULL);
  gtk_menu_shell_insert(GTK_MENU_SHELL(m), item, 1);
  gtk_widget_show(item);
}
void xt_host_set_backdrop(GtkWidget *d, GtkWidget *w) {
  xfce_desktop_put_to_layer(XFCE_DESKTOP(d), XFCE_DESKTOP_LAYER_BACKDROP, w);
}
