/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "xfce-tweaks.h"
static gboolean hit(GtkWidget*w,double x,double y,gpointer data){return x>=10&&x<20&&y>=10&&y<20;}
int main(int argc,char**argv){
 g_test_init(&argc,&argv,NULL);
 /* Helpers store only GObject data; no display or native view is needed. */
 GObject*object=g_object_new(G_TYPE_OBJECT,NULL);GtkWidget*view=(GtkWidget*)object;
 gpointer a=GINT_TO_POINTER(1),b=GINT_TO_POINTER(2);
 g_assert_false(xt_surface_at(view,15,15));xt_surface_register(view,"test",hit,NULL);
 g_assert_true(xt_surface_at(view,15,15));g_assert_false(xt_surface_at(view,25,15));xt_surface_unregister(view,"test");g_assert_false(xt_surface_at(view,15,15));
 g_assert_cmpfloat(xt_icon_opacity(view,a),==,1);
 xt_icon_opacity_set(view,"groups",a,.4);g_assert_cmpfloat(xt_icon_opacity(view,a),==,.4);g_assert_cmpfloat(xt_icon_opacity(view,b),==,1);
 xt_icon_opacity_set(view,"other",a,.5);g_assert_cmpfloat(xt_icon_opacity(view,a),==,.2);
 xt_icon_opacity_clear(view,"groups");g_assert_cmpfloat(xt_icon_opacity(view,a),==,.5);
 xt_icon_opacity_clear(view,"other");g_assert_cmpfloat(xt_icon_opacity(view,a),==,1);
 xt_icon_opacity_set(view,"groups",a,0);g_assert_cmpfloat(xt_icon_opacity(view,a),==,0);
 xt_icon_opacity_set(view,"groups",a,2);g_assert_cmpfloat(xt_icon_opacity(view,a),==,1);
 g_object_unref(object);g_print("PASS: interactive surfaces, assigned-icon opacity, provider composition and clearing\n");return 0;
}
