/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared by native card modules; no host changes or managed overlay windows. */
static gboolean widget_hidden(Widget*w){
    if(w->drop_target)return TRUE;
    if(!w->hidden)return FALSE;
    /* Temporarily expose a card if its assigned display is disconnected.
     * Do not rewrite the saved placement; reconnect restores it. */
    for(GList*l=views;l;l=l->next)if(!((Widget*)l->data)->hidden)return TRUE;
    return !views || views->data!=w;
}
static GdkRectangle widget_preview_rect(Widget*w){
    Widget*source=w->preview;
    int width=gtk_widget_get_allocated_width(w->view),height=gtk_widget_get_allocated_height(w->view);
    int rw=MIN(width,source->original_width),rh=MIN(height,source->original_height);
    return (GdkRectangle){CLAMP(w->preview_x,0,MAX(0,width-rw)),CLAMP(w->preview_y,0,MAX(0,height-rh)),rw,rh};
}
static void widget_save_placement(Widget*w){
    const char*names[]={"x","y","width","height"};
    int values[]={w->x,w->y,w->width,w->height};
    for(int i=0;i<4;i++){char*k=g_strconcat(w->key,"/",names[i],NULL);xfconf_channel_set_int(host->settings(),k,values[i]);g_free(k);}
    char*k=g_strconcat(w->key,"/hidden",NULL);xfconf_channel_set_bool(host->settings(),k,w->hidden);g_free(k);
}
static void widget_clear_transfer(Widget*w){
    for(GList*l=views;l;l=l->next){Widget*other=l->data;
        if(other->preview==w)other->preview=NULL;
        if(other->drop_target==w)other->drop_target=NULL;
        gtk_widget_queue_draw(other->view);
    }
    w->drop_target=NULL;w->preview=NULL;
}
static void widget_drag_preview(Widget*w,double root_x,double root_y){
    Widget*target=NULL;int ox=0,oy=0;
    for(GList*l=views;l;l=l->next){Widget*other=l->data;
        if(other==w || !gtk_widget_get_realized(other->view) || !gtk_widget_get_visible(other->view))continue;
        int x,y;gdk_window_get_origin(gtk_widget_get_window(other->view),&x,&y);
        if(root_x>=x && root_y>=y && root_x<x+gtk_widget_get_allocated_width(other->view) && root_y<y+gtk_widget_get_allocated_height(other->view)){target=other;ox=x;oy=y;break;}
    }
    widget_clear_transfer(w);
    if(target){
        w->drop_target=target;target->preview=w;
        target->preview_x=(int)(root_x-ox-(w->start_x-w->original_x));
        target->preview_y=(int)(root_y-oy-(w->start_y-w->original_y));
    }
    for(GList*l=views;l;l=l->next)gtk_widget_queue_draw(((Widget*)l->data)->view);
}
static gboolean widget_finish_transfer(Widget*w){
    Widget*target=w->drop_target;if(!target)return FALSE;
    GdkRectangle r=widget_preview_rect(target);
    target->x=r.x;target->y=r.y;target->width=w->original_width;target->height=w->original_height;
    target->positioned=TRUE;target->hidden=FALSE;
    w->x=w->original_x;w->y=w->original_y;w->positioned=TRUE;w->hidden=TRUE;
    widget_clear_transfer(w);widget_save_placement(target);widget_save_placement(w);
    return TRUE;
}
