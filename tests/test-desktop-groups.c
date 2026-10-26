/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../modules/desktop-groups/dg-store.h"
#include <glib/gstdio.h>
static char *dir, *path;
static DgStore *fresh(void) {
    GError *e = NULL;
    DgStore *s = dg_store_load(path, &e);
    g_assert_no_error(e);
    return s;
}
static void cleanup(void) {
    g_remove(path);
}
static void membership(void) {
    DgStore *s = fresh();
    DgGroup *a = dg_group_new(s->defaults, "EDID-A"), *b = dg_group_new(s->defaults, "EDID-B");
    g_ptr_array_add(s->groups, a);
    g_ptr_array_add(s->groups, b);
    char *file = g_build_filename(dir, "desktop-file.txt", NULL);
    g_file_set_contents(file, "untouched", -1, NULL);
    dg_store_assign(s, a, file, 2, 3);
    g_assert_true(dg_store_owner(s, file) == a);
    dg_store_assign(s, b, file, 4, 5);
    g_assert_true(dg_store_owner(s, file) == b);
    g_assert_cmpuint(g_hash_table_size(a->members), ==, 0);
    dg_store_rename(s, file, "renamed");
    g_assert_null(dg_store_owner(s, file));
    g_assert_true(dg_store_owner(s, "renamed") == b);
    dg_store_assign(s, NULL, "renamed", 0, 0);
    g_assert_null(dg_store_owner(s, "renamed"));
    dg_store_assign(s, a, file, 0, 0);
    dg_store_remove(s, a);
    g_assert_null(dg_store_owner(s, file));
    char *text = NULL;
    g_file_get_contents(file, &text, NULL, NULL);
    g_assert_cmpstr(text, ==, "untouched");
    g_free(text);
    g_remove(file);
    g_free(file);
    dg_store_free(s);
    cleanup();
}
static void roundtrip(void) {
    DgStore *s = fresh();
    s->auto_arrange = TRUE;
    s->quick_create = TRUE;
    s->defaults->hover_exit = .2;
    DgGroup *g = dg_group_new(s->defaults, "stable-EDID-disconnected");
    g_ptr_array_add(s->groups, g);
    g_free(g->title);
    g->title = g_strdup("Code ; files\n日本語");
    g->x = 923.5;
    g->y = 119.25;
    g->width = 411;
    g->height = 333;
    g->auto_size = FALSE;
    g->auto_icons = FALSE;
    g->locked = TRUE;
    g->title_bottom = TRUE;
    g->title_visible = FALSE;
    g->alignment = 2;
    g->opacity = .17;
    g->hover_enter = .88;
    g->hover_exit = .23;
    g->radius = 20;
    g->border = 3;
    g_free(g->image);
    g->image = g_strdup("/unavailable/image.png");
    g_free(g->color);
    g->color = g_strdup("rgba(1,2,3,0.4)");
    dg_store_assign(s, g, "/Desktop/a; b\nç.desktop", 7, 8);
    char *id = g_strdup(g->id);
    GError *e = NULL;
    g_assert_true(dg_store_save(s, &e));
    g_assert_no_error(e);
    dg_store_free(s);
    s = fresh();
    g_assert_cmpuint(s->groups->len, ==, 1);
    g = g_ptr_array_index(s->groups, 0);
    g_assert_cmpstr(g->id, ==, id);
    g_assert_cmpstr(g->monitor, ==, "stable-EDID-disconnected");
    g_assert_cmpstr(g->title, ==, "Code ; files\n日本語");
    g_assert_cmpfloat(g->x, ==, 923.5);
    g_assert_cmpfloat(g->y, ==, 119.25);
    g_assert_cmpfloat(g->width, ==, 411);
    g_assert_cmpfloat(g->height, ==, 333);
    g_assert_false(g->auto_size);
    g_assert_false(g->auto_icons);
    g_assert_true(g->locked);
    g_assert_true(g->title_bottom);
    g_assert_false(g->title_visible);
    g_assert_cmpint(g->alignment, ==, 2);
    g_assert_cmpfloat(g->opacity, ==, .17);
    g_assert_cmpfloat(g->hover_enter, ==, .88);
    g_assert_cmpfloat(g->hover_exit, ==, .23);
    g_assert_cmpfloat(g->radius, ==, 20);
    g_assert_cmpfloat(g->border, ==, 3);
    g_assert_cmpstr(g->image, ==, "/unavailable/image.png");
    g_assert_cmpstr(g->color, ==, "rgba(1,2,3,0.4)");
    g_assert_true(s->auto_arrange);
    g_assert_true(s->quick_create);
    g_assert_cmpfloat(s->defaults->hover_exit, ==, .2);
    DgMember *m = g_hash_table_lookup(g->members, "/Desktop/a; b\nç.desktop");
    g_assert_nonnull(m);
    g_assert_cmpint(m->row, ==, 7);
    g_assert_cmpint(m->col, ==, 8);
    dg_store_free(s);
    g_free(id);
    cleanup();
}
static void corrupt(void) {
    const char *bad = "[broken\nfoo";
    g_file_set_contents(path, bad, -1, NULL);
    GError *e = NULL;
    DgStore *s = dg_store_load(path, &e);
    g_assert_nonnull(e);
    g_clear_error(&e);
    g_assert_false(s->writable);
    g_assert_false(dg_store_save(s, &e));
    g_assert_nonnull(e);
    g_clear_error(&e);
    char *contents = NULL;
    g_file_get_contents(path, &contents, NULL, NULL);
    g_assert_cmpstr(contents, ==, bad);
    g_free(contents);
    dg_store_free(s);
    cleanup();
}
static void invalid(void) {
    const char *data = "[Desktop Groups]\nversion=1\n[Group "
                       "00000000-0000-4000-8000-000000000000]\nwidth=-9\nheight=nan\nopacity=2\nhover_enter=-"
                       "5\nlocked=garbage\nx=inf\nmembers=missing;\n";
    g_file_set_contents(path, data, -1, NULL);
    DgStore *s = fresh();
    DgGroup *g = g_ptr_array_index(s->groups, 0);
    g_assert_cmpfloat(g->width, ==, 96);
    g_assert_cmpfloat(g->height, ==, 260);
    g_assert_cmpfloat(g->opacity, ==, 1);
    g_assert_cmpfloat(g->hover_enter, ==, 0);
    g_assert_cmpfloat(g->x, ==, 0);
    g_assert_false(g->locked);
    g_assert_true(dg_store_owner(s, "missing") == g);
    dg_store_free(s);
    cleanup();
}
static void unsupported(void) {
    g_file_set_contents(path, "[Desktop Groups]\nversion=99\n", -1, NULL);
    GError *e = NULL;
    DgStore *s = dg_store_load(path, &e);
    g_assert_nonnull(e);
    g_clear_error(&e);
    g_assert_false(s->writable);
    dg_store_free(s);
    cleanup();
}
static void defaults(void) {
    DgStore *s = fresh();
    DgGroup *g = dg_group_new(s->defaults, "A");
    g_assert_cmpstr(g->title, ==, "");
    g_assert_true(g->auto_size);
    g_assert_true(g->auto_icons);
    g_assert_cmpuint(g_hash_table_size(g->members), ==, 0);
    dg_group_free(g);
    dg_store_free(s);
}
int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    dir = g_dir_make_tmp("desktop-groups-tests-XXXXXX", NULL);
    path = g_build_filename(dir, "groups.ini", NULL);
    g_test_add_func("/groups/empty-defaults", defaults);
    g_test_add_func("/groups/membership-no-file-operations", membership);
    g_test_add_func("/groups/persistence-monitors-appearance", roundtrip);
    g_test_add_func("/groups/corruption-preservation", corrupt);
    g_test_add_func("/groups/invalid-values", invalid);
    g_test_add_func("/groups/future-version", unsupported);
    int result = g_test_run();
    g_rmdir(dir);
    g_free(dir);
    g_free(path);
    return result;
}
