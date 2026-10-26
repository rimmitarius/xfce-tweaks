#!/usr/bin/env python3
"""Run with python3 tests/test-victus-x11.py (creates a private X server/bus).
Private home/config and private X server required; no hardware writes."""
import os,subprocess,tempfile,time,shutil,sys
from pathlib import Path
if os.environ.get('XFCE_TWEAKS_VICTUS_TEST_SANDBOX')!='1':
 result=subprocess.run(['xvfb-run','-a','-s','-screen 0 1280x800x24','dbus-run-session','--',sys.executable,__file__],env=dict(os.environ,XFCE_TWEAKS_VICTUS_TEST_SANDBOX='1'))
 sys.exit(result.returncode)
ROOT=Path(__file__).resolve().parents[1]
(ROOT/'test-results').mkdir(exist_ok=True)
def run(*args):return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT).strip()
def xf(*args):return run('xfconf-query','-c','xfce4-desktop',*args)
def wait(fn):
 for _ in range(60):
  try:
   result=fn()
   if result:return result
  except subprocess.CalledProcessError:pass
  time.sleep(.1)
 raise AssertionError('Timed out')
def xdo(*args):run('xdotool',*map(str,args));time.sleep(.2)
with tempfile.TemporaryDirectory(prefix='victus-x11-') as tmp:
 root=Path(tmp)
 for key,dirname in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_CACHE_HOME','cache'),('XDG_DATA_HOME','data')]:
  os.environ[key]=str(root/dirname);(root/dirname).mkdir()
 (root/'home/Desktop').mkdir();(root/'config/user-dirs.dirs').write_text('XDG_DESKTOP_DIR="$HOME/Desktop"\n')
 module=root/'modules/victus-control';module.mkdir(parents=True)
 shutil.copy(ROOT/'modules/victus-control/tweak.ini',module)
 shutil.copy(ROOT/'modules/victus-control/build/libvictus-control.so',module)
 shutil.copytree(ROOT/'modules/victus-control/assets',module/'assets',ignore=shutil.ignore_patterns('__pycache__'))
 os.environ['XFCE_TWEAKS_MODULE_PATH']=str(root/'modules')
 os.environ['NO_AT_BRIDGE']='1';os.environ['GIO_USE_VFS']='local'
 run('dbus-update-activation-environment','DISPLAY','HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_DATA_HOME','NO_AT_BRIDGE')
 xf('-p','/xfce-tweaks/enabled/victus-control','-n','-t','bool','-s','true')
 wm=subprocess.Popen(['xfwm4','--compositor=off'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 log=(ROOT/'tests/victus-x11.log').open('w');desktop=None
 try:
  desktop=subprocess.Popen([str(ROOT/'build-host/src/xfdesktop'),'--disable-wm-check'],stdout=log,stderr=log)
  time.sleep(2)
  wait(lambda:xf('-p','/xfce-tweaks/status/victus-control')=='active')
  time.sleep(1)
  import gi;gi.require_version('Gdk','3.0');from gi.repository import Gdk
  screen=Gdk.get_default_root_window();Gdk.pixbuf_get_from_window(screen,0,0,1280,800).savev(str(ROOT/'test-results/victus-widget.png'),'png',[],[])
  width=int(run('xdotool','getdisplaygeometry').split()[0]);x=width-180
  def pixel():return bytes(Gdk.pixbuf_get_from_window(screen,width-360-24+15,124,1,1).get_pixels())
  def appearance(key,kind,value):xf('-p','/xfce-tweaks/victus-control/appearance/'+key,'-n','-t',kind,'-s',str(value));time.sleep(.15)
  before=pixel();appearance('color','string','#15202b');appearance('text-color','string','#ffffff');appearance('opacity','double',1);assert pixel()!=before
  appearance('radius','double',24);appearance('border-width','double',3);appearance('border-color','string','#6d9dce')
  from gi.repository import GdkPixbuf
  image=GdkPixbuf.Pixbuf.new(GdkPixbuf.Colorspace.RGB,False,8,4,4);image.fill(0xd65b45ff);image_path=root/'background.png';image.savev(str(image_path),'png',[],[])
  before=pixel();appearance('image','string',image_path);assert pixel()!=before
  appearance('hover-enter','double',1);appearance('hover-exit','double',.35)
  xdo('mousemove',x,60);time.sleep(.3);entered=pixel();xdo('mousemove',400,500);time.sleep(.3);assert pixel()!=entered
  print('PASS: live color, image, rounded border and hover opacity customization')
  xdo('mousemove',x,60);xdo('click',1)
  window=wait(lambda:run('xdotool','search','--name','^HP Victus/Omen Control$'))
  print('PASS: native widget click opens GTK controls')
  xdo('windowactivate','--sync',window);xdo('key','alt+F4');time.sleep(.5)
  xdo('mousemove',x,60);xdo('mousedown',1);xdo('mousemove',x-250,210);xdo('mouseup',1)
  properties=xf('-lv');assert '/xfce-tweaks/victus-control/monitors/' in properties,properties
  print('PASS: dragging widget persists monitor position')
  key=next(line.split()[0].rsplit('/',1)[0] for line in properties.splitlines() if '/xfce-tweaks/victus-control/monitors/' in line and line.split()[0].endswith('/x'))
  left=int(xf('-p',key+'/x'));top=int(xf('-p',key+'/y'));old_width,old_height=360,320
  for name,new_width,new_height in [('mini',200,120),('minimum',200,110),('compact',280,220),('wide',520,180),('tall',240,360),('full',360,320),('restored-compact',280,220)]:
   xdo('mousemove',left+old_width-12,top+old_height-12);xdo('mousedown',1)
   xdo('mousemove',left+new_width-12,top+new_height-12);xdo('mouseup',1)
   assert int(xf('-p',key+'/width'))==new_width
   assert int(xf('-p',key+'/height'))==new_height
   assert int(xf('-p',key+'/x'))==left and int(xf('-p',key+'/y'))==top
   assert subprocess.run(['xdotool','search','--name','^HP Victus/Omen Control$'],stdout=subprocess.DEVNULL).returncode!=0,'Resizing opened controls'
   Gdk.pixbuf_get_from_window(screen,left,top,new_width,new_height).savev(str(ROOT/('test-results/victus-'+name+'.png')),'png',[],[])
   old_width,old_height=new_width,new_height
  print('PASS: corner resizing reflows all five sizes, keeps its anchor and does not open controls')

  xf('-p','/xfce-tweaks/enabled/victus-control','-s','false');wait(lambda:xf('-p','/xfce-tweaks/status/victus-control')=='disabled')
  xf('-p','/xfce-tweaks/enabled/victus-control','-s','true');wait(lambda:xf('-p','/xfce-tweaks/status/victus-control')=='active')
  xdo('mousemove',x-250,210);xdo('click',1)
  window=wait(lambda:run('xdotool','search','--name','^HP Victus/Omen Control$'))
  print('PASS: disable/re-enable restores widget and saved position')
  desktop_windows=run('xdotool','search','--class','xfdesktop').splitlines()
  assert any('_NET_WM_WINDOW_TYPE_DESKTOP' in run('xprop','-id',w,'_NET_WM_WINDOW_TYPE') for w in desktop_windows)
  assert '_NET_WM_WINDOW_TYPE_NORMAL' in run('xprop','-id',window,'_NET_WM_WINDOW_TYPE')
  print('PASS: widget stays in desktop window; controls use a normal window')
  xdo('windowactivate','--sync',window);xdo('key','alt+F4')
  before=xf('-lv');desktop.terminate();desktop.wait(timeout=5)
  desktop=subprocess.Popen([str(ROOT/'build-host/src/xfdesktop'),'--disable-wm-check'],stdout=log,stderr=log);time.sleep(2)
  xdo('mousemove',x-250,210);xdo('click',1)
  window=wait(lambda:run('xdotool','search','--name','^HP Victus/Omen Control$'))
  xdo('windowactivate','--sync',window);xdo('key','alt+F4')
  assert xf('-p','/xfce-tweaks/victus-control/appearance/image')==str(image_path)
  assert float(xf('-p','/xfce-tweaks/victus-control/appearance/radius'))==24
  assert int(xf('-p',key+'/width'))==280 and int(xf('-p',key+'/height'))==220
  print('PASS: desktop restart restores widget position and appearance')
 finally:
  if desktop:desktop.terminate();desktop.wait(timeout=5)
  wm.terminate();wm.wait(timeout=5);log.close()
