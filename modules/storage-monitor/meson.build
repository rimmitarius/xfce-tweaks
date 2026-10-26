project('storage-monitor','c',version:'0.1.0',default_options:['c_std=gnu11','warning_level=2'])
sdk=dependency('xfce-tweaks-1')
cc=meson.get_compiler('c')
shared_module('storage-monitor','storage-monitor.c',dependencies:[sdk,cc.find_library('dl'),cc.find_library('m')],gnu_symbol_visibility:'hidden')
