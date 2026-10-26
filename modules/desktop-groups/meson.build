project('desktop-groups','c',version:'0.1.0',default_options:['c_std=gnu11'])
sdk=dependency('xfce-tweaks-1',version:'>=0.1.0')
math=meson.get_compiler('c').find_library('m')
shared_module('desktop-groups',['desktop-groups.c','dg-store.c','dg-layout.c'],dependencies:[sdk,math],gnu_symbol_visibility:'hidden')
