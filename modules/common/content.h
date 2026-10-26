/* SPDX-License-Identifier: GPL-2.0-or-later */
static char *option(const char *section,const char *name,const char *fallback){char*k=g_strconcat(PREFIX,"/",section,"/",name,NULL);char*s=xfconf_channel_get_string(host->settings(),k,fallback);g_free(k);return s;}
static char *weather_value(const char *name,const char *fallback){char*s=weather?g_key_file_get_string(weather,"Weather",name,NULL):NULL;return s?s:g_strdup(fallback);}
static void fetched(GObject*object,GAsyncResult*result,gpointer unused){
    char*out=NULL,*err=NULL;GError*error=NULL;
    gboolean ok=g_subprocess_communicate_utf8_finish(G_SUBPROCESS(object),result,&out,&err,&error);
    if(fetch==G_SUBPROCESS(object)){
        if(ok&&out&&views){GKeyFile*k=g_key_file_new();if(g_key_file_load_from_data(k,out,-1,G_KEY_FILE_NONE,NULL)){g_clear_pointer(&weather,g_key_file_unref);weather=k;}else g_key_file_unref(k);}
        g_clear_object(&fetch);
        for(GList*l=views;l;l=l->next)gtk_widget_queue_draw(((Widget*)l->data)->view);
    }
    g_clear_error(&error);g_free(out);g_free(err);
}
static gboolean refresh(gpointer unused){
    if(!IS_WEATHER){
        static gint64 last=-1;char*seconds=option("clock","seconds","false");
        gint64 tick=g_get_real_time()/G_USEC_PER_SEC/(!strcmp(seconds,"true")?1:60);g_free(seconds);
        if(tick==last)return G_SOURCE_CONTINUE;last=tick;
    }
    if(IS_WEATHER && !fetch && g_get_monotonic_time()>=weather_due){
        char *lat=option("weather","latitude",""),*lon=option("weather","longitude",""),*units=option("weather","units","celsius");
        if(*lat&&*lon){char*app=g_build_filename(assets,"weather.py",NULL);fetch=g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE,NULL,"python3",app,lat,lon,units,NULL);g_free(app);if(fetch)g_subprocess_communicate_utf8_async(fetch,NULL,NULL,fetched,NULL);}
        else g_clear_pointer(&weather,g_key_file_unref);
        weather_due=g_get_monotonic_time()+900*G_USEC_PER_SEC;g_free(lat);g_free(lon);g_free(units);
    }
    for(GList*l=views;l;l=l->next)gtk_widget_queue_draw(((Widget*)l->data)->view);
    return G_SOURCE_CONTINUE;
}
static void draw(GtkWidget*view,cairo_t*cr){
    Widget*w=g_object_get_data(G_OBJECT(view),WIDGET_ID);if(!w || (widget_hidden(w)&&!w->preview))return;
    GdkRectangle r=rect(w);GdkRGBA ink;gboolean custom;
    render_background(w,gtk_widget_get_style_context(view),cr,r,&ink,&custom);
    if(!custom)gtk_style_context_get_color(gtk_widget_get_style_context(view),GTK_STATE_FLAG_NORMAL,&ink);
    cairo_save(cr);cairo_rectangle(cr,r.x+8,r.y+8,MAX(0,r.width-16),MAX(0,r.height-16));cairo_clip(cr);
    double scale=MIN(r.width/320.,r.height/180.);double x=r.x+12,y=r.y+12;int width=r.width-24;
    GdkRGBA muted=ink;muted.alpha*=.7;
    if(!IS_WEATHER){
        char*zone=option("clock","timezone",""),*format=option("clock","format","24"),*seconds=option("clock","seconds","false"),*style=option("clock","style","digital");
        GTimeZone*tz=*zone?g_time_zone_new_identifier(zone):g_time_zone_new_local();if(!tz)tz=g_time_zone_new_local();GDateTime*now=g_date_time_new_now(tz);g_time_zone_unref(tz);
        const char*fmt=!strcmp(format,"12")?(!strcmp(seconds,"true")?"%I:%M:%S %p":"%I:%M %p"):(!strcmp(seconds,"true")?"%H:%M:%S":"%H:%M");
        char*t=g_date_time_format(now,fmt),*date=g_date_time_format(now,"%A, %B %e"),*label=g_date_time_format(now,"%Z");
        if(!strcmp(style,"analog")){
            double radius=MIN(r.height*.31,r.width*.24),cx=r.x+r.width/2.,cy=r.y+r.height*.40;
            gdk_cairo_set_source_rgba(cr,&ink);cairo_set_line_width(cr,2);cairo_new_path(cr);cairo_arc(cr,cx,cy,radius,0,2*G_PI);cairo_stroke(cr);
            for(int i=0;i<12;i++){double a=i*G_PI/6;cairo_move_to(cr,cx+sin(a)*radius*.85,cy-cos(a)*radius*.85);cairo_line_to(cr,cx+sin(a)*radius*.94,cy-cos(a)*radius*.94);cairo_stroke(cr);}
            double minute=g_date_time_get_minute(now)+g_date_time_get_second(now)/60.,hour=g_date_time_get_hour(now)+minute/60.;
            double angles[]={hour*G_PI/6,minute*G_PI/30,g_date_time_get_second(now)*G_PI/30};double lengths[]={.52,.78,.83};
            for(int i=0;i<(!strcmp(seconds,"true")?3:2);i++){cairo_set_line_width(cr,i==0?4:i==1?2.5:1);cairo_move_to(cr,cx,cy);cairo_line_to(cr,cx+sin(angles[i])*radius*lengths[i],cy-cos(angles[i])*radius*lengths[i]);cairo_stroke(cr);}
            text(view,cr,date,x,r.y+r.height*.78,width,MAX(.75,scale),FALSE,PANGO_ALIGN_CENTER,ink);
        }else{
            text(view,cr,t,x,y+r.height*.09,width,MAX(1.,3.2*scale),TRUE,PANGO_ALIGN_CENTER,ink);
            text(view,cr,date,x,y+r.height*.53,width,MAX(.75,1.1*scale),FALSE,PANGO_ALIGN_CENTER,ink);
        }
        text(view,cr,label,x,r.y+r.height-28,width,.8,FALSE,PANGO_ALIGN_CENTER,muted);
        g_free(t);g_free(date);g_free(label);g_free(zone);g_free(format);g_free(seconds);g_free(style);g_date_time_unref(now);
    }else{
        char*name=option("weather","location","Choose a location"),*temp=weather_value("temperature","—"),*condition=weather_value("condition","Click to configure weather"),*detail=weather_value("detail",""),*forecast=weather_value("forecast",""),*status=weather_value("status","Weather by Open-Meteo"),*symbol=weather_value("icon","weather-overcast-symbolic");
        text(view,cr,name,x,y,width,MAX(.85,scale),TRUE,PANGO_ALIGN_CENTER,ink);
        int size=MAX(24,(int)(40*scale));GtkIconInfo*info=gtk_icon_theme_lookup_icon(gtk_icon_theme_get_default(),symbol,size,GTK_ICON_LOOKUP_FORCE_SYMBOLIC);
        if(info){GdkPixbuf*p=gtk_icon_info_load_symbolic(info,&ink,NULL,NULL,NULL,NULL,NULL);if(p){gdk_cairo_set_source_pixbuf(cr,p,x,y+32*scale);cairo_paint(cr);g_object_unref(p);}g_object_unref(info);}
        text(view,cr,temp,x+size+8,y+25*scale,width-size-8,MAX(1.2,2.5*scale),TRUE,PANGO_ALIGN_CENTER,ink);
        text(view,cr,condition,x,y+77*scale,width,MAX(.8,scale),FALSE,PANGO_ALIGN_CENTER,ink);
        if(r.height>=180){text(view,cr,detail,x,y+103*scale,width,MAX(.75,.85*scale),FALSE,PANGO_ALIGN_CENTER,muted);text(view,cr,forecast,x,y+126*scale,width,MAX(.7,.8*scale),FALSE,PANGO_ALIGN_CENTER,muted);}
        text(view,cr,status,x,r.y+r.height-25,width,.7,FALSE,PANGO_ALIGN_CENTER,muted);
        g_free(name);g_free(temp);g_free(condition);g_free(detail);g_free(forecast);g_free(status);g_free(symbol);
    }
    cairo_restore(cr);gdk_cairo_set_source_rgba(cr,&muted);cairo_set_line_width(cr,1.5);cairo_new_path(cr);
    for(int i=0;i<3;i++){cairo_move_to(cr,r.x+r.width-20+i*4,r.y+r.height-8);cairo_line_to(cr,r.x+r.width-8,r.y+r.height-20+i*4);}cairo_stroke(cr);
}
