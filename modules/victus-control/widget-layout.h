/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <glib.h>
typedef enum { VC_FULL, VC_COMPACT, VC_WIDE, VC_TALL, VC_MINI } VcLayout;
typedef struct { VcLayout kind; int width,height; } VcLayoutSize;
static inline VcLayoutSize vc_layout(int width,int height) {
    if(height<135 || (width<250 && height<180))return (VcLayoutSize){VC_MINI,200,120};
    if(width>=400 && height<230)return (VcLayoutSize){VC_WIDE,520,180};
    if(width<290 && height>=330)return (VcLayoutSize){VC_TALL,240,360};
    if(width<330 || height<280)return (VcLayoutSize){VC_COMPACT,280,220};
    return (VcLayoutSize){VC_FULL,360,320};
}
static inline void vc_resize(int width,int height,int dx,int dy,int available_width,int available_height,int*out_width,int*out_height) {
    int max_width=MAX(1,MIN(1000,available_width)),max_height=MAX(1,MIN(1000,available_height));
    *out_width=CLAMP(width+dx,MIN(200,max_width),max_width);
    *out_height=CLAMP(height+dy,MIN(110,max_height),max_height);
}
