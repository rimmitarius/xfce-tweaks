project('video-wallpaper','c',version:'0.1.0',default_options:['c_std=gnu11'])
sdk=dependency('xfce-tweaks-1',version:'>=0.1.0')
shared_module('video-wallpaper','video-wallpaper.c',dependencies:[sdk,dependency('gstreamer-1.0')],gnu_symbol_visibility:'hidden')
