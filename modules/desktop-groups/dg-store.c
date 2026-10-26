/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dg-store.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <math.h>

DgGroup *dg_group_new(const DgGroup *d, const char *monitor) {
    DgGroup *g = g_new0(DgGroup, 1);
    g->id = g_uuid_string_random();
    g->title = g_strdup(d ? d->title : "");
    g->monitor = g_strdup(monitor ? monitor : "");
    g->color = g_strdup(d ? d->color : "#202630");
    g->image = g_strdup(d ? d->image : "");
    g->text_shadow_color = g_strdup(d ? d->text_shadow_color : "#000000");
    g->width = d ? d->width : 360;
    g->height = d ? d->height : 260;
    g->opacity = d ? d->opacity : .45;
    g->hover_enter = d ? d->hover_enter : .7;
    g->hover_exit = d ? d->hover_exit : .45;
    g->radius = d ? d->radius : 12;
    g->border = d ? d->border : 1;
    g->text_shadow = d ? d->text_shadow : .35;
    g->title_visible = d ? d->title_visible : TRUE;
    g->title_bottom = d ? d->title_bottom : FALSE;
    g->alignment = d ? d->alignment : 1;
    g->auto_size = d ? d->auto_size : TRUE;
    g->auto_icons = d ? d->auto_icons : TRUE;
    g->locked = d ? d->locked : FALSE;
    g->members = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    return g;
}
void dg_group_free(DgGroup *g) {
    if (!g)
        return;
    g_free(g->id);
    g_free(g->title);
    g_free(g->monitor);
    g_free(g->color);
    g_free(g->image);
    g_free(g->text_shadow_color);
    g_hash_table_destroy(g->members);
    g_free(g);
}
static double number(GKeyFile *k, const char *s, const char *key, double fallback, double lo, double hi) {
    GError *e = NULL;
    double n = g_key_file_get_double(k, s, key, &e);
    if (e || !isfinite(n)) {
        g_clear_error(&e);
        return fallback;
    }
    return CLAMP(n, lo, hi);
}
static gboolean boolean(GKeyFile *k, const char *s, const char *key, gboolean fallback) {
    GError *e = NULL;
    gboolean n = g_key_file_get_boolean(k, s, key, &e);
    if (e) {
        g_clear_error(&e);
        return fallback;
    }
    return n;
}
static void string(GKeyFile *k, const char *s, const char *key, char **dest) {
    char *v = g_key_file_get_string(k, s, key, NULL);
    if (v) {
        g_free(*dest);
        *dest = v;
    }
}
static void read_group(GKeyFile *k, const char *s, DgGroup *g) {
    string(k, s, "title", &g->title);
    string(k, s, "monitor", &g->monitor);
    string(k, s, "color", &g->color);
    string(k, s, "image", &g->image);
    string(k, s, "text_shadow_color", &g->text_shadow_color);
#define N(f, lo, hi) g->f = number(k, s, #f, g->f, lo, hi)
    N(x, -100000, 100000);
    N(y, -100000, 100000);
    N(width, 96, 32768);
    N(height, 64, 32768);
    N(opacity, 0, 1);
    N(hover_enter, 0, 1);
    N(hover_exit, 0, 1);
    N(radius, 0, 100);
    N(border, 0, 20);
    N(text_shadow, 0, 1);
    N(alignment, 0, 2);
#undef N
#define B(f) g->f = boolean(k, s, #f, g->f)
    B(title_visible);
    B(title_bottom);
    B(locked);
    B(auto_size);
    B(auto_icons);
#undef B
}
DgGroup *dg_store_owner(DgStore *s, const char *id) {
    if (!id)
        return NULL;
    for (guint i = 0; i < s->groups->len; i++) {
        DgGroup *g = g_ptr_array_index(s->groups, i);
        if (g_hash_table_contains(g->members, id))
            return g;
    }
    return NULL;
}
void dg_store_assign(DgStore *s, DgGroup *g, const char *id, int row, int col) {
    if (!id || !*id)
        return;
    for (guint i = 0; i < s->groups->len; i++)
        g_hash_table_remove(((DgGroup *)g_ptr_array_index(s->groups, i))->members, id);
    if (g) {
        DgMember *m = g_new(DgMember, 1);
        m->row = row;
        m->col = col;
        g_hash_table_replace(g->members, g_strdup(id), m);
    }
}
void dg_store_remove(DgStore *s, DgGroup *g) {
    g_ptr_array_remove(s->groups, g);
}
DgStore *dg_store_load(const char *path, GError **error) {
    DgStore *s = g_new0(DgStore, 1);
    s->path = g_strdup(path);
    s->writable = TRUE;
    s->groups = g_ptr_array_new_with_free_func((GDestroyNotify)dg_group_free);
    s->defaults = dg_group_new(NULL, "");
    GKeyFile *k = g_key_file_new();
    GError *e = NULL;
    if (!g_key_file_load_from_file(k, path, G_KEY_FILE_NONE, &e)) {
        if (!g_error_matches(e, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            s->writable = FALSE;
            g_propagate_error(error, e);
        } else
            g_clear_error(&e);
        g_key_file_unref(k);
        return s;
    }
    if (g_key_file_get_integer(k, "Desktop Groups", "version", NULL) != 1) {
        s->writable = FALSE;
        g_set_error_literal(
            error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Unsupported Desktop Groups configuration version");
        g_key_file_unref(k);
        return s;
    }
    s->auto_arrange = boolean(k, "Desktop Groups", "auto_arrange", FALSE);
    s->quick_create = boolean(k, "Desktop Groups", "quick_create", FALSE);
    read_group(k, "Defaults", s->defaults);
    gsize n = 0;
    char **sections = g_key_file_get_groups(k, &n);
    for (gsize i = 0; i < n; i++)
        if (g_str_has_prefix(sections[i], "Group ") && g_uuid_string_is_valid(sections[i] + 6)) {
            DgGroup *g = dg_group_new(s->defaults, "");
            g_free(g->id);
            g->id = g_strdup(sections[i] + 6);
            read_group(k, sections[i], g);
            g_ptr_array_add(s->groups, g);
            gsize count = 0;
            char **members = g_key_file_get_string_list(k, sections[i], "members", &count, NULL);
            for (gsize j = 0; j < count; j++) {
                char *key = g_strdup_printf("position-%" G_GSIZE_FORMAT, j);
                gsize len = 0;
                gint *pos = g_key_file_get_integer_list(k, sections[i], key, &len, NULL);
                dg_store_assign(s, g, members[j], len == 2 ? CLAMP(pos[0], 0, 10000) : 0,
                    len == 2 ? CLAMP(pos[1], 0, 10000) : 0);
                g_free(pos);
                g_free(key);
            }
            g_strfreev(members);
        }
    g_strfreev(sections);
    g_key_file_unref(k);
    return s;
}
static void write_group(GKeyFile *k, const char *s, DgGroup *g) {
#define S(f) g_key_file_set_string(k, s, #f, g->f)
    S(title);
    S(monitor);
    S(color);
    S(image);
    S(text_shadow_color);
#undef S
#define N(f) g_key_file_set_double(k, s, #f, g->f)
    N(x);
    N(y);
    N(width);
    N(height);
    N(opacity);
    N(hover_enter);
    N(hover_exit);
    N(radius);
    N(border);
    N(text_shadow);
    N(alignment);
#undef N
#define B(f) g_key_file_set_boolean(k, s, #f, g->f)
    B(title_visible);
    B(title_bottom);
    B(locked);
    B(auto_size);
    B(auto_icons);
#undef B
    GList *ids = g_hash_table_get_keys(g->members);
    ids = g_list_sort(ids, (GCompareFunc)g_strcmp0);
    gsize n = g_list_length(ids), i = 0;
    const char **names = g_new0(const char *, n + 1);
    for (GList *l = ids; l; l = l->next, i++) {
        names[i] = l->data;
        DgMember *m = g_hash_table_lookup(g->members, l->data);
        int pos[] = { m->row, m->col };
        char *key = g_strdup_printf("position-%" G_GSIZE_FORMAT, i);
        g_key_file_set_integer_list(k, s, key, pos, 2);
        g_free(key);
    }
    g_key_file_set_string_list(k, s, "members", names, n);
    g_free(names);
    g_list_free(ids);
}
gboolean dg_store_save(DgStore *s, GError **error) {
    if (!s->writable) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
            "Configuration is read-only after a load error; preserve and repair the original file first");
        return FALSE;
    }
    GKeyFile *k = g_key_file_new();
    g_key_file_set_integer(k, "Desktop Groups", "version", 1);
    g_key_file_set_boolean(k, "Desktop Groups", "auto_arrange", s->auto_arrange);
    g_key_file_set_boolean(k, "Desktop Groups", "quick_create", s->quick_create);
    write_group(k, "Defaults", s->defaults);
    for (guint i = 0; i < s->groups->len; i++) {
        DgGroup *g = g_ptr_array_index(s->groups, i);
        char *section = g_strconcat("Group ", g->id, NULL);
        write_group(k, section, g);
        g_free(section);
    }
    gsize len;
    char *data = g_key_file_to_data(k, &len, NULL);
    char *dir = g_path_get_dirname(s->path);
    gboolean ok = FALSE;
    if (g_mkdir_with_parents(dir, 0700) < 0)
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot create %s", dir);
    else
        ok = g_file_set_contents_full(
            s->path, data, len, G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
    g_free(dir);
    g_free(data);
    g_key_file_unref(k);
    return ok;
}
void dg_store_free(DgStore *s) {
    if (s) {
        g_free(s->path);
        g_ptr_array_unref(s->groups);
        dg_group_free(s->defaults);
        g_free(s);
    }
}

void dg_store_rename(DgStore *s, const char *old_id, const char *new_id) {
    DgGroup *g = dg_store_owner(s, old_id);
    if (g && new_id && g_strcmp0(old_id, new_id)) {
        DgMember pos = *(DgMember *)g_hash_table_lookup(g->members, old_id);
        dg_store_assign(s, NULL, old_id, 0, 0);
        dg_store_assign(s, g, new_id, pos.row, pos.col);
    }
}
