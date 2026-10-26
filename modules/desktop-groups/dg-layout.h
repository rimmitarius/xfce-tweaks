/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <glib.h>

typedef struct {
    int x, y, columns, rows, cell, pitch;
} DgGrid;

DgGrid dg_grid_compute(int x, int y, int width, int height, int cell, int gap, int padding, int title_height,
    gboolean title_bottom, int footer);
void dg_grid_required(
    int count, int columns, int cell, int gap, int padding, int title_height, int *width, int *height);
