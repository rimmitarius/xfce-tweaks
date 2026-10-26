project('desktop-peek','c',version:'0.1.0',default_options:['c_std=gnu11','warning_level=2'])
sdk=dependency('xfce-tweaks-1',version:'>=0.1.0')
shared_module('desktop-peek','desktop-peek.c',dependencies:sdk,gnu_symbol_visibility:'hidden')
