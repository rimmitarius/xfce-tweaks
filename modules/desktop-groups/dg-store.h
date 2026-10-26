/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <glib.h>

typedef struct {
    int row, col;
} DgMember;
typedef struct {
    char *id, *title, *monitor, *color, *image, *text_shadow_color;
    double x, y, width, height, opacity, hover_enter, hover_exit, radius, border, text_shadow;
    gboolean title_visible, title_bottom, locked, auto_size, auto_icons;
    int alignment;
    GHashTable *members; /* native xfdesktop identifier -> DgMember */
} DgGroup;
typedef struct {
    char *path;
    GPtrArray *groups;
    DgGroup *defaults;
    gboolean auto_arrange, quick_create, writable;
} DgStore;

DgGroup *dg_group_new(const DgGroup *defaults, const char *monitor);
void dg_group_free(DgGroup *group);
DgStore *dg_store_load(const char *path, GError **error);
void dg_store_free(DgStore *store);
gboolean dg_store_save(DgStore *store, GError **error);
void dg_store_assign(DgStore *store, DgGroup *group, const char *identifier, int row, int col);
DgGroup *dg_store_owner(DgStore *store, const char *identifier);
void dg_store_remove(DgStore *store, DgGroup *group);

void dg_store_rename(DgStore *store, const char *old_id, const char *new_id);
