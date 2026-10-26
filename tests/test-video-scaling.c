/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../modules/video-wallpaper/video-wallpaper.c"
static XfconfChannel*channel;
static XfconfChannel*settings(void){return channel;}
static void run_case(GtkWidget*desktop,const char*mode,int expected_height,gboolean aspect){
 GError*error=NULL;
 GstElement*pipeline=gst_parse_launch("videotestsrc num-buffers=1 ! video/x-raw,width=3840,height=2160 ! aspectratiocrop name=crop ! fakesink name=out sync=false",&error);g_assert_no_error(error);
 Video v={.desktop=desktop,.player=pipeline,.prefix="ultrawide",.crop=gst_bin_get_by_name(GST_BIN(pipeline),"crop"),.sink=gst_element_factory_make("gtksink",NULL)};
 gst_object_ref_sink(v.sink);char*key=scaling_key(&v);xfconf_channel_set_string(channel,key,mode);g_free(key);apply_scaling(&v);
 gboolean actual=FALSE;g_object_get(v.sink,"force-aspect-ratio",&actual,NULL);g_assert_cmpint(actual,==,aspect);
 gst_element_set_state(pipeline,GST_STATE_PLAYING);GstBus*bus=gst_element_get_bus(pipeline);GstMessage*m=gst_bus_timed_pop_filtered(bus,10*GST_SECOND,GST_MESSAGE_EOS|GST_MESSAGE_ERROR);g_assert_nonnull(m);g_assert_cmpint(GST_MESSAGE_TYPE(m),==,GST_MESSAGE_EOS);
 GstElement*out=gst_bin_get_by_name(GST_BIN(pipeline),"out");GstPad*pad=gst_element_get_static_pad(out,"sink");GstCaps*caps=gst_pad_get_current_caps(pad);g_assert_nonnull(caps);
 int width,height;const GstStructure*structure=gst_caps_get_structure(caps,0);g_assert_true(gst_structure_get_int(structure,"width",&width));g_assert_true(gst_structure_get_int(structure,"height",&height));g_assert_cmpint(width,==,3840);g_assert_cmpint(height,==,expected_height);
 g_print("PASS %s: 4K frames negotiated to %dx%d for %dx%d display\n",mode,width,height,gtk_widget_get_allocated_width(desktop),gtk_widget_get_allocated_height(desktop));
 gst_caps_unref(caps);gst_object_unref(pad);gst_object_unref(out);gst_message_unref(m);gst_object_unref(bus);gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(pipeline);gst_object_unref(v.crop);gst_object_unref(v.sink);
}
int main(int argc,char**argv){
 gtk_init(&argc,&argv);gst_init(&argc,&argv);g_assert_true(xfconf_init(NULL));channel=xfconf_channel_get("xfce4-desktop");XtHost api={.settings=settings};host=&api;
 GtkWidget*desktop=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_default_size(GTK_WINDOW(desktop),2560,1080);gtk_widget_show(desktop);while(gtk_events_pending())gtk_main_iteration();
 run_case(desktop,"fill",1620,TRUE);run_case(desktop,"fit",2160,TRUE);run_case(desktop,"stretch",2160,FALSE);
 Video a={.prefix="monitor-a"},b={.prefix="monitor-b"};char*ka=scaling_key(&a),*kb=scaling_key(&b);xfconf_channel_set_string(channel,ka,"fill");xfconf_channel_set_string(channel,kb,"fit");g_assert_cmpint(scaling_mode(&a),==,2);g_assert_cmpint(scaling_mode(&b),==,1);g_free(ka);g_free(kb);g_print("PASS independent monitor scaling settings\n");
 gtk_widget_destroy(desktop);xfconf_shutdown();return 0;
}
