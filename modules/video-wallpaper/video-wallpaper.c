/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "xfce-tweaks.h"
#include <gst/gst.h>
#include <string.h>
static const XtHost *host;
typedef struct {
  GtkWidget *desktop, *widget;
  GstElement *player,*sink,*sinkbin,*crop;
  int crop_width,crop_height;
  guint watch;
  char *filename, *prefix,*monitor_id,*native_filename;
  GtkWidget *chooser;
  gboolean retired,using_gl,force_software;
  GList *menus;
} Video;
static void stop(Video *v) {
  if (v->watch) {
    g_source_remove(v->watch);
    v->watch = 0;
  }
  if (v->player)
    gst_element_set_state(v->player, GST_STATE_NULL);
  if (v->widget) {
    host->set_backdrop_widget(v->desktop, NULL);
    g_clear_object(&v->widget);
  }
  if (v->player) {
    gst_object_unref(v->player);
    v->player = NULL;
  }
  gst_clear_object(&v->sink);gst_clear_object(&v->sinkbin);gst_clear_object(&v->crop);v->crop_width=v->crop_height=0;v->using_gl=FALSE;
  g_clear_pointer(&v->filename, g_free);
}
static void menu_destroyed(GtkWidget *w, Video *v) {
  v->menus = g_list_remove(v->menus, w);
}
static void free_video(gpointer data) {
  Video *v = data;
  g_signal_handlers_disconnect_by_data(host->settings(), v);
  stop(v);
  if(!v->retired)g_signal_handlers_disconnect_by_data(v->desktop,v);
  while (v->menus)
    gtk_widget_destroy(v->menus->data);
  if (v->chooser) {
    v->retired = TRUE;
    gtk_dialog_response(GTK_DIALOG(v->chooser), GTK_RESPONSE_CANCEL);
    return;
  }
  g_free(v->prefix);g_free(v->monitor_id);g_free(v->native_filename);
  g_free(v);
}
static gboolean backdrop(GtkWidget*,const char*,const char*);
static char*display_key(Video*v,const char*name){
  char*id=g_compute_checksum_for_string(G_CHECKSUM_SHA256,v->monitor_id?v->monitor_id:v->prefix?v->prefix:"default",-1);
  char*key=g_strconcat("/video-wallpaper/displays/",id,"/",name,NULL);g_free(id);return key;
}
static gboolean display_bool(Video*v,const char*name,gboolean fallback){
  char*global=g_strconcat("/video-wallpaper/",name,NULL);fallback=xfconf_channel_get_bool(host->settings(),global,fallback);g_free(global);
  char*key=display_key(v,name);gboolean value=xfconf_channel_get_bool(host->settings(),key,fallback);g_free(key);return value;
}
static void set_display_bool(Video*v,const char*name,gboolean value){char*key=display_key(v,name);xfconf_channel_set_bool(host->settings(),key,value);g_free(key);}
static char *display_string(Video*v,const char*name,const char*fallback){char*key=display_key(v,name);char*value=xfconf_channel_get_string(host->settings(),key,fallback);g_free(key);return value;}
typedef enum { XT_AUTOPLUG_TRY=0, XT_AUTOPLUG_EXPOSE=1, XT_AUTOPLUG_SKIP=2 } XtAutoplugSelect;
static XtAutoplugSelect select_decoder(GstElement*bin,GstPad*pad,GstCaps*caps,GstElementFactory*factory,gpointer data){
  Video*v=data;char*mode=display_string(v,"gpu","auto");if(!strcmp(mode,"auto")){g_free(mode);return XT_AUTOPLUG_TRY;}
  const char*klass=gst_element_factory_get_klass(factory);const char*name=gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
  if(!klass||!strstr(klass,"Decoder")||!strstr(klass,"Video")){g_free(mode);return XT_AUTOPLUG_TRY;}
  gboolean intel=g_str_has_prefix(name,"va")||g_str_has_prefix(name,"qsv"),nvidia=g_str_has_prefix(name,"nv");
  gboolean match=(!strcmp(mode,"intel")&&intel)||(!strcmp(mode,"nvidia")&&nvidia);g_free(mode);
  if(match)return XT_AUTOPLUG_TRY;
  if(intel||nvidia)return XT_AUTOPLUG_SKIP;
  return XT_AUTOPLUG_TRY;
}
static void deep_element_added(GstBin*bin,GstBin*subbin,GstElement*element,gpointer data){
  Video*v=data;
  if(g_signal_lookup("autoplug-select",G_OBJECT_TYPE(element)))g_signal_connect(element,"autoplug-select",G_CALLBACK(select_decoder),data);
  const char*klass=GST_IS_ELEMENT(element)?gst_element_get_metadata(element,"klass"):NULL;
  if(klass&&strstr(klass,"Decoder")&&strstr(klass,"Video")){char*mode=display_string(v,"gpu","auto");g_message("Video Wallpaper: %s decode preference selected %s",mode,GST_OBJECT_NAME(element));g_free(mode);}
}
static void seed_controls(Video*v){
 const char*names[]={"paused","muted"};
 for(int n=0;n<2;n++){char*key=display_key(v,names[n]);if(!xfconf_channel_has_property(host->settings(),key))xfconf_channel_set_bool(host->settings(),key,display_bool(v,names[n],n==1));g_free(key);}
}
static gboolean paused(Video*v){return display_bool(v,"paused",FALSE);}
static char*scaling_key(Video*v){
  char*id=g_compute_checksum_for_string(G_CHECKSUM_SHA256,v->prefix?v->prefix:"default",-1);
  char*key=g_strconcat("/video-wallpaper/scaling/",id,NULL);g_free(id);return key;
}
static int scaling_mode(Video*v){
  char*key=scaling_key(v);char*value=xfconf_channel_get_string(host->settings(),key,"stretch");g_free(key);
  int mode=!strcmp(value,"fill")?2:!strcmp(value,"fit")?1:0;g_free(value);return mode;
}
static void refresh_frame(Video*v){
  gint64 position=0;
  if(v->player && paused(v) && gst_element_query_position(v->player,GST_FORMAT_TIME,&position))
    gst_element_seek_simple(v->player,GST_FORMAT_TIME,GST_SEEK_FLAG_FLUSH|GST_SEEK_FLAG_KEY_UNIT,position);
}
static void apply_scaling(Video*v){
  if(!v->sink||!v->crop)return;
  int mode=scaling_mode(v),width=0,height=1;
  if(mode==2){width=MAX(1,gtk_widget_get_allocated_width(v->desktop));height=MAX(1,gtk_widget_get_allocated_height(v->desktop));}
  g_object_set(v->sink,"force-aspect-ratio",mode!=0,NULL);
  if(width!=v->crop_width || height!=v->crop_height){
    char*ratio=g_strdup_printf("%d/%d",width,height);gst_util_set_object_arg(G_OBJECT(v->crop),"aspect-ratio",ratio);g_free(ratio);
    v->crop_width=width;v->crop_height=height;refresh_frame(v);
  }
}
static void allocated(GtkWidget*desktop,GtkAllocation*allocation,gpointer data){apply_scaling(data);}
static void apply(Video *v) {
  if (!v->player)
    return;
  apply_scaling(v);
  g_object_set(
      v->player, "mute",
      display_bool(v,"muted",TRUE),
      "volume", 0.5, NULL);
  gst_element_set_state(v->player,
                        paused(v) ? GST_STATE_PAUSED : GST_STATE_PLAYING);
}
static void changed(XfconfChannel *c, const char *p, const GValue *value,
                    gpointer data) {
  Video*v=data;
  char*file=display_key(v,"file"),*pause_key=display_key(v,"paused"),*mute_key=display_key(v,"muted"),*scale=scaling_key(v);
  if(!strcmp(p,file) && v->prefix){
    v->force_software=FALSE;
    char*prefix=g_strdup(v->prefix),*filename=g_strdup(v->native_filename);
    backdrop(v->desktop,filename,prefix);g_free(prefix);g_free(filename);
  }else if(!strcmp(p,pause_key)||!strcmp(p,mute_key)||!strcmp(p,scale)||!strcmp(p,"/video-wallpaper/paused")||!strcmp(p,"/video-wallpaper/muted"))apply(v);
  else {char*gpu_key=display_key(v,"gpu");if(!strcmp(p,gpu_key)&&v->prefix&&v->native_filename){stop(v);v->force_software=FALSE;char*prefix=g_strdup(v->prefix),*filename=g_strdup(v->native_filename);backdrop(v->desktop,filename,prefix);g_free(prefix);g_free(filename);}g_free(gpu_key);}
  g_free(file);g_free(pause_key);g_free(mute_key);g_free(scale);
}
static void attach(GtkWidget *d) {
  if (g_object_get_data(G_OBJECT(d), "xt-video"))
    return;
  Video *v = g_new0(Video, 1);
  v->desktop = d;
  if(g_object_class_find_property(G_OBJECT_GET_CLASS(d),"monitor")){
    GObject*monitor=NULL;g_object_get(d,"monitor",&monitor,NULL);
    if(monitor){
      if(g_object_class_find_property(G_OBJECT_GET_CLASS(monitor),"identifier"))g_object_get(monitor,"identifier",&v->monitor_id,NULL);
      if((!v->monitor_id || !*v->monitor_id) && g_object_class_find_property(G_OBJECT_GET_CLASS(monitor),"connector")){g_clear_pointer(&v->monitor_id,g_free);g_object_get(monitor,"connector",&v->monitor_id,NULL);}
      g_object_unref(monitor);
    }
  }
  g_object_set_data_full(G_OBJECT(d), "xt-video", v, free_video);
  g_signal_connect(d,"size-allocate",G_CALLBACK(allocated),v);
  g_signal_connect(host->settings(), "property-changed", G_CALLBACK(changed),
                   v);
}
static void detach(GtkWidget *d) {
  g_object_set_data(G_OBJECT(d), "xt-video", NULL);
}
typedef struct { GtkWidget *desktop; char *prefix,*filename; } GlRetry;
static gboolean retry_software_sink(gpointer data){
  GlRetry*r=data;Video*v=g_object_get_data(G_OBJECT(r->desktop),"xt-video");
  if(v&&!v->retired){v->force_software=TRUE;backdrop(r->desktop,r->filename,r->prefix);}
  g_free(r->prefix);g_free(r->filename);g_free(r);return G_SOURCE_REMOVE;
}
static gboolean bus_message(GstBus *bus, GstMessage *m, gpointer data) {
  Video *v = data;
  if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS) {
    gst_element_seek_simple(v->player, GST_FORMAT_TIME,
                            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 0);
    apply(v);
  } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
    GError *error = NULL;
    char *debug = NULL;
    gst_message_parse_error(m, &error, &debug);
    g_warning("Video Wallpaper: %s", error->message);
    g_clear_error(&error);
    g_free(debug);
    if(v->using_gl&&!v->force_software&&v->prefix&&v->native_filename){
      GlRetry*r=g_new0(GlRetry,1);r->desktop=v->desktop;r->prefix=g_strdup(v->prefix);r->filename=g_strdup(v->native_filename);
      v->watch=0;gst_element_set_state(v->player,GST_STATE_NULL);g_idle_add(retry_software_sink,r);return G_SOURCE_REMOVE;
    }
    gst_element_set_state(v->player, GST_STATE_NULL);
  }
  return G_SOURCE_CONTINUE;
}
static gboolean is_video(const char *f) {
  if (!f)
    return FALSE;
  gboolean uncertain;
  char *type = g_content_type_guess(f, NULL, 0, &uncertain);
  gboolean video = type && g_str_has_prefix(type, "video/");
  g_free(type);
  return video;
}
static gboolean backdrop(GtkWidget *d, const char *filename,
                         const char *prefix) {
  Video *v = g_object_get_data(G_OBJECT(d), "xt-video");
  if (!v)
    return FALSE;
  g_free(v->prefix);v->prefix=g_strdup(prefix);
  g_free(v->native_filename);v->native_filename=g_strdup(filename);
  seed_controls(v);
  char*file_key=display_key(v,"file");g_autofree char*selected=xfconf_channel_get_string(host->settings(),file_key,NULL);g_free(file_key);
  if(selected && is_video(selected) && g_file_test(selected,G_FILE_TEST_IS_REGULAR))filename=selected;
  if (!is_video(filename) || !g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
    stop(v);
    return FALSE;
  }
  if (v->player && !g_strcmp0(filename, v->filename)){apply(v);return TRUE;}
  stop(v);
  GstElement *player = gst_element_factory_make("playbin", NULL);
  GstElement *sink = v->force_software?NULL:gst_element_factory_make("gtkglsink", NULL);
  GstElement *sinkbin = sink?gst_element_factory_make("glsinkbin",NULL):NULL;
  gboolean using_gl=sink&&sinkbin;
  if(!using_gl){if(sink)gst_object_unref(sink);if(sinkbin)gst_object_unref(sinkbin);sink=gst_element_factory_make("gtksink",NULL);sinkbin=NULL;}
  if (!player || !sink) {
    if (player) gst_object_unref(player);
    if (sink) gst_object_unref(sink);
    g_warning("Video Wallpaper requires GStreamer playbin and a GTK video sink");
    return FALSE;
  }
  GstElement*crop=gst_element_factory_make("aspectratiocrop",NULL);
  if(!crop){gst_object_unref(player);gst_object_unref(sink);if(sinkbin)gst_object_unref(sinkbin);g_warning("Video Wallpaper requires the GStreamer videocrop plugin");return FALSE;}
  gst_object_ref_sink(crop);
  gst_object_ref_sink(player);
  gst_object_ref_sink(sink);
  if(sinkbin){gst_object_ref_sink(sinkbin);g_object_set(sinkbin,"sink",sink,NULL);}
  g_object_set(sink, "force-aspect-ratio", FALSE, NULL);
  g_object_get(sink, "widget", &v->widget, NULL);
  if (!v->widget) {
    gst_object_unref(player);
    gst_object_unref(sink);
    if(sinkbin)gst_object_unref(sinkbin);
    gst_object_unref(crop);
    return FALSE;
  }
  gtk_widget_set_hexpand(v->widget, TRUE);
  gtk_widget_set_vexpand(v->widget, TRUE);
  gtk_widget_set_halign(v->widget, GTK_ALIGN_FILL);
  gtk_widget_set_valign(v->widget, GTK_ALIGN_FILL);
  char *uri = g_filename_to_uri(filename, NULL, NULL);
  g_object_set(player, "uri", uri, "video-sink", sinkbin?sinkbin:sink,"video-filter",crop, NULL);
  v->sink=gst_object_ref(sink);v->sinkbin=sinkbin?gst_object_ref(sinkbin):NULL;v->using_gl=using_gl;v->crop=crop;
  g_signal_connect(player,"deep-element-added",G_CALLBACK(deep_element_added),v);
  g_free(uri);
  gst_object_unref(sink);
  if(sinkbin)gst_object_unref(sinkbin);
  v->player = player;
  v->filename = g_strdup(filename);
  GstBus *bus = gst_element_get_bus(player);
  v->watch = gst_bus_add_watch(bus, bus_message, v);
  gst_object_unref(bus);
  host->set_backdrop_widget(d, v->widget);
  apply(v);
  return TRUE;
}
static void play(GtkMenuItem *i, Video *v) {
  set_display_bool(v,"paused",FALSE);
  apply(v);
}
static void pause_video(GtkMenuItem *i, Video *v) {
  set_display_bool(v,"paused",TRUE);
}
static void mute(GtkCheckMenuItem *i, Video *v) {
  set_display_bool(v,"muted",gtk_check_menu_item_get_active(i));
}
static void choose(GtkMenuItem *i, Video *v) {
  GtkWidget *dialog = gtk_file_chooser_dialog_new(
      "Choose Video Wallpaper", GTK_WINDOW(v->desktop),
      GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open",
      GTK_RESPONSE_ACCEPT, NULL);
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "Video files");
  gtk_file_filter_add_mime_type(filter, "video/*");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
  if(v->filename)gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog),v->filename);
  v->chooser = dialog;
  int response = gtk_dialog_run(GTK_DIALOG(dialog));
  v->chooser = NULL;
  if (response == GTK_RESPONSE_ACCEPT && !v->retired && v->prefix) {
    char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    char *k = display_key(v,"file");
    xfconf_channel_set_string(host->settings(), k, f);
    g_free(f);
    g_free(k);
  }
  gtk_widget_destroy(dialog);
  if (v->retired)
    free_video(v);
}
static void item(GtkWidget *m, const char *label, GCallback callback, Video *v,
                 gboolean sensitive) {
  GtkWidget *i = gtk_menu_item_new_with_label(label);
  gtk_widget_set_sensitive(i, sensitive);
  g_signal_connect(i, "activate", callback, v);
  gtk_menu_shell_append(GTK_MENU_SHELL(m), i);
}
static void scaling_changed(GtkCheckMenuItem*item,Video*v){
  if(!gtk_check_menu_item_get_active(item))return;
  const char*mode=g_object_get_data(G_OBJECT(item),"scaling-mode");char*key=scaling_key(v);
  xfconf_channel_set_string(host->settings(),key,mode);g_free(key);refresh_frame(v);
}
static void gpu_changed(GtkCheckMenuItem*item,Video*v){
  if(!gtk_check_menu_item_get_active(item))return;
  const char*mode=g_object_get_data(G_OBJECT(item),"gpu-mode");char*key=display_key(v,"gpu");
  xfconf_channel_set_string(host->settings(),key,mode);g_free(key);
}
static void menu(GtkWidget *d, GtkWidget *m) {
  Video *v = g_object_get_data(G_OBJECT(d), "xt-video");
  if (!v)
    return;
  GtkWidget *root = gtk_menu_item_new_with_label("Video Wallpaper"),
            *sub = gtk_menu_new();
  v->menus = g_list_prepend(v->menus, root);
  g_signal_connect(root, "destroy", G_CALLBACK(menu_destroyed), v);
  item(sub, "Play", G_CALLBACK(play), v, v->player != NULL);
  item(sub, "Pause", G_CALLBACK(pause_video), v, v->player != NULL);
  GtkWidget *check = gtk_check_menu_item_new_with_label("Mute");
  gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(check),
      display_bool(v,"muted",TRUE));
  g_signal_connect(check, "toggled", G_CALLBACK(mute), v);
  gtk_menu_shell_append(GTK_MENU_SHELL(sub), check);
  GtkWidget*scaling=gtk_menu_item_new_with_label("Scaling"),*options=gtk_menu_new();
  const char*labels[]={"Stretch to display","Fit whole video (letterbox)","Fill display (crop, keep proportions)"};
  const char*modes[]={"stretch","fit","fill"};GSList*group=NULL;int current=scaling_mode(v);
  for(int n=0;n<3;n++){
    GtkWidget*option=gtk_radio_menu_item_new_with_label(group,labels[n]);group=gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(option));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option),n==current);
    g_object_set_data(G_OBJECT(option),"scaling-mode",(gpointer)modes[n]);g_signal_connect(option,"toggled",G_CALLBACK(scaling_changed),v);
    gtk_menu_shell_append(GTK_MENU_SHELL(options),option);
  }
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(scaling),options);gtk_menu_shell_append(GTK_MENU_SHELL(sub),scaling);
  GtkWidget*gpu=gtk_menu_item_new_with_label("Video Decode GPU"),*gpu_options=gtk_menu_new();
  const char*gpu_labels[]={"Automatic","Intel VA-API","NVIDIA NVDEC"};const char*gpu_modes[]={"auto","intel","nvidia"};char*current_gpu=display_string(v,"gpu","auto");GSList*gpu_group=NULL;
  for(int n=0;n<3;n++){GtkWidget*option=gtk_radio_menu_item_new_with_label(gpu_group,gpu_labels[n]);gpu_group=gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(option));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option),!strcmp(current_gpu,gpu_modes[n]));g_object_set_data(G_OBJECT(option),"gpu-mode",(gpointer)gpu_modes[n]);g_signal_connect(option,"toggled",G_CALLBACK(gpu_changed),v);gtk_menu_shell_append(GTK_MENU_SHELL(gpu_options),option);}
  g_free(current_gpu);gtk_menu_item_set_submenu(GTK_MENU_ITEM(gpu),gpu_options);gtk_menu_shell_append(GTK_MENU_SHELL(sub),gpu);
  item(sub, "Choose Video…", G_CALLBACK(choose), v, TRUE);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(root), sub);
  gtk_menu_shell_prepend(GTK_MENU_SHELL(m), root);
  gtk_widget_show_all(root);
}
static const XtTweak tweak = {.abi_version = XT_ABI_VERSION,
                              .struct_size = sizeof(XtTweak),
                              .id = "video-wallpaper",
                              .name = "Video Wallpaper",
                              .capabilities = XT_CAP_BACKDROP_PROVIDER,
                              .attach_desktop = attach,
                              .detach_desktop = detach,
                              .backdrop_changed = backdrop,
                              .desktop_menu = menu};
G_MODULE_EXPORT const XtTweak *xfce_tweak_query(const XtHost *api) {
  if (!api || api->abi_version != XT_ABI_VERSION ||
      api->struct_size < sizeof(XtHost))
    return NULL;
  GError *error = NULL;
  if (!gst_init_check(NULL, NULL, &error)) {
    g_clear_error(&error);
    return NULL;
  }
  host = api;
  return &tweak;
}
