/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../modules/desktop-peek/desktop-peek.c"
static XtViewState state;
static XtIconState icon;
static const XtViewState *view_state(GtkWidget*w){return &state;}
static const XtIconState *icon_state(gpointer item){return &icon;}
static gboolean surface(GtkWidget*w,double x,double y,gpointer data){return x>=90&&x<110;}
int main(int argc,char**argv){
 g_test_init(&argc,&argv,NULL);
 XtHost api={.abi_version=XT_ABI_VERSION,.struct_size=sizeof(XtHost),.view_state=view_state,.icon_state=icon_state};host=&api;
 GObject*object=g_object_new(G_TYPE_OBJECT,NULL);GtkWidget*view=(GtkWidget*)object;
 cairo_rectangle_int_t bounds={10,10,30,40};icon.icon_slot_region=cairo_region_create_rectangle(&bounds);icon.placed=TRUE;
 state.items=g_list_append(NULL,GINT_TO_POINTER(1));
 GdkEventButton event={.type=GDK_2BUTTON_PRESS,.button=1,.x=20,.y=20};
 g_assert_false(view_press(view,&event));event.y=45;g_assert_false(view_press(view,&event));
 xt_surface_register(view,"widget",surface,NULL);event.x=100;event.y=100;g_assert_false(view_press(view,&event));xt_surface_unregister(view,"widget");
 event.state=GDK_CONTROL_MASK;g_assert_false(view_press(view,&event));event.state=GDK_SHIFT_MASK;g_assert_false(view_press(view,&event));event.state=GDK_MOD1_MASK;g_assert_false(view_press(view,&event));
 event.state=0;event.button=3;g_assert_false(plain_double(&event));event.button=1;event.type=GDK_BUTTON_PRESS;g_assert_false(plain_double(&event));
 g_list_free(state.items);cairo_region_destroy(icon.icon_slot_region);g_object_unref(object);
 g_print("PASS: native icon and label double-clicks, interactive surfaces and modifiers are never intercepted\n");return 0;
}
