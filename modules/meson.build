project('xfce-tweaks-modules','c',version:'0.1.0',default_options:['c_std=gnu11','warning_level=2'])
gtk=dependency('gtk+-3.0')
xfconf=dependency('libxfconf-0')
gmodule=dependency('gmodule-2.0')
cc=meson.get_compiler('c')
math=cc.find_library('m')
sdk=include_directories('../sdk')
shared_module('desktop-groups',['desktop-groups/desktop-groups.c','desktop-groups/dg-store.c','desktop-groups/dg-layout.c'],
 include_directories:sdk,dependencies:[gtk,xfconf,gmodule,math],gnu_symbol_visibility:'hidden',
 install:true,install_dir:get_option('libdir')/'xfce-tweaks/modules/desktop-groups')
shared_module('video-wallpaper','video-wallpaper/video-wallpaper.c',include_directories:sdk,
 dependencies:[gtk,xfconf,gmodule,dependency('gstreamer-1.0')],gnu_symbol_visibility:'hidden',
 install:true,install_dir:get_option('libdir')/'xfce-tweaks/modules/video-wallpaper')
foreach id:['desktop-groups','video-wallpaper']
 install_data(id/'tweak.ini',install_dir:get_option('libdir')/'xfce-tweaks/modules'/id)
endforeach

store_test=executable('test-desktop-groups',['../tests/test-desktop-groups.c','desktop-groups/dg-store.c'],dependencies:[dependency('glib-2.0'),math])
test('desktop-groups-store',store_test)
layout_test=executable('test-group-layout',['../tests/test-group-layout.c','desktop-groups/dg-layout.c'],dependencies:dependency('glib-2.0'))
test('desktop-groups-layout',layout_test)

abi_test=executable('test-abi','../tests/test-abi.c',include_directories:sdk,dependencies:[gtk,xfconf,gmodule])
test('tweak-abi',abi_test,args:[meson.current_build_dir()/'libdesktop-groups.so',meson.current_build_dir()/'libvideo-wallpaper.so'])

victus_module=shared_module('victus-control','victus-control/victus-control.c',include_directories:sdk,
 dependencies:[gtk,xfconf,gmodule,cc.find_library('dl'),math],gnu_symbol_visibility:'hidden',
 install:true,install_dir:get_option('libdir')/'xfce-tweaks/modules/victus-control')
install_data('victus-control/tweak.ini',install_dir:get_option('libdir')/'xfce-tweaks/modules/victus-control')
install_subdir('victus-control/assets',install_dir:get_option('libdir')/'xfce-tweaks/modules/victus-control',exclude_directories:['__pycache__'])

test('victus-control-abi',abi_test,args:[victus_module.full_path()])
test('victus-control-hardware',find_program('python3'),args:[meson.current_source_dir()/'../tests/test-victus.py'])
victus_widget_test=executable('test-victus-widget','../tests/test-victus-widget.c',include_directories:sdk,dependencies:[gtk,xfconf,gmodule,cc.find_library('dl'),math])
test('victus-widget-power',victus_widget_test)
victus_layout_test=executable('test-victus-layout','../tests/test-victus-layout.c',dependencies:dependency('glib-2.0'))
test('victus-widget-layout',victus_layout_test)
peek_module=shared_module('desktop-peek','desktop-peek/desktop-peek.c',include_directories:sdk,
 dependencies:[gtk,xfconf,gmodule],gnu_symbol_visibility:'hidden',install:true,
 install_dir:get_option('libdir')/'xfce-tweaks/modules/desktop-peek')
install_data('desktop-peek/tweak.ini',install_dir:get_option('libdir')/'xfce-tweaks/modules/desktop-peek')
test('desktop-peek-abi',abi_test,args:[peek_module.full_path()])
cooperation_test=executable('test-cooperation','../tests/test-cooperation.c',include_directories:sdk,dependencies:[gtk,xfconf,gmodule])
test('module-cooperation',cooperation_test)
peek_input_test=executable('test-desktop-peek','../tests/test-desktop-peek.c',include_directories:sdk,dependencies:[gtk,xfconf,gmodule])
test('desktop-peek-input',peek_input_test)

external_dimming=shared_module('external-dimming','external-dimming/external-dimming.c',include_directories:sdk,
 dependencies:[gtk,xfconf,gmodule,dependency('x11'),dependency('xrandr')],gnu_symbol_visibility:'hidden',
 install:true,install_dir:get_option('libdir')/'xfce-tweaks/modules/external-dimming')
install_data('external-dimming/tweak.ini',install_dir:get_option('libdir')/'xfce-tweaks/modules/external-dimming')
test('external-dimming-abi',abi_test,args:[external_dimming.full_path()])

foreach id:['clock-widget','weather-widget']
 widget=shared_module(id,id/(id+'.c'),include_directories:sdk,
 dependencies:[gtk,xfconf,gmodule,cc.find_library('dl'),math],gnu_symbol_visibility:'hidden',
 install:true,install_dir:get_option('libdir')/'xfce-tweaks/modules'/id)
 install_data(id/'tweak.ini',install_dir:get_option('libdir')/'xfce-tweaks/modules'/id)
 install_subdir(id/'assets',install_dir:get_option('libdir')/'xfce-tweaks/modules'/id,exclude_directories:['__pycache__'])
 test(id+'-abi',abi_test,args:[widget.full_path()])
endforeach
foreach id:['cpu-monitor','gpu-monitor','memory-monitor','storage-monitor']
 monitor_widget=shared_module(id,id/(id+'.c'),include_directories:sdk,
 dependencies:[gtk,xfconf,gmodule,cc.find_library('dl'),math],gnu_symbol_visibility:'hidden',
 install:true,install_dir:get_option('libdir')/'xfce-tweaks/modules'/id)
 install_data(id/'tweak.ini',install_dir:get_option('libdir')/'xfce-tweaks/modules'/id)
 install_subdir(id/'assets',install_dir:get_option('libdir')/'xfce-tweaks/modules'/id,exclude_directories:['__pycache__'])
endforeach
test('weather-client',find_program('python3'),args:[meson.current_source_dir()/'../tests/test-weather.py'])
foreach id:['clock-widget','weather-widget','victus-control']
 executable('test-transfer-'+id,'../tests/test-widget-transfer.c',include_directories:sdk,
 c_args:['-DMODULE_SOURCE="../modules/'+id+'/'+id+'.c"','-DTEST_WIDGET_ID="'+id+'"'],
 dependencies:[gtk,xfconf,gmodule,cc.find_library('dl'),math])
endforeach
executable('test-video-scaling','../tests/test-video-scaling.c',include_directories:sdk,
 dependencies:[gtk,xfconf,gmodule,dependency('gstreamer-1.0')])
executable('test-video-monitors','../tests/test-video-monitors.c',include_directories:sdk,
 dependencies:[gtk,xfconf,gmodule,dependency('gstreamer-1.0')])
executable('test-group-transfer',['../tests/test-group-transfer.c','desktop-groups/dg-store.c','desktop-groups/dg-layout.c'],include_directories:sdk,dependencies:[gtk,xfconf,gmodule,math])
