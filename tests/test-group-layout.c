/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../modules/desktop-groups/dg-layout.h"

static void corner(void) {
    DgGrid g = dg_grid_compute(103, 133, 397, 200, 104, 6, 8, 34, FALSE, 0);
    g_assert_cmpint(g.x, ==, 111);
    g_assert_cmpint(g.y, ==, 175);
    g_assert_cmpint(g.columns, ==, 3);
    g_assert_cmpint(g.rows, ==, 1);
    DgGrid moved = dg_grid_compute(120, 144, 397, 200, 104, 6, 8, 34, FALSE, 0);
    g_assert_cmpint(moved.columns, ==, g.columns);
    g_assert_cmpint(moved.rows, ==, g.rows);
    g_assert_cmpint(moved.x - g.x, ==, 17);
    g_assert_cmpint(moved.y - g.y, ==, 11);
}
static void compact(void) {
    int w, h;
    dg_grid_required(3, 3, 104, 6, 8, 34, &w, &h);
    g_assert_cmpint(w, ==, 340);
    g_assert_cmpint(h, ==, 154);
    DgGrid grid = dg_grid_compute(103, 133, w, h, 104, 6, 8, 34, FALSE, 0);
    g_assert_cmpint(grid.columns * grid.rows, ==, 3);
    dg_grid_required(1, 1, 104, 6, 8, 0, &w, &h);
    g_assert_cmpint(w, ==, 120);
    g_assert_cmpint(h, ==, 120);
}
static void scaling(void) {
    DgGrid small = dg_grid_compute(103, 133, 397, 300, 64, 6, 8, 34, FALSE, 0);
    DgGrid large = dg_grid_compute(103, 133, 397, 300, 128, 6, 8, 34, FALSE, 0);
    g_assert_cmpint(small.x, ==, large.x);
    g_assert_cmpint(small.y, ==, large.y);
    g_assert_cmpint(small.columns, >, large.columns);
    g_assert_cmpint(small.rows, >, large.rows);
    g_assert_cmpint(large.pitch, ==, 134);
}
static void title_and_overflow(void) {
    DgGrid top = dg_grid_compute(103, 133, 240, 220, 104, 6, 8, 34, FALSE, 24);
    DgGrid bottom = dg_grid_compute(103, 133, 240, 220, 104, 6, 8, 34, TRUE, 24);
    DgGrid hidden = dg_grid_compute(103, 133, 240, 220, 104, 6, 8, 0, FALSE, 24);
    g_assert_cmpint(top.y, ==, 175);
    g_assert_cmpint(bottom.y, ==, 141);
    g_assert_cmpint(hidden.y, ==, 141);
    g_assert_cmpint(top.columns * top.rows, ==, 2);
    DgGrid tiny = dg_grid_compute(0, 0, 96, 64, 104, 6, 8, 34, FALSE, 24);
    g_assert_cmpint(tiny.columns, ==, 0);
    g_assert_cmpint(tiny.rows, ==, 0);
}
int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/layout/local-upper-left", corner);
    g_test_add_func("/layout/compact-auto-size", compact);
    g_test_add_func("/layout/native-cell-size-changes", scaling);
    g_test_add_func("/layout/title-and-overflow", title_and_overflow);
    return g_test_run();
}
