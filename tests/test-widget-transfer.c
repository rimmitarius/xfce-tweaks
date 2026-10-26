/* SPDX-License-Identifier: GPL-2.0-or-later */
#include MODULE_SOURCE
#include <stdio.h>
static XfconfChannel*channel;
static GtkWidget*left_view,*right_view;
static XfconfChannel*settings(void){return channel;}
static Widget*state(GtkWidget*v){return g_object_get_data(G_OBJECT(v),TEST_WIDGET_ID);}
static gboolean command(GIOChannel*io,GIOCondition condition,gpointer unused){
 char*line=NULL;if(g_io_channel_read_line(io,&line,NULL,NULL,NULL)!=G_IO_STATUS_NORMAL){gtk_main_quit();return FALSE;}
 Widget*a=state(left_view),*b=state(right_view);
 if(g_str_has_prefix(line,"preview")){g_assert_true(a->drop_target==b);g_assert_true(b->preview==a);g_assert_cmpint(rect(b).x,==,160);g_assert_cmpint(rect(b).y,==,170);puts("PASS destination preview");}
 else if(g_str_has_prefix(line,"forward")){g_assert_true(a->hidden);g_assert_false(b->hidden);g_assert_cmpint(b->x,==,160);g_assert_cmpint(b->y,==,170);g_assert_cmpint(b->width,==,280);g_assert_cmpint(b->height,==,160);g_assert_false(b->running);puts("PASS forward transfer and size");}
 else if(g_str_has_prefix(line,"back")){g_assert_false(a->hidden);g_assert_true(b->hidden);g_assert_cmpint(a->x,==,80);g_assert_cmpint(a->y,==,80);puts("PASS reverse transfer");}
 else if(g_str_has_prefix(line,"quit")){gtk_main_quit();}
 fflush(stdout);g_free(line);return TRUE;
}
int main(int argc,char**argv){
 gtk_init(&argc,&argv);g_assert_true(xfconf_init(NULL));channel=xfconf_channel_get("xfce4-desktop");
 XtHost api={.abi_version=XT_ABI_VERSION,.struct_size=sizeof(XtHost),.settings=settings};g_assert_nonnull(xfce_tweak_query(&api));
 GtkWidget*windows[2];GtkWidget**areas[]={&left_view,&right_view};
 for(int i=0;i<2;i++){windows[i]=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_decorated(GTK_WINDOW(windows[i]),FALSE);gtk_window_set_default_size(GTK_WINDOW(windows[i]),800,700);gtk_window_move(GTK_WINDOW(windows[i]),i*800,0);*areas[i]=gtk_drawing_area_new();gtk_widget_add_events(*areas[i],GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK|GDK_LEAVE_NOTIFY_MASK);gtk_container_add(GTK_CONTAINER(windows[i]),*areas[i]);gtk_widget_show_all(windows[i]);attach(*areas[i],i?"test-right":"test-left");}
 Widget*a=state(left_view),*b=state(right_view);
 if(argc>1 && !strcmp(argv[1],"restore")){g_assert_true(a->hidden);g_assert_false(b->hidden);g_assert_cmpint(b->width,==,280);g_assert_cmpint(b->height,==,160);g_assert_cmpint(b->x,==,160);puts("PASS monitor assignment restored");detach(right_view);g_assert_false(widget_hidden(a));attach(right_view,"test-right");g_assert_true(widget_hidden(a));g_assert_false(widget_hidden(state(right_view)));puts("PASS disconnected-display fallback and reconnect");}
 else{
  a->x=80;a->y=80;a->width=280;a->height=160;a->positioned=TRUE;a->hidden=FALSE;
  b->x=400;b->y=400;b->width=200;b->height=120;b->positioned=TRUE;b->hidden=FALSE;
  gtk_widget_queue_draw(left_view);gtk_widget_queue_draw(right_view);
  puts("READY");fflush(stdout);GIOChannel*io=g_io_channel_unix_new(0);g_io_add_watch(io,G_IO_IN|G_IO_HUP,command,NULL);gtk_main();g_io_channel_unref(io);
 }
 detach(left_view);detach(right_view);gtk_widget_destroy(windows[0]);gtk_widget_destroy(windows[1]);xfconf_shutdown();return 0;
}
