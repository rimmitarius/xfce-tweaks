/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "../modules/victus-control/victus-control.c"
#include <glib/gstdio.h>
static void put(const char*base,const char*name,const char*text){char*p=g_build_filename(base,name,NULL);g_assert_true(g_file_set_contents(p,text,-1,NULL));g_free(p);}
int main(int argc,char**argv){
    g_test_init(&argc,&argv,NULL);char*root=g_dir_make_tmp("victus-watts-XXXXXX",NULL);g_assert_nonnull(root);
    char*directory=g_build_filename(root,"class/power_supply/BAT1",NULL);g_assert_cmpint(g_mkdir_with_parents(directory,0700),==,0);
    g_setenv("XFCE_TWEAKS_TEST_SYSFS",root,TRUE);
    put(directory,"type","Battery");put(directory,"capacity","50");put(directory,"status","Discharging");put(directory,"power_now","19500000");refresh(NULL);g_assert_cmpfloat(watts,==,19.5);
    put(directory,"status","Charging");refresh(NULL);g_assert_cmpfloat(watts,==,-1);
    put(directory,"status","Discharging");char*p=g_build_filename(directory,"power_now",NULL);g_remove(p);g_free(p);
    put(directory,"current_now","2000000");put(directory,"voltage_now","15000000");refresh(NULL);g_assert_cmpfloat(watts,==,30);
    put(directory,"current_now","-1");refresh(NULL);g_assert_cmpfloat(watts,==,-1);
    const char*names[]={"type","capacity","status","current_now","voltage_now",NULL};for(int i=0;names[i];i++){p=g_build_filename(directory,names[i],NULL);g_remove(p);g_free(p);}
    g_rmdir(directory);g_free(directory);p=g_build_filename(root,"class/power_supply",NULL);g_rmdir(p);g_free(p);p=g_build_filename(root,"class",NULL);g_rmdir(p);g_free(p);g_rmdir(root);g_free(root);g_clear_pointer(&profile,g_free);
    g_print("PASS: watts conversion, charging exclusion, current/voltage fallback, invalid readings\n");return 0;
}
