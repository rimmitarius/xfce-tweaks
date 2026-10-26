/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "xfce-tweaks.h"
int main(int argc,char **argv) {
    g_assert_cmpint(argc,>,1);
    for(int i=1;i<argc;i++) {
        GModule *module=g_module_open(argv[i],G_MODULE_BIND_LOCAL);g_assert_nonnull(module);
        XtQuery query=NULL;g_assert_true(g_module_symbol(module,"xfce_tweak_query",(gpointer*)&query));
        XtHost host={.abi_version=999,.struct_size=sizeof(XtHost)};
        g_assert_null(query(&host));host.abi_version=XT_ABI_VERSION;host.struct_size=1;
        g_assert_null(query(&host));host.struct_size=sizeof(XtHost);
        const XtTweak *tweak=query(&host);g_assert_nonnull(tweak);
        g_assert_cmpuint(tweak->abi_version,==,XT_ABI_VERSION);g_assert_cmpuint(tweak->struct_size,==,sizeof(XtTweak));
        g_assert_nonnull(tweak->id);g_print("PASS: ABI validation and public entry point: %s\n",tweak->id);
        g_module_close(module);
    }
    return 0;
}
