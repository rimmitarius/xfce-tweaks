/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "xfce-tweaks.h"

#include <dlfcn.h>
#include <math.h>
#include <string.h>
#include <glib/gstdio.h>
#include <time.h>
static const XtHost *host;
static char *assets;
static GList *views;
static guint timer;
static gulong appearance_watch;
static GdkPixbuf *background_image;
static char *background_filename;

typedef struct { GtkWidget *view; char *key; int x,y,width,height,original_width,original_height; gboolean hidden;gpointer preview,drop_target;int preview_x,preview_y; gboolean positioned,drag,resizing,moved,resize_cursor,running,hovered,opacity_ready; double opacity,fade_from,fade_target; gint64 fade_start; guint fade_source; double start_x,start_y; int original_x,original_y; GSubprocess *controls; } Widget;

static GKeyFile *weather;
static GSubprocess *fetch;
static gint64 weather_due;
static gboolean refresh(gpointer unused);
static double setting(const char*name,double fallback) {
    char*key=g_strconcat(PREFIX "/appearance/",name,NULL);double value=xfconf_channel_get_double(host->settings(),key,fallback);g_free(key);return isfinite(value)?value:fallback;
}
static GdkRectangle widget_preview_rect(Widget*w);
static GdkRectangle rect(Widget*w) {
    if(w->preview)return widget_preview_rect(w);
    int width=gtk_widget_get_allocated_width(w->view),height=gtk_widget_get_allocated_height(w->view);
    int rw=MIN(width,CLAMP(w->width>0?w->width:(int)setting("width",320),200,1000)),rh=MIN(height,CLAMP(w->height>0?w->height:(int)setting("height",180),110,1000));
    return (GdkRectangle){CLAMP(w->positioned?w->x:width-rw-24,0,MAX(0,width-rw)),CLAMP(w->positioned?w->y:DEFAULT_Y,0,MAX(0,height-rh)),rw,rh};
}
#include "widget-transfer.h"
static gboolean fade_tick(gpointer data){
    Widget*w=data;double progress=MIN(1.,(g_get_monotonic_time()-w->fade_start)/180000.);
    w->opacity=w->fade_from+(w->fade_target-w->fade_from)*progress;gtk_widget_queue_draw(w->view);
    if(progress>=1){w->fade_source=0;return G_SOURCE_REMOVE;}return G_SOURCE_CONTINUE;
}
static void hover(Widget*w,gboolean over){
    if(w->hovered==over)return;
    if(!w->opacity_ready){w->opacity=CLAMP(setting("opacity",.85),0,1);w->opacity_ready=TRUE;}
    w->hovered=over;w->fade_from=w->opacity;w->fade_target=CLAMP(setting(over?"hover-enter":"hover-exit",setting("opacity",.85)),0,1);w->fade_start=g_get_monotonic_time();
    if(!w->fade_source)w->fade_source=g_timeout_add(16,fade_tick,w);
}
static void appearance_changed(XfconfChannel*c,const char*property,const GValue*value,gpointer unused) {
    if(g_str_has_prefix(property,PREFIX "/monitors/")){
        for(GList*l=views;l;l=l->next){Widget*w=l->data;
            if(g_str_has_prefix(property,w->key)){
                const char*suffix=property+strlen(w->key);
                if(!strcmp(suffix,"/x")){w->x=xfconf_channel_get_int(c,property,0);w->positioned=TRUE;}
                else if(!strcmp(suffix,"/y"))w->y=xfconf_channel_get_int(c,property,0);
                else if(!strcmp(suffix,"/hidden"))w->hidden=xfconf_channel_get_bool(c,property,FALSE);
                else if(!strcmp(suffix,"/width"))w->width=xfconf_channel_get_int(c,property,0);
                else if(!strcmp(suffix,"/height"))w->height=xfconf_channel_get_int(c,property,0);
                gtk_widget_queue_draw(w->view);
            }
        }
        return;
    }
    if(!strcmp(property,PREFIX "/weather/longitude") || !strcmp(property,PREFIX "/weather/units")){if(fetch){g_subprocess_force_exit(fetch);g_clear_object(&fetch);}weather_due=0;refresh(NULL);}
    if(!g_str_has_prefix(property,PREFIX "/appearance/") && !g_str_has_prefix(property,PREFIX "/clock/"))return;
    for(GList*l=views;l;l=l->next){Widget*w=l->data;w->opacity=CLAMP(setting(w->hovered?"hover-enter":"opacity",setting("opacity",.85)),0,1);w->opacity_ready=TRUE;if(w->fade_source){g_source_remove(w->fade_source);w->fade_source=0;}gtk_widget_queue_draw(w->view);}
}
static void render_background(Widget*w,GtkStyleContext*style,cairo_t*cr,GdkRectangle r,GdkRGBA*ink,gboolean*custom) {
    XfconfChannel*c=host->settings();const char*prefix=PREFIX "/appearance/";
    char*key=g_strconcat(prefix,"color",NULL);char*color=xfconf_channel_get_string(c,key,"");g_free(key);
    key=g_strconcat(prefix,"image",NULL);char*filename=xfconf_channel_get_string(c,key,"");g_free(key);
    if(!w->opacity_ready){w->opacity=CLAMP(setting("opacity",.85),0,1);w->opacity_ready=TRUE;}double opacity=w->opacity;
    GdkRGBA bg;*custom=gdk_rgba_parse(&bg,color);if(*custom){double lum=.2126*bg.red+.7152*bg.green+.0722*bg.blue;gdk_rgba_parse(ink,lum<.5?"#ffffff":"#151515");}
    if(g_strcmp0(filename,background_filename)){
        g_free(background_filename);background_filename=g_strdup(filename);g_clear_object(&background_image);
        if(*filename)background_image=gdk_pixbuf_new_from_file_at_scale(filename,1024,1024,TRUE,NULL);
    }
    cairo_save(cr);
    const double radius=CLAMP(setting("radius",12),0,MIN(r.width,r.height)/2.);
    cairo_new_path(cr);cairo_arc(cr,r.x+r.width-radius,r.y+radius,radius,-G_PI/2,0);
    cairo_arc(cr,r.x+r.width-radius,r.y+r.height-radius,radius,0,G_PI/2);
    cairo_arc(cr,r.x+radius,r.y+r.height-radius,radius,G_PI/2,G_PI);
    cairo_arc(cr,r.x+radius,r.y+radius,radius,G_PI,3*G_PI/2);cairo_close_path(cr);cairo_path_t*outline=cairo_copy_path(cr);cairo_clip(cr);
    cairo_push_group(cr);
    if(!*custom && !gtk_style_context_lookup_color(style,"theme_bg_color",&bg))gdk_rgba_parse(&bg,"#f6f5f4");
    gdk_cairo_set_source_rgba(cr,&bg);cairo_paint(cr);
    if(background_image){
        double w=gdk_pixbuf_get_width(background_image),h=gdk_pixbuf_get_height(background_image),scale=MAX(r.width/w,r.height/h);
        cairo_save(cr);cairo_translate(cr,r.x+(r.width-w*scale)/2,r.y+(r.height-h*scale)/2);cairo_scale(cr,scale,scale);
        gdk_cairo_set_source_pixbuf(cr,background_image,0,0);cairo_paint(cr);cairo_restore(cr);
    }
    cairo_pop_group_to_source(cr);cairo_paint_with_alpha(cr,opacity);
    double border=CLAMP(setting("border-width",1),0,20);
    if(border>0){
        key=g_strconcat(prefix,"border-color",NULL);char*border_text=xfconf_channel_get_string(c,key,"rgba(255,255,255,0.45)");g_free(key);
        GdkRGBA border_color;if(!gdk_rgba_parse(&border_color,border_text))gdk_rgba_parse(&border_color,"#808080");g_free(border_text);
        cairo_append_path(cr,outline);gdk_cairo_set_source_rgba(cr,&border_color);cairo_set_line_width(cr,border*2);cairo_stroke(cr);
    }
    cairo_path_destroy(outline);cairo_restore(cr);
    key=g_strconcat(prefix,"text-color",NULL);char*text=xfconf_channel_get_string(c,key,"");g_free(key);
    GdkRGBA chosen;if(gdk_rgba_parse(&chosen,text)){*ink=chosen;*custom=TRUE;}
    g_free(text);g_free(color);g_free(filename);
}
static void text(GtkWidget*view,cairo_t*cr,const char*value,double x,double y,int width,double scale,gboolean bold,PangoAlignment alignment,GdkRGBA color){
    PangoLayout*layout=gtk_widget_create_pango_layout(view,value);pango_layout_set_width(layout,width*PANGO_SCALE);pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_END);pango_layout_set_alignment(layout,alignment);
    PangoAttrList*attrs=pango_attr_list_new();pango_attr_list_insert(attrs,pango_attr_scale_new(scale*CLAMP(setting("text-scale",1),.5,2)));if(bold)pango_attr_list_insert(attrs,pango_attr_weight_new(PANGO_WEIGHT_BOLD));pango_layout_set_attributes(layout,attrs);pango_attr_list_unref(attrs);
    char *shadow_key=g_strconcat(PREFIX "/appearance/text-shadow-color",NULL);
    char *shadow_text=xfconf_channel_get_string(host->settings(),shadow_key,"#000000");g_free(shadow_key);
    GdkRGBA shadow={0,0,0,1};gdk_rgba_parse(&shadow,shadow_text);g_free(shadow_text);
    double strength=CLAMP(setting("text-shadow",.30),0,1);shadow.alpha*=strength;
    if(strength>0){gdk_cairo_set_source_rgba(cr,&shadow);for(int sy=-1;sy<=1;sy++)for(int sx=-1;sx<=1;sx++)if(sx||sy){cairo_move_to(cr,x+sx,y+sy);pango_cairo_show_layout(cr,layout);}}
    gdk_cairo_set_source_rgba(cr,&color);cairo_move_to(cr,x,y);pango_cairo_show_layout(cr,layout);g_object_unref(layout);
}

#include "content.h"
static gboolean inside(GdkRectangle r,double x,double y) {return x>=r.x && y>=r.y && x<r.x+r.width && y<r.y+r.height;}
static gboolean resize_corner(GdkRectangle r,double x,double y){return inside(r,x,y)&&x>=r.x+r.width-24&&y>=r.y+r.height-24;}
static void resize_cursor(Widget*w,gboolean enabled){
    if(w->resize_cursor==enabled)return;
    GdkWindow*window=gtk_widget_get_window(w->view);if(!window)return;
    GdkCursor*cursor=enabled?gdk_cursor_new_from_name(gtk_widget_get_display(w->view),"se-resize"):NULL;gdk_window_set_cursor(window,cursor);g_clear_object(&cursor);w->resize_cursor=enabled;
}
static gboolean press(GtkWidget*view,GdkEventButton*e) {
    Widget*w=g_object_get_data(G_OBJECT(view),WIDGET_ID);if(!w||widget_hidden(w)||e->button!=1)return FALSE;
    GdkRectangle r=rect(w);if(!inside(r,e->x,e->y))return FALSE;hover(w,TRUE);
    w->drag=TRUE;w->moved=FALSE;w->resizing=resize_corner(r,e->x,e->y);w->start_x=e->x;w->start_y=e->y;w->original_x=r.x;w->original_y=r.y;w->original_width=r.width;w->original_height=r.height;
    if(w->resizing){w->positioned=TRUE;w->x=r.x;w->y=r.y;resize_cursor(w,TRUE);}gtk_grab_add(view);return TRUE;
}
static gboolean motion(GtkWidget*view,GdkEventMotion*e) {
    Widget*w=g_object_get_data(G_OBJECT(view),WIDGET_ID);if(!w||(!w->drag&&widget_hidden(w)))return FALSE;hover(w,inside(rect(w),e->x,e->y));resize_cursor(w,w->resizing||resize_corner(rect(w),e->x,e->y));if(!w->drag)return FALSE;
    if(w->resizing){
        w->width=CLAMP(w->original_width+(int)(e->x-w->start_x),MIN(200,gtk_widget_get_allocated_width(view)),MAX(200,gtk_widget_get_allocated_width(view)-w->x));w->height=CLAMP(w->original_height+(int)(e->y-w->start_y),110,MAX(110,gtk_widget_get_allocated_height(view)-w->y));w->moved=TRUE;hover(w,inside(rect(w),e->x,e->y));gtk_widget_queue_draw(view);return TRUE;
    }
    if(fabs(e->x-w->start_x)+fabs(e->y-w->start_y)>5) {
        w->moved=TRUE;widget_drag_preview(w,e->x_root,e->y_root);w->positioned=TRUE;w->x=w->original_x+e->x-w->start_x;w->y=w->original_y+e->y-w->start_y;
        GdkRectangle r=rect(w);w->x=r.x;w->y=r.y;gtk_widget_queue_draw(view);
    }
    return TRUE;
}
static void controls_exited(GObject*object,GAsyncResult*result,gpointer data) {
    GError*error=NULL;g_subprocess_wait_finish(G_SUBPROCESS(object),result,&error);g_clear_error(&error);
    GWeakRef*weak=data;GObject*view=g_weak_ref_get(weak);
    if(view){Widget*w=g_object_get_data(view,WIDGET_ID);if(w && w->controls==G_SUBPROCESS(object))w->running=FALSE;g_object_unref(view);}
    g_weak_ref_clear(weak);g_free(weak);
}
static gboolean release(GtkWidget*view,GdkEventButton*e) {
    Widget*w=g_object_get_data(G_OBJECT(view),WIDGET_ID);if(!w||!w->drag||e->button!=1)return FALSE;
    if(!w->resizing && (w->moved || fabs(e->x-w->start_x)+fabs(e->y-w->start_y)>5)){
        w->moved=TRUE;widget_drag_preview(w,e->x_root,e->y_root);
        if(widget_finish_transfer(w)){w->drag=FALSE;gtk_grab_remove(view);resize_cursor(w,FALSE);return TRUE;}
    }
    gboolean resized=w->resizing;w->drag=w->resizing=FALSE;gtk_grab_remove(view);resize_cursor(w,resize_corner(rect(w),e->x,e->y));
    if(resized||w->moved) {
        if(w->hidden){w->hidden=FALSE;char*k=g_strconcat(w->key,"/hidden",NULL);xfconf_channel_set_bool(host->settings(),k,FALSE);g_free(k);}
        char*key=g_strconcat(w->key,"/x",NULL);xfconf_channel_set_int(host->settings(),key,w->x);g_free(key);
        key=g_strconcat(w->key,"/y",NULL);xfconf_channel_set_int(host->settings(),key,w->y);g_free(key);
        if(resized){int width=w->width?w->width:w->original_width,height=w->height?w->height:w->original_height;key=g_strconcat(w->key,"/width",NULL);xfconf_channel_set_int(host->settings(),key,width);g_free(key);key=g_strconcat(w->key,"/height",NULL);xfconf_channel_set_int(host->settings(),key,height);g_free(key);}
    } else {
        gboolean running=w->running;
        if(!running){g_clear_object(&w->controls);char*app=g_build_filename(assets,"app.py",NULL);GError*error=NULL;
            w->controls=g_subprocess_new(G_SUBPROCESS_FLAGS_NONE,&error,"python3",app,"--id",WIDGET_ID,"--monitor-key",w->key,NULL);g_free(app);
            if(w->controls){w->running=TRUE;GWeakRef*weak=g_new0(GWeakRef,1);g_weak_ref_init(weak,G_OBJECT(view));g_subprocess_wait_async(w->controls,NULL,controls_exited,weak);}
            if(error){g_warning("Victus controls: %s",error->message);g_clear_error(&error);}}
    }
    return TRUE;
}
static gboolean draw_cb(GtkWidget*v,cairo_t*cr,gpointer data){cairo_save(cr);draw(v,cr);cairo_restore(cr);return FALSE;}
static gboolean press_cb(GtkWidget*v,GdkEventButton*e,gpointer data){return press(v,e);}
static gboolean release_cb(GtkWidget*v,GdkEventButton*e,gpointer data){return release(v,e);}
static gboolean leave_cb(GtkWidget*v,GdkEventCrossing*e,gpointer data){Widget*w=data;hover(w,FALSE);if(!w->resizing)resize_cursor(w,FALSE);return FALSE;}
static gboolean motion_cb(GtkWidget*v,GdkEventMotion*e,gpointer data){return motion(v,e);}
static gboolean widget_surface(GtkWidget*view,double x,double y,gpointer data){Widget*w=data;return !widget_hidden(w)&&inside(rect(w),x,y);}
static void free_widget(gpointer data) {
    Widget*w=data;widget_clear_transfer(w);xt_surface_unregister(w->view,WIDGET_ID);if(w->drag && gtk_widget_has_grab(w->view))gtk_grab_remove(w->view);
    resize_cursor(w,FALSE);
    if(w->fade_source)g_source_remove(w->fade_source);
    if(w->controls){g_subprocess_force_exit(w->controls);g_clear_object(&w->controls);}
    g_signal_handlers_disconnect_by_data(w->view,w);
    views=g_list_remove(views,w);g_free(w->key);g_free(w);
    if(!views && timer){g_source_remove(timer);timer=0;weather_due=0;g_clear_object(&background_image);g_clear_pointer(&background_filename,g_free);if(fetch){g_subprocess_force_exit(fetch);g_clear_object(&fetch);}g_clear_pointer(&weather,g_key_file_unref);weather_due=0;if(appearance_watch){g_signal_handler_disconnect(host->settings(),appearance_watch);appearance_watch=0;}}
}
static void attach(GtkWidget*view,const char*monitor) {
    if(g_object_get_data(G_OBJECT(view),WIDGET_ID))return;
    Widget*w=g_new0(Widget,1);w->view=view;
    char*id=g_compute_checksum_for_string(G_CHECKSUM_SHA256,monitor?monitor:"default",-1);
    w->key=g_strdup_printf(PREFIX "/monitors/%s",id);g_free(id);
    char*key=g_strconcat(w->key,"/x",NULL);w->positioned=xfconf_channel_has_property(host->settings(),key);w->x=xfconf_channel_get_int(host->settings(),key,0);g_free(key);
    key=g_strconcat(w->key,"/y",NULL);w->y=xfconf_channel_get_int(host->settings(),key,24);g_free(key);
    key=g_strconcat(w->key,"/width",NULL);w->width=xfconf_channel_get_int(host->settings(),key,0);g_free(key);
    key=g_strconcat(w->key,"/height",NULL);w->height=xfconf_channel_get_int(host->settings(),key,0);g_free(key);
    key=g_strconcat(w->key,"/hidden",NULL);w->hidden=xfconf_channel_get_bool(host->settings(),key,FALSE);g_free(key);
    g_object_set_data_full(G_OBJECT(view),WIDGET_ID,w,free_widget);
    xt_surface_register(view,WIDGET_ID,widget_surface,w);views=g_list_prepend(views,w);
    for(GList*l=views;l;l=l->next)gtk_widget_queue_draw(((Widget*)l->data)->view);
    g_signal_connect_after(view,"draw",G_CALLBACK(draw_cb),w);
    g_signal_connect(view,"button-press-event",G_CALLBACK(press_cb),w);
    g_signal_connect(view,"button-release-event",G_CALLBACK(release_cb),w);
    g_signal_connect(view,"motion-notify-event",G_CALLBACK(motion_cb),w);
    g_signal_connect(view,"leave-notify-event",G_CALLBACK(leave_cb),w);
    if(!appearance_watch)appearance_watch=g_signal_connect(host->settings(),"property-changed",G_CALLBACK(appearance_changed),NULL);
    if(!timer){refresh(NULL);timer=g_timeout_add_seconds(IS_WEATHER?60:1,refresh,NULL);}
}
static void detach(GtkWidget*view) {g_object_set_data(G_OBJECT(view),WIDGET_ID,NULL);gtk_widget_queue_draw(view);}
static const XtTweak tweak={.abi_version=XT_ABI_VERSION,.struct_size=sizeof(XtTweak),.id=WIDGET_ID,.name=WIDGET_NAME,
 .attach_view=attach,.detach_view=detach};
G_MODULE_EXPORT const XtTweak *xfce_tweak_query(const XtHost*api) {
    if(!api||api->abi_version!=XT_ABI_VERSION||api->struct_size<sizeof(XtHost))return NULL;
    Dl_info info;if(!dladdr((void*)xfce_tweak_query,&info))return NULL;
    char*directory=g_path_get_dirname(info.dli_fname);g_free(assets);assets=g_build_filename(directory,"assets",NULL);g_free(directory);
    host=api;return &tweak;
}
