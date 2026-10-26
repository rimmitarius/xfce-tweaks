/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "xfce-tweaks.h"
#include "widget-layout.h"
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
static double watts=-1;
static int fan_mode=-1,fan_request=-1;
static GHashTable*icon_cache;
static gulong theme_watch;
static gboolean discharging;
typedef struct { GtkWidget *view; char *key; int x,y,width,height,original_width,original_height; gboolean hidden;gpointer preview,drop_target;int preview_x,preview_y; gboolean positioned,drag,resizing,moved,resize_cursor,running,hovered,opacity_ready; double opacity,fade_from,fade_target; gint64 fade_start; guint fade_source; double start_x,start_y; int original_x,original_y; GSubprocess *controls; } Widget;
static char *profile;
static int fan1=-1,fan2=-1,temperature=-1,battery=-1;
static char *sys_path(const char *name) { const char *root=g_getenv("XFCE_TWEAKS_TEST_SYSFS");return g_build_filename(root?root:"/sys",name,NULL); }
static char *read_path(const char *path) { char *s=NULL;if(!g_file_get_contents(path,&s,NULL,NULL))return NULL;g_strstrip(s);return s; }
static char *read_sys(const char *name) { char*p=sys_path(name);char*s=read_path(p);g_free(p);return s; }
static int number(const char *path) { char*s=read_path(path);if(!s)return -1;char*end;long n=strtol(s,&end,10);int value=(*end=='\0' && n>=0 && n<=G_MAXINT)?n:-1;g_free(s);return value; }
static gboolean refresh(gpointer unused) {
    fan1=fan2=temperature=battery=-1;watts=-1;discharging=FALSE;fan_mode=fan_request=-1;g_free(profile);profile=read_sys("firmware/acpi/platform_profile");
    char *base=sys_path("class/hwmon");GDir*d=g_dir_open(base,0,NULL);const char*n;
    if(d)while((n=g_dir_read_name(d))) {
        char *dir=g_build_filename(base,n,NULL),*path=g_build_filename(dir,"name",NULL),*name=read_path(path);g_free(path);
        if(!g_strcmp0(name,"hp")) {
            path=g_build_filename(dir,"fan1_input",NULL);fan1=number(path);g_free(path);
            path=g_build_filename(dir,"fan2_input",NULL);fan2=number(path);g_free(path);
            path=g_build_filename(dir,"pwm1_enable",NULL);fan_mode=number(path);g_free(path);
        } else if(!g_strcmp0(name,"coretemp")) {
            GDir*td=g_dir_open(dir,0,NULL);const char*t;
            if(td)while((t=g_dir_read_name(td)))if(g_str_has_prefix(t,"temp") && g_str_has_suffix(t,"_input")) {
                path=g_build_filename(dir,t,NULL);int v=number(path);g_free(path);temperature=MAX(temperature,v);
            }
            if(td)g_dir_close(td);
        }
        g_free(name);g_free(dir);
    }
    if(d)g_dir_close(d);g_free(base);
    base=sys_path("class/power_supply");d=g_dir_open(base,0,NULL);
    if(d)while((n=g_dir_read_name(d))) {
        char *path=g_build_filename(base,n,"type",NULL),*type=read_path(path);g_free(path);
        if(!g_strcmp0(type,"Battery")){
            path=g_build_filename(base,n,"capacity",NULL);battery=number(path);g_free(path);
            path=g_build_filename(base,n,"status",NULL);char*status=read_path(path);g_free(path);discharging=!g_strcmp0(status,"Discharging");g_free(status);
            if(discharging){
                path=g_build_filename(base,n,"power_now",NULL);int power=number(path);g_free(path);
                if(power>=0)watts=power/1000000.0;
                else {path=g_build_filename(base,n,"current_now",NULL);int current_ua=number(path);g_free(path);
                    path=g_build_filename(base,n,"voltage_now",NULL);int voltage_uv=number(path);g_free(path);
                    if(current_ua>=0 && voltage_uv>=0)watts=(double)current_ua*voltage_uv/1e12;}
            }
        }
        g_free(type);
    }
    if(d)g_dir_close(d);g_free(base);
    if(fan_mode==1){GStatBuf status;if(!g_stat("/run/xfce-tweaks/fan-request",&status) && time(NULL)-status.st_mtime<=12)fan_request=number("/run/xfce-tweaks/fan-request");if(fan_request<0||fan_request>100)fan_request=-1;}
    for(GList*l=views;l;l=l->next)gtk_widget_queue_draw(((Widget*)l->data)->view);
    return G_SOURCE_CONTINUE;
}
static double setting(const char*name,double fallback) {
    char*key=g_strconcat("/xfce-tweaks/victus-control/appearance/",name,NULL);double value=xfconf_channel_get_double(host->settings(),key,fallback);g_free(key);return isfinite(value)?value:fallback;
}
static GdkRectangle widget_preview_rect(Widget*w);
static GdkRectangle rect(Widget*w) {
    if(w->preview)return widget_preview_rect(w);
    int width=gtk_widget_get_allocated_width(w->view),height=gtk_widget_get_allocated_height(w->view);
    int rw=MIN(width,CLAMP(w->width>0?w->width:(int)setting("width",360),200,1000)),rh=MIN(height,CLAMP(w->height>0?w->height:(int)setting("height",320),110,1000));
    return (GdkRectangle){CLAMP(w->positioned?w->x:width-rw-24,0,MAX(0,width-rw)),CLAMP(w->positioned?w->y:24,0,MAX(0,height-rh)),rw,rh};
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
    if(g_str_has_prefix(property,"/xfce-tweaks/victus-control/monitors/")){
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
    if(!g_str_has_prefix(property,"/xfce-tweaks/victus-control/appearance/"))return;
    for(GList*l=views;l;l=l->next){Widget*w=l->data;w->opacity=CLAMP(setting(w->hovered?"hover-enter":"opacity",setting("opacity",.85)),0,1);w->opacity_ready=TRUE;if(w->fade_source){g_source_remove(w->fade_source);w->fade_source=0;}gtk_widget_queue_draw(w->view);}
}
static void render_background(Widget*w,GtkStyleContext*style,cairo_t*cr,GdkRectangle r,GdkRGBA*ink,gboolean*custom) {
    XfconfChannel*c=host->settings();const char*prefix="/xfce-tweaks/victus-control/appearance/";
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
    if(!*custom && !gtk_style_context_lookup_color(style,"theme_bg_color",&bg))gdk_rgba_parse(&bg,"#f6f5f4");gdk_cairo_set_source_rgba(cr,&bg);cairo_paint(cr);
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
    PangoAttrList*attrs=pango_attr_list_new();pango_attr_list_insert(attrs,pango_attr_scale_new(scale));if(bold)pango_attr_list_insert(attrs,pango_attr_weight_new(PANGO_WEIGHT_BOLD));pango_layout_set_attributes(layout,attrs);pango_attr_list_unref(attrs);
    char*shadow_text=xfconf_channel_get_string(host->settings(),"/xfce-tweaks/victus-control/appearance/text-shadow-color","#000000");GdkRGBA shadow={0,0,0,1};gdk_rgba_parse(&shadow,shadow_text);g_free(shadow_text);double strength=CLAMP(setting("text-shadow",.30),0,1);shadow.alpha*=strength;
    if(strength>0){gdk_cairo_set_source_rgba(cr,&shadow);for(int sy=-1;sy<=1;sy++)for(int sx=-1;sx<=1;sx++)if(sx||sy){cairo_move_to(cr,x+sx,y+sy);pango_cairo_show_layout(cr,layout);}}
    gdk_cairo_set_source_rgba(cr,&color);cairo_move_to(cr,x,y);pango_cairo_show_layout(cr,layout);g_object_unref(layout);
}
static void icon_theme_changed(GtkIconTheme*theme,gpointer unused){if(icon_cache)g_hash_table_remove_all(icon_cache);for(GList*l=views;l;l=l->next)gtk_widget_queue_draw(((Widget*)l->data)->view);}
static void icon(cairo_t*cr,const char*name,double x,double y,int size,GdkRGBA ink){
    cairo_new_path(cr);
    if(!strcmp(name,"fan-symbolic")){
        cairo_save(cr);cairo_translate(cr,x+size/2.,y+size/2.);cairo_scale(cr,size/24.,size/24.);gdk_cairo_set_source_rgba(cr,&ink);
        for(int i=0;i<3;i++){cairo_move_to(cr,1,-2);cairo_curve_to(cr,-1,-10,7,-13,9,-6);cairo_curve_to(cr,10,-2,5,0,1,-2);cairo_fill(cr);cairo_rotate(cr,2*G_PI/3);}
        cairo_arc(cr,0,0,2,0,2*G_PI);cairo_fill(cr);cairo_restore(cr);return;
    }
    if(!strcmp(name,"temperature-symbolic")){
        cairo_save(cr);cairo_translate(cr,x,y);cairo_scale(cr,size/24.,size/24.);gdk_cairo_set_source_rgba(cr,&ink);cairo_set_line_width(cr,2);cairo_arc(cr,12,17,4,0,2*G_PI);cairo_stroke(cr);cairo_move_to(cr,9,14);cairo_line_to(cr,9,5);cairo_arc(cr,12,5,3,G_PI,2*G_PI);cairo_line_to(cr,15,14);cairo_stroke(cr);cairo_move_to(cr,12,17);cairo_line_to(cr,12,9);cairo_stroke(cr);cairo_restore(cr);return;
    }
    if(!strcmp(name,"widget-power")){
        cairo_save(cr);cairo_translate(cr,x,y);cairo_scale(cr,size/24.,size/24.);gdk_cairo_set_source_rgba(cr,&ink);cairo_move_to(cr,14,1);cairo_line_to(cr,5,13);cairo_line_to(cr,11,13);cairo_line_to(cr,9,23);cairo_line_to(cr,20,10);cairo_line_to(cr,13,10);cairo_close_path(cr);cairo_fill(cr);cairo_restore(cr);return;
    }
    if(!icon_cache)icon_cache=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_object_unref);
    char*key=g_strdup_printf("%s/%d/%.3f/%.3f/%.3f",name,size,ink.red,ink.green,ink.blue);GdkPixbuf*pixbuf=g_hash_table_lookup(icon_cache,key);
    if(!pixbuf){GtkIconInfo*info=gtk_icon_theme_lookup_icon(gtk_icon_theme_get_default(),name,size,GTK_ICON_LOOKUP_FORCE_SYMBOLIC);if(info){pixbuf=gtk_icon_info_load_symbolic(info,&ink,NULL,NULL,NULL,NULL,NULL);g_object_unref(info);}if(pixbuf)g_hash_table_insert(icon_cache,g_strdup(key),pixbuf);}
    if(pixbuf){gdk_cairo_set_source_pixbuf(cr,pixbuf,x,y);cairo_paint_with_alpha(cr,ink.alpha);}g_free(key);
}
static void gauge(cairo_t*cr,double x,double y,double fraction,GdkRGBA ink,GdkRGBA accent){
    cairo_new_path(cr);
    cairo_set_line_width(cr,7);cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);GdkRGBA track=ink;track.alpha=.12;gdk_cairo_set_source_rgba(cr,&track);cairo_arc(cr,x,y,43,G_PI*.75,G_PI*2.25);cairo_stroke(cr);
    GdkRGBA tick=ink;tick.alpha=.3;gdk_cairo_set_source_rgba(cr,&tick);cairo_set_line_width(cr,1);
    for(int i=0;i<=4;i++){double angle=G_PI*(.75+1.5*i/4.);cairo_move_to(cr,x+cos(angle)*52,y+sin(angle)*52);cairo_line_to(cr,x+cos(angle)*55,y+sin(angle)*55);cairo_stroke(cr);}
    cairo_set_line_width(cr,7);
    if(fraction>=0){gdk_cairo_set_source_rgba(cr,&accent);cairo_arc(cr,x,y,43,G_PI*.75,G_PI*(.75+1.5*CLAMP(fraction,0,1)));cairo_stroke(cr);}
}
static void small_gauge(cairo_t*cr,double x,double y,double scale,double fraction,GdkRGBA ink,GdkRGBA accent){cairo_save(cr);cairo_translate(cr,x,y);cairo_scale(cr,scale,scale);gauge(cr,0,0,fraction,ink,accent);cairo_restore(cr);}
static void battery_icon(cairo_t*cr,double x,double y,int level,GdkRGBA ink,GdkRGBA accent){
    GdkRGBA outline=ink;outline.alpha=.65;if(level>=0&&level<20)gdk_rgba_parse(&accent,"#e5a50a");cairo_set_line_width(cr,1.8);gdk_cairo_set_source_rgba(cr,&outline);cairo_rectangle(cr,x,y,28,15);cairo_stroke(cr);cairo_rectangle(cr,x+29,y+4,3,7);cairo_fill(cr);
    if(level>=0){gdk_cairo_set_source_rgba(cr,&accent);cairo_rectangle(cr,x+3,y+3,22*CLAMP(level/100.,0,1),9);cairo_fill(cr);}
}
static void draw(GtkWidget*view,cairo_t*cr) {
    Widget*w=g_object_get_data(G_OBJECT(view),"victus-control");if(!w || (widget_hidden(w)&&!w->preview))return;
    GdkRectangle r=rect(w);GtkStyleContext*style=gtk_widget_get_style_context(view);GdkRGBA ink,accent;gboolean custom;
    render_background(w,style,cr,r,&ink,&custom);if(!custom)gtk_style_context_get_color(style,GTK_STATE_FLAG_NORMAL,&ink);
    if(!gtk_style_context_lookup_color(style,"theme_selected_bg_color",&accent))gdk_rgba_parse(&accent,"#3584e4");
    GdkRGBA muted=ink;muted.alpha=.65;
    VcLayoutSize layout=vc_layout(r.width,r.height);double scale=MIN((double)r.width/layout.width,(double)r.height/layout.height);cairo_save(cr);cairo_translate(cr,r.x+(r.width-layout.width*scale)/2,r.y+(r.height-layout.height*scale)/2);cairo_scale(cr,scale,scale);
    char*title=xfconf_channel_get_string(host->settings(),"/xfce-tweaks/victus-control/appearance/title","Victus Control");
    gboolean visible=xfconf_channel_get_bool(host->settings(),"/xfce-tweaks/victus-control/appearance/title-visible",TRUE),bottom=xfconf_channel_get_bool(host->settings(),"/xfce-tweaks/victus-control/appearance/title-bottom",FALSE);
    char*alignment=xfconf_channel_get_string(host->settings(),"/xfce-tweaks/victus-control/appearance/title-alignment","center");
    PangoAlignment align=!g_strcmp0(alignment,"right")?PANGO_ALIGN_RIGHT:!g_strcmp0(alignment,"left")?PANGO_ALIGN_LEFT:PANGO_ALIGN_CENTER;
    double fan_fraction=fan_mode==0?1:fan_mode==1&&fan_request>=0?fan_request/100.:-1;
    double power_fraction=watts>=0?watts/MAX(60.,ceil(watts/20)*20):-1;
    char*fan_value=fan_mode==0?g_strdup("MAX"):fan_mode==1&&fan_request>=0?g_strdup_printf("%d%%",fan_request):g_strdup(fan_mode==1?"Manual":fan_mode==2?"Auto":"—");
    char*power_value=watts>=0?g_strdup_printf("%.1f",watts):g_strdup("—");
    char*fan_detail=fan1>=0&&fan2>=0?g_strdup_printf("%d / %d RPM reported",fan1,fan2):g_strdup("RPM unavailable");
    char*power_detail=watts>=0?g_strdup_printf("0–%.0f W scale",MAX(60.,ceil(watts/20)*20)):g_strdup(discharging?"Reading unavailable":"Plugged in");
    char*cpu=temperature>=0?g_strdup_printf("CPU %.0f °C",temperature/1000.):g_strdup("CPU unavailable");
    char*profile_label=g_strdup(profile?profile:"Unavailable");for(char*t=profile_label;*t;t++){if(*t=='-')*t=' ';else *t=g_ascii_toupper(*t);}
    char*battery_text=battery>=0?g_strdup_printf("%d%%",battery):g_strdup("—");
    if(visible){
        int y=bottom?layout.height-31:12;
        if(layout.kind==VC_FULL){icon(cr,"computer-symbolic",18,y+1,20,muted);text(view,cr,title,46,y,268,1.12,TRUE,align,ink);}
        else text(view,cr,title,14,y,layout.width-28,layout.kind==VC_MINI?.9:1.05,TRUE,align,ink);
    }
    if(layout.kind==VC_FULL){
        gauge(cr,96,104,fan_fraction,ink,accent);gauge(cr,264,104,power_fraction,ink,accent);
        icon(cr,"fan-symbolic",84,77,24,muted);icon(cr,"widget-power",252,77,24,muted);
        text(view,cr,fan_value,36,108,120,1.5,TRUE,PANGO_ALIGN_CENTER,ink);text(view,cr,power_value,204,106,120,1.85,TRUE,PANGO_ALIGN_CENTER,ink);
        text(view,cr,"FAN CONTROL",24,158,144,.8,TRUE,PANGO_ALIGN_CENTER,muted);text(view,cr,"BATTERY WATTS",192,158,144,.8,TRUE,PANGO_ALIGN_CENTER,muted);
        text(view,cr,fan_detail,18,178,156,.76,FALSE,PANGO_ALIGN_CENTER,muted);text(view,cr,power_detail,186,178,156,.8,FALSE,PANGO_ALIGN_CENTER,muted);
        icon(cr,"temperature-symbolic",20,208,18,muted);text(view,cr,cpu,44,208,130,.9,FALSE,PANGO_ALIGN_LEFT,ink);text(view,cr,profile_label,180,208,160,.8,TRUE,PANGO_ALIGN_RIGHT,accent);
        battery_icon(cr,22,246,battery,ink,accent);text(view,cr,battery_text,64,241,80,1.2,TRUE,PANGO_ALIGN_LEFT,ink);text(view,cr,discharging?"On battery":"External power",160,246,180,.9,FALSE,PANGO_ALIGN_RIGHT,muted);
        if(!bottom){icon(cr,"preferences-system-symbolic",97,291,16,muted);text(view,cr,"Click to open controls",119,289,180,.8,FALSE,PANGO_ALIGN_LEFT,muted);}
    }else if(layout.kind==VC_COMPACT){
        small_gauge(cr,78,86,.72,fan_fraction,ink,accent);small_gauge(cr,202,86,.72,power_fraction,ink,accent);
        icon(cr,"fan-symbolic",69,64,18,muted);icon(cr,"widget-power",193,64,18,muted);
        text(view,cr,fan_value,32,88,92,1.3,TRUE,PANGO_ALIGN_CENTER,ink);text(view,cr,power_value,156,86,92,1.5,TRUE,PANGO_ALIGN_CENTER,ink);
        text(view,cr,"FAN",28,128,100,.8,TRUE,PANGO_ALIGN_CENTER,muted);text(view,cr,"WATTS",152,128,100,.8,TRUE,PANGO_ALIGN_CENTER,muted);
        text(view,cr,cpu,16,158,136,.9,FALSE,PANGO_ALIGN_LEFT,ink);battery_icon(cr,173,160,battery,ink,accent);text(view,cr,battery_text,212,156,56,1.,TRUE,PANGO_ALIGN_RIGHT,ink);
        if(!bottom)text(view,cr,profile_label,16,190,248,.8,TRUE,PANGO_ALIGN_CENTER,accent);
    }else if(layout.kind==VC_WIDE){
        small_gauge(cr,76,94,.78,fan_fraction,ink,accent);small_gauge(cr,232,94,.78,power_fraction,ink,accent);
        icon(cr,"fan-symbolic",66,71,20,muted);icon(cr,"widget-power",222,71,20,muted);
        text(view,cr,fan_value,26,95,100,1.4,TRUE,PANGO_ALIGN_CENTER,ink);text(view,cr,power_value,182,93,100,1.7,TRUE,PANGO_ALIGN_CENTER,ink);
        text(view,cr,"FAN",26,137,100,.8,TRUE,PANGO_ALIGN_CENTER,muted);text(view,cr,"WATTS",182,137,100,.8,TRUE,PANGO_ALIGN_CENTER,muted);
        icon(cr,"temperature-symbolic",326,58,18,muted);text(view,cr,cpu,350,58,152,1.,FALSE,PANGO_ALIGN_LEFT,ink);
        battery_icon(cr,326,94,battery,ink,accent);text(view,cr,battery_text,370,90,128,1.2,TRUE,PANGO_ALIGN_LEFT,ink);text(view,cr,profile_label,326,128,178,.8,TRUE,PANGO_ALIGN_LEFT,accent);
    }else if(layout.kind==VC_TALL){
        small_gauge(cr,120,96,.85,fan_fraction,ink,accent);small_gauge(cr,120,222,.85,power_fraction,ink,accent);
        icon(cr,"fan-symbolic",110,71,20,muted);icon(cr,"widget-power",110,197,20,muted);
        text(view,cr,fan_value,60,99,120,1.6,TRUE,PANGO_ALIGN_CENTER,ink);text(view,cr,power_value,60,223,120,1.8,TRUE,PANGO_ALIGN_CENTER,ink);
        text(view,cr,"FAN CONTROL",30,148,180,.8,TRUE,PANGO_ALIGN_CENTER,muted);text(view,cr,"BATTERY WATTS",30,274,180,.8,TRUE,PANGO_ALIGN_CENTER,muted);
        text(view,cr,cpu,12,308,140,.9,FALSE,PANGO_ALIGN_LEFT,ink);battery_icon(cr,152,312,battery,ink,accent);text(view,cr,battery_text,188,308,40,.9,TRUE,PANGO_ALIGN_RIGHT,ink);
        if(!bottom)text(view,cr,profile_label,12,339,216,.8,TRUE,PANGO_ALIGN_CENTER,accent);
    }else{
        double y=bottom?15:40;
        icon(cr,"fan-symbolic",12,y+1,18,muted);text(view,cr,fan_value,36,y-1,64,1.05,TRUE,PANGO_ALIGN_LEFT,ink);
        icon(cr,"widget-power",107,y+1,18,accent);char*value=watts>=0?g_strdup_printf("%.1f W",watts):g_strdup("— W");text(view,cr,value,130,y-1,62,1.05,TRUE,PANGO_ALIGN_LEFT,ink);g_free(value);
        text(view,cr,cpu,12,y+37,118,.85,FALSE,PANGO_ALIGN_LEFT,ink);battery_icon(cr,123,y+40,battery,ink,accent);text(view,cr,battery_text,157,y+36,34,.85,TRUE,PANGO_ALIGN_RIGHT,ink);
    }
    cairo_restore(cr);
    if(w->hovered || w->resizing){cairo_save(cr);gdk_cairo_set_source_rgba(cr,&muted);cairo_set_line_width(cr,1.5);for(int i=0;i<3;i++){cairo_move_to(cr,r.x+r.width-22+i*4,r.y+r.height-10);cairo_line_to(cr,r.x+r.width-10,r.y+r.height-22+i*4);}cairo_stroke(cr);cairo_restore(cr);}
    g_free(title);g_free(alignment);g_free(fan_value);g_free(power_value);g_free(fan_detail);g_free(cpu);g_free(profile_label);g_free(battery_text);g_free(power_detail);
}
static gboolean inside(GdkRectangle r,double x,double y) {return x>=r.x && y>=r.y && x<r.x+r.width && y<r.y+r.height;}
static gboolean resize_corner(GdkRectangle r,double x,double y){return inside(r,x,y)&&x>=r.x+r.width-24&&y>=r.y+r.height-24;}
static void resize_cursor(Widget*w,gboolean enabled){
    if(w->resize_cursor==enabled)return;GdkWindow*window=gtk_widget_get_window(w->view);if(!window)return;
    GdkCursor*cursor=enabled?gdk_cursor_new_from_name(gtk_widget_get_display(w->view),"se-resize"):NULL;gdk_window_set_cursor(window,cursor);g_clear_object(&cursor);w->resize_cursor=enabled;
}
static gboolean press(GtkWidget*view,GdkEventButton*e) {
    Widget*w=g_object_get_data(G_OBJECT(view),"victus-control");if(!w||widget_hidden(w)||e->button!=1)return FALSE;
    GdkRectangle r=rect(w);if(!inside(r,e->x,e->y))return FALSE;hover(w,TRUE);
    w->drag=TRUE;w->moved=FALSE;w->resizing=resize_corner(r,e->x,e->y);w->start_x=e->x;w->start_y=e->y;w->original_x=r.x;w->original_y=r.y;w->original_width=r.width;w->original_height=r.height;
    if(w->resizing){w->positioned=TRUE;w->x=r.x;w->y=r.y;resize_cursor(w,TRUE);}gtk_grab_add(view);return TRUE;
}
static gboolean motion(GtkWidget*view,GdkEventMotion*e) {
    Widget*w=g_object_get_data(G_OBJECT(view),"victus-control");if(!w||(!w->drag&&widget_hidden(w)))return FALSE;hover(w,inside(rect(w),e->x,e->y));resize_cursor(w,w->resizing||resize_corner(rect(w),e->x,e->y));if(!w->drag)return FALSE;
    if(w->resizing){
        vc_resize(w->original_width,w->original_height,(int)(e->x-w->start_x),(int)(e->y-w->start_y),gtk_widget_get_allocated_width(view)-w->x,gtk_widget_get_allocated_height(view)-w->y,&w->width,&w->height);w->moved=TRUE;hover(w,inside(rect(w),e->x,e->y));gtk_widget_queue_draw(view);return TRUE;
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
    if(view){Widget*w=g_object_get_data(view,"victus-control");if(w && w->controls==G_SUBPROCESS(object))w->running=FALSE;g_object_unref(view);}
    g_weak_ref_clear(weak);g_free(weak);
}
static gboolean release(GtkWidget*view,GdkEventButton*e) {
    Widget*w=g_object_get_data(G_OBJECT(view),"victus-control");if(!w||!w->drag||e->button!=1)return FALSE;
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
            w->controls=g_subprocess_new(G_SUBPROCESS_FLAGS_NONE,&error,"python3",app,"--monitor-key",w->key,NULL);g_free(app);
            if(w->controls){w->running=TRUE;GWeakRef*weak=g_new0(GWeakRef,1);g_weak_ref_init(weak,G_OBJECT(view));g_subprocess_wait_async(w->controls,NULL,controls_exited,weak);}
            if(error){g_warning("Victus controls: %s",error->message);g_clear_error(&error);}}
    }
    return TRUE;
}
static gboolean draw_cb(GtkWidget*v,cairo_t*cr,gpointer data){draw(v,cr);return FALSE;}
static gboolean press_cb(GtkWidget*v,GdkEventButton*e,gpointer data){return press(v,e);}
static gboolean release_cb(GtkWidget*v,GdkEventButton*e,gpointer data){return release(v,e);}
static gboolean leave_cb(GtkWidget*v,GdkEventCrossing*e,gpointer data){Widget*w=data;hover(w,FALSE);if(!w->resizing)resize_cursor(w,FALSE);return FALSE;}
static gboolean motion_cb(GtkWidget*v,GdkEventMotion*e,gpointer data){return motion(v,e);}
static gboolean widget_surface(GtkWidget*view,double x,double y,gpointer data){Widget*w=data;return !widget_hidden(w)&&inside(rect(w),x,y);}
static void free_widget(gpointer data) {
    Widget*w=data;widget_clear_transfer(w);xt_surface_unregister(w->view,"victus-control");if(w->drag && gtk_widget_has_grab(w->view))gtk_grab_remove(w->view);
    resize_cursor(w,FALSE);
    if(w->fade_source)g_source_remove(w->fade_source);
    if(w->controls){g_subprocess_force_exit(w->controls);g_clear_object(&w->controls);}
    g_signal_handlers_disconnect_by_data(w->view,w);
    views=g_list_remove(views,w);g_free(w->key);g_free(w);
    if(!views && timer){g_source_remove(timer);timer=0;g_clear_pointer(&profile,g_free);g_clear_object(&background_image);g_clear_pointer(&background_filename,g_free);g_clear_pointer(&icon_cache,g_hash_table_unref);if(theme_watch){g_signal_handler_disconnect(gtk_icon_theme_get_default(),theme_watch);theme_watch=0;}if(appearance_watch){g_signal_handler_disconnect(host->settings(),appearance_watch);appearance_watch=0;}}
}
static void attach(GtkWidget*view,const char*monitor) {
    if(g_object_get_data(G_OBJECT(view),"victus-control"))return;
    Widget*w=g_new0(Widget,1);w->view=view;
    char*id=g_compute_checksum_for_string(G_CHECKSUM_SHA256,monitor?monitor:"default",-1);
    w->key=g_strdup_printf("/xfce-tweaks/victus-control/monitors/%s",id);g_free(id);
    char*key=g_strconcat(w->key,"/x",NULL);w->positioned=xfconf_channel_has_property(host->settings(),key);w->x=xfconf_channel_get_int(host->settings(),key,0);g_free(key);
    key=g_strconcat(w->key,"/y",NULL);w->y=xfconf_channel_get_int(host->settings(),key,24);g_free(key);
    key=g_strconcat(w->key,"/width",NULL);w->width=xfconf_channel_get_int(host->settings(),key,0);g_free(key);
    key=g_strconcat(w->key,"/height",NULL);w->height=xfconf_channel_get_int(host->settings(),key,0);g_free(key);
    key=g_strconcat(w->key,"/hidden",NULL);w->hidden=xfconf_channel_get_bool(host->settings(),key,FALSE);g_free(key);
    g_object_set_data_full(G_OBJECT(view),"victus-control",w,free_widget);
    xt_surface_register(view,"victus-control",widget_surface,w);views=g_list_prepend(views,w);
    for(GList*l=views;l;l=l->next)gtk_widget_queue_draw(((Widget*)l->data)->view);
    g_signal_connect_after(view,"draw",G_CALLBACK(draw_cb),w);
    g_signal_connect(view,"button-press-event",G_CALLBACK(press_cb),w);
    g_signal_connect(view,"button-release-event",G_CALLBACK(release_cb),w);
    g_signal_connect(view,"motion-notify-event",G_CALLBACK(motion_cb),w);
    g_signal_connect(view,"leave-notify-event",G_CALLBACK(leave_cb),w);
    if(!theme_watch)theme_watch=g_signal_connect(gtk_icon_theme_get_default(),"changed",G_CALLBACK(icon_theme_changed),NULL);
    if(!appearance_watch)appearance_watch=g_signal_connect(host->settings(),"property-changed",G_CALLBACK(appearance_changed),NULL);
    if(!timer){refresh(NULL);timer=g_timeout_add_seconds(2,refresh,NULL);}
}
static void detach(GtkWidget*view) {g_object_set_data(G_OBJECT(view),"victus-control",NULL);gtk_widget_queue_draw(view);}
static const XtTweak tweak={.abi_version=XT_ABI_VERSION,.struct_size=sizeof(XtTweak),.id="victus-control",.name="Victus Control",
 .attach_view=attach,.detach_view=detach};
G_MODULE_EXPORT const XtTweak *xfce_tweak_query(const XtHost*api) {
    if(!api||api->abi_version!=XT_ABI_VERSION||api->struct_size<sizeof(XtHost))return NULL;
    Dl_info info;if(!dladdr((void*)xfce_tweak_query,&info))return NULL;
    char*directory=g_path_get_dirname(info.dli_fname);g_free(assets);assets=g_build_filename(directory,"assets",NULL);g_free(directory);
    host=api;return &tweak;
}
