/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../modules/desktop-groups/desktop-groups.c"
#include <stdio.h>
static GtkWidget*areas[2];static XtViewState states[2];static XtIconState icons[3];static const char*ids[]={"/test/a","/test/b","/test/c"};static DgGroup*group;static int transfers;
static const XtViewState*view_state(GtkWidget*w){return &states[w==areas[1]];}
static const XtIconState*icon_state(gpointer item){return &icons[GPOINTER_TO_INT(item)-1];}
static const char*icon_id(GtkWidget*w,gpointer item){return ids[GPOINTER_TO_INT(item)-1];}
static void transfer(GtkWidget*source,GtkWidget*dest,gpointer item){
 const char*id=icon_id(source,item);DgGroup*owner=dg_store_owner(dg_store,id);
 g_assert_cmpstr(owner->monitor,==,dg_state(dest)->monitor); /* Native callback reentrancy. */
 XtViewState*s=&states[source==areas[1]],*d=&states[dest==areas[1]];g_assert_nonnull(g_list_find(s->items,item));s->items=g_list_remove(s->items,item);d->items=g_list_append(d->items,item);transfers++;
}
static gboolean draw_cb(GtkWidget*w,cairo_t*cr,gpointer p){dg_draw(w,cr);return FALSE;}
static gboolean press_cb(GtkWidget*w,GdkEventButton*e,gpointer p){return dg_press(w,e);}
static gboolean motion_cb(GtkWidget*w,GdkEventMotion*e,gpointer p){return dg_motion(w,e);}
static gboolean release_cb(GtkWidget*w,GdkEventButton*e,gpointer p){return dg_release(w,e);}
static gboolean command(GIOChannel*io,GIOCondition condition,gpointer p){char*line=NULL;if(g_io_channel_read_line(io,&line,NULL,NULL,NULL)!=G_IO_STATUS_NORMAL){gtk_main_quit();return FALSE;}
 if(g_str_has_prefix(line,"right")){g_assert_cmpstr(group->monitor,==,"right");g_assert_cmpuint(g_list_length(states[1].items),==,3);g_assert_cmpint((int)group->x,==,160);g_assert_cmpint((int)group->y,==,170);g_assert_cmpint((int)group->width,==,280);puts("PASS cross-display motion transfers group and all members before release");}
 else if(g_str_has_prefix(line,"left")){g_assert_cmpstr(group->monitor,==,"left");g_assert_cmpuint(g_list_length(states[0].items),==,3);g_assert_cmpint((int)group->x,==,80);g_assert_cmpint((int)group->y,==,80);puts("PASS reverse transfer preserves group dimensions and membership");}
 else if(g_str_has_prefix(line,"released")){g_assert_null(dg_active_drag());g_assert_false(gtk_widget_has_grab(areas[0]));g_assert_false(gtk_widget_has_grab(areas[1]));puts("PASS pointer and GTK grabs released");}
 else if(g_str_has_prefix(line,"quit")){g_assert_cmpint(transfers,>=,9);gtk_main_quit();}
 fflush(stdout);g_free(line);return TRUE;}
int main(int argc,char**argv){gtk_init(&argc,&argv);XtHost api={.abi_version=XT_ABI_VERSION,.struct_size=sizeof(XtHost),.view_state=view_state,.icon_state=icon_state,.icon_id=icon_id,.transfer_icon=transfer};g_assert_nonnull(xfce_tweak_query(&api));
 GtkWidget*windows[2];for(int i=0;i<2;i++){windows[i]=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_decorated(GTK_WINDOW(windows[i]),FALSE);gtk_window_set_default_size(GTK_WINDOW(windows[i]),800,700);gtk_window_move(GTK_WINDOW(windows[i]),i*800,0);areas[i]=gtk_drawing_area_new();gtk_widget_add_events(areas[i],GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK);gtk_container_add(GTK_CONTAINER(windows[i]),areas[i]);gtk_widget_show_all(windows[i]);states[i].slot_size=64;states[i].icon_size=48;xfdesktop_icon_view_groups_enable(areas[i],i?"right":"left");g_signal_connect(areas[i],"draw",G_CALLBACK(draw_cb),NULL);g_signal_connect(areas[i],"button-press-event",G_CALLBACK(press_cb),NULL);g_signal_connect(areas[i],"motion-notify-event",G_CALLBACK(motion_cb),NULL);g_signal_connect(areas[i],"button-release-event",G_CALLBACK(release_cb),NULL);}
 if(argc>1){g_assert_cmpuint(dg_store->groups->len,==,1);group=g_ptr_array_index(dg_store->groups,0);g_assert_cmpstr(group->monitor,==,"right");g_assert_cmpuint(g_hash_table_size(group->members),==,3);puts("PASS transferred group and membership restore from disk");}
 else{group=dg_group_new(dg_store->defaults,"left");group->x=80;group->y=80;group->width=280;group->height=160;group->auto_size=FALSE;group->locked=FALSE;g_ptr_array_add(dg_store->groups,group);for(int i=0;i<3;i++){gpointer item=GINT_TO_POINTER(i+1);states[0].items=g_list_append(states[0].items,item);dg_store_assign(dg_store,group,ids[i],0,i);}dg_store->auto_arrange=FALSE;dg_refresh();puts("READY");fflush(stdout);GIOChannel*io=g_io_channel_unix_new(0);g_io_add_watch(io,G_IO_IN|G_IO_HUP,command,NULL);gtk_main();g_io_channel_unref(io);}
 dg_detach(areas[0]);dg_detach(areas[1]);g_list_free(states[0].items);g_list_free(states[1].items);gtk_widget_destroy(windows[0]);gtk_widget_destroy(windows[1]);return 0;}
