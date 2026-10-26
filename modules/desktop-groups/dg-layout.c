/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dg-layout.h"

DgGrid dg_grid_compute(int x, int y, int width, int height, int cell, int gap, int padding, int title_height,
    gboolean title_bottom, int footer) {
    cell = MAX(1, cell);
    gap = MAX(0, gap);
    DgGrid grid = { .x = x + padding,
        .y = y + padding + (title_bottom ? 0 : title_height),
        .cell = cell,
        .pitch = cell + gap };
    int available_width = width - padding * 2;
    int available_height = height - padding * 2 - title_height - footer;
    grid.columns = MAX(0, (available_width + gap) / grid.pitch);
    grid.rows = MAX(0, (available_height + gap) / grid.pitch);
    return grid;
}

void dg_grid_required(
    int count, int columns, int cell, int gap, int padding, int title_height, int *width, int *height) {
    columns = MAX(1, columns);
    count = MAX(1, count);
    int rows = (count + columns - 1) / columns;
    *width = 2 * padding + columns * cell + MAX(0, columns - 1) * gap;
    *height = 2 * padding + title_height + rows * cell + MAX(0, rows - 1) * gap;
}
