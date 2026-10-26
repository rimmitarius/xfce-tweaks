/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../modules/victus-control/widget-layout.h"
int main(int argc,char**argv){
 g_test_init(&argc,&argv,NULL);
 g_assert_cmpint(vc_layout(360,320).kind,==,VC_FULL);
 g_assert_cmpint(vc_layout(280,220).kind,==,VC_COMPACT);
 g_assert_cmpint(vc_layout(520,180).kind,==,VC_WIDE);
 g_assert_cmpint(vc_layout(240,360).kind,==,VC_TALL);
 g_assert_cmpint(vc_layout(200,120).kind,==,VC_MINI);
 int width,height;
 vc_resize(360,320,-100,-100,800,600,&width,&height);g_assert_cmpint(width,==,260);g_assert_cmpint(height,==,220);
 vc_resize(360,320,-900,-900,800,600,&width,&height);g_assert_cmpint(width,==,200);g_assert_cmpint(height,==,110);
 vc_resize(360,320,2000,2000,750,550,&width,&height);g_assert_cmpint(width,==,750);g_assert_cmpint(height,==,550);
 vc_resize(360,320,0,0,120,80,&width,&height);g_assert_cmpint(width,==,120);g_assert_cmpint(height,==,80);
 for(int w=200;w<=1000;w+=10)for(int h=110;h<=1000;h+=10){VcLayoutSize l=vc_layout(w,h);g_assert_cmpint(l.width,>,0);g_assert_cmpint(l.height,>,0);}
 g_print("PASS: responsive layouts, resize limits and monitor bounds\n");return 0;
}
