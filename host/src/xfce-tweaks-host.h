/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../../sdk/xfce-tweaks.h"
void xt_attach_view(GtkWidget *, const char *);
void xt_view_menu(GtkWidget *, GtkWidget *, int, int);
void xt_file_renamed(GFile *, GFile *);
void xt_draw(GtkWidget *, cairo_t *);
gboolean xt_coords_to_slot(GtkWidget *, int, int, int *, int *);
gboolean xt_press(GtkWidget *, GdkEventButton *);
gboolean xt_release(GtkWidget *, GdkEventButton *);
gboolean xt_motion(GtkWidget *, GdkEventMotion *);
void xt_leave(GtkWidget *);
void xt_band_menu(GtkWidget *, GdkEventButton *, gboolean);
void xt_dropped(GtkWidget *, GtkWidget *, gpointer, int, int);
void xt_attach_desktop(GtkWidget *);
gboolean xt_backdrop_changed(GtkWidget *, const char *, const char *);
void xt_desktop_menu(GtkWidget *, GtkWidget *);
const XtHost *xt_icon_host_api(void);
void xt_reset_icon_layout(GtkWidget *);
void xt_host_set_backdrop(GtkWidget *, GtkWidget *);
