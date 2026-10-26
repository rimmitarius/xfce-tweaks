/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "xfce-tweaks.h"
#include <string.h>
#define HIDDEN "/xfce-tweaks/desktop-peek/hidden"
static const XtHost *host;
static GList *views, *desktops;
static gboolean hidden;
static gulong watch;
typedef struct { GtkWidget *widget; gboolean ours, destroying; } View;
static void hide_view(View *v) {
    if (gtk_widget_get_visible(v->widget)) {
        v->ours = TRUE;
        if (gtk_widget_has_grab(v->widget)) gtk_grab_remove(v->widget);
        gtk_widget_hide(v->widget);
    }
}
static void set_hidden(gboolean value) {
    if (hidden == value) return;
    hidden = value;
    for (GList *l=views;l;l=l->next) {
        View *v=l->data;
        if (value) hide_view(v);
        else if (v->ours) { v->ours=FALSE; gtk_widget_show(v->widget); }
    }
    xfconf_channel_set_bool(host->settings(),HIDDEN,value);
}
static void changed(XfconfChannel *c,const char *key,const GValue *value,gpointer unused) {
    if (!strcmp(key,HIDDEN)) set_hidden(xfconf_channel_get_bool(c,key,FALSE));
}
static void start(void) {
    if (watch) return;
    /* Hidden is a temporary presentation state: a restart always recovers. */
    hidden=FALSE;
    xfconf_channel_set_bool(host->settings(),HIDDEN,FALSE);
    watch=g_signal_connect(host->settings(),"property-changed",G_CALLBACK(changed),NULL);
}
static void stop_if_unused(void) {
    if (views || desktops) return;
    hidden=FALSE;
    if (watch) { g_signal_handler_disconnect(host->settings(),watch);watch=0; }
    xfconf_channel_set_bool(host->settings(),HIDDEN,FALSE);
}
static gboolean plain_double(GdkEventButton *e) {
    return e->type==GDK_2BUTTON_PRESS && e->button==1 &&
        !(e->state&(GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SUPER_MASK|GDK_META_MASK|GDK_HYPER_MASK));
}
static gboolean view_press(GtkWidget *view,GdkEventButton *e) {
    if (hidden || !plain_double(e) || xt_surface_at(view,e->x,e->y)) return FALSE;
    const XtViewState *state=host->view_state(view);
    if (!state || state->definitely_rubber_banding) return FALSE;
    for (GList *l=state->items;l;l=l->next) {
        const XtIconState *icon=host->icon_state(l->data);
        if (icon && icon->placed && icon->icon_slot_region &&
            cairo_region_contains_point(icon->icon_slot_region,e->x,e->y)) return FALSE;
    }
    /* Complete the native second press before its release target is hidden. */
    GdkEventButton release=*e;release.type=GDK_BUTTON_RELEASE;
    gboolean handled=FALSE;
    g_signal_emit_by_name(view,"button-release-event",&release,&handled);
    set_hidden(TRUE);
    return TRUE;
}
static gboolean desktop_press(GtkWidget *desktop,GdkEventButton *e,gpointer unused) {
    if (!hidden || !plain_double(e)) return FALSE;
    set_hidden(FALSE);
    return TRUE;
}
static void on_show(GtkWidget *view,View *v) { if (hidden) hide_view(v); }
static void on_destroy(GtkWidget *view,View *v) { v->destroying=TRUE; }
static void free_view(gpointer data) {
    View *v=data;
    views=g_list_remove(views,v);
    g_signal_handlers_disconnect_by_data(v->widget,v);
    if (v->ours && !v->destroying) gtk_widget_show(v->widget);
    g_free(v);stop_if_unused();
}
static void attach_view(GtkWidget *view,const char *monitor) {
    if (g_object_get_data(G_OBJECT(view),"desktop-peek")) return;
    start();View *v=g_new0(View,1);v->widget=view;
    views=g_list_prepend(views,v);
    g_object_set_data_full(G_OBJECT(view),"desktop-peek",v,free_view);
    g_signal_connect(view,"show",G_CALLBACK(on_show),v);
    g_signal_connect(view,"destroy",G_CALLBACK(on_destroy),v);
    if (hidden) hide_view(v);
}
static void detach_view(GtkWidget *view) {g_object_set_data(G_OBJECT(view),"desktop-peek",NULL);}
static void desktop_gone(gpointer unused,GObject *desktop) {desktops=g_list_remove(desktops,desktop);stop_if_unused();}
static void attach_desktop(GtkWidget *desktop) {
    if (g_list_find(desktops,desktop)) return;
    start();desktops=g_list_prepend(desktops,desktop);
    g_object_weak_ref(G_OBJECT(desktop),desktop_gone,NULL);
    g_signal_connect(desktop,"button-press-event",G_CALLBACK(desktop_press),NULL);
}
static void detach_desktop(GtkWidget *desktop) {
    g_signal_handlers_disconnect_by_func(desktop,G_CALLBACK(desktop_press),NULL);
    g_object_weak_unref(G_OBJECT(desktop),desktop_gone,NULL);
    desktops=g_list_remove(desktops,desktop);
    if (!desktops) set_hidden(FALSE);
    stop_if_unused();
}
static void reveal(GtkMenuItem *item,gpointer unused) {if(watch)set_hidden(!hidden);}
static void menu(GtkWidget *source,GtkWidget *root) {
    if (g_object_get_data(G_OBJECT(root),"desktop-peek-menu")) return;
    g_object_set_data(G_OBJECT(root),"desktop-peek-menu",GINT_TO_POINTER(1));
    GtkWidget *parent=gtk_menu_item_new_with_label("Desktop Visibility"),*sub=gtk_menu_new();
    GtkWidget *toggle=gtk_menu_item_new_with_label(hidden?"Show desktop items":"Hide desktop items");
    g_signal_connect(toggle,"activate",G_CALLBACK(reveal),NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(sub),toggle);gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(root),parent);gtk_widget_show_all(parent);
}
static void view_menu(GtkWidget *view,GtkWidget *root,int x,int y) {menu(view,root);}
static const XtTweak tweak={.abi_version=XT_ABI_VERSION,.struct_size=sizeof(XtTweak),
    .id="desktop-peek",.name="Desktop Visibility",.attach_view=attach_view,.detach_view=detach_view,
    .attach_desktop=attach_desktop,.detach_desktop=detach_desktop,.button_press=view_press,
    .desktop_menu=menu,.view_menu=view_menu};
G_MODULE_EXPORT const XtTweak *xfce_tweak_query(const XtHost *api) {
    if (!api || api->abi_version!=XT_ABI_VERSION || api->struct_size<sizeof(XtHost)) return NULL;
    host=api;return &tweak;
}
