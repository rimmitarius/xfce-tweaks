project('clock-widget','c',version:'0.1.0',default_options:['c_std=gnu11','warning_level=2'])
cc=meson.get_compiler('c')
shared_module('clock-widget','clock-widget.c',dependencies:[dependency('xfce-tweaks-1'),cc.find_library('dl'),cc.find_library('m')],gnu_symbol_visibility:'hidden')
