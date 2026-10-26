#!/usr/bin/env python3
"""Exercise both native widgets on a private desktop and bus."""
import os,subprocess,tempfile,time,shutil,sys
from pathlib import Path
if os.environ.get('XT_WIDGET_TEST')!='1':
 sys.exit(subprocess.run(['xvfb-run','-a','-s','-screen 0 1280x900x24','dbus-run-session','--',sys.executable,__file__],env=dict(os.environ,XT_WIDGET_TEST='1')).returncode)
ROOT=Path(__file__).resolve().parents[1]
def run(*args):return subprocess.check_output(list(map(str,args)),text=True,stderr=subprocess.STDOUT).strip()
def xf(*args):return run('xfconf-query','-c','xfce4-desktop',*args)
def wait(fn):
 for _ in range(100):
  try:
   value=fn()
   if value:return value
  except subprocess.CalledProcessError:pass
  time.sleep(.1)
 raise AssertionError('Timed out')
def xdo(*args):run('xdotool',*args);time.sleep(.2)
with tempfile.TemporaryDirectory(prefix='clock-weather-') as tmp:
 root=Path(tmp)
 for key,directory in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_CACHE_HOME','cache'),('XDG_DATA_HOME','data')]:os.environ[key]=str(root/directory);(root/directory).mkdir()
 (root/'home/Desktop').mkdir();(root/'config/user-dirs.dirs').write_text('XDG_DESKTOP_DIR="$HOME/Desktop"\n')
 for ident in ['clock-widget','weather-widget','desktop-peek']:
  module=root/'modules'/ident;module.mkdir(parents=True)
  shutil.copy(ROOT/'modules'/ident/'tweak.ini',module);shutil.copy(ROOT/'build-modules'/('lib'+ident+'.so'),module)
  if (ROOT/'modules'/ident/'assets').exists():shutil.copytree(ROOT/'modules'/ident/'assets',module/'assets',ignore=shutil.ignore_patterns('__pycache__'))
 os.environ['XFCE_TWEAKS_MODULE_PATH']=str(root/'modules');os.environ['NO_AT_BRIDGE']='1';os.environ['GIO_USE_VFS']='local'
 run('dbus-update-activation-environment','DISPLAY','HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_DATA_HOME','NO_AT_BRIDGE')
 for ident in ['clock-widget','weather-widget']:xf('-p','/xfce-tweaks/enabled/'+ident,'-n','-t','bool','-s','true')
 wm=subprocess.Popen(['xfwm4','--compositor=off'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 (ROOT/'test-results').mkdir(exist_ok=True)
 log=(ROOT/'tests/clock-weather-x11.log').open('w');desktop=None
 try:
  desktop=subprocess.Popen([ROOT/'build-host/src/xfdesktop','--disable-wm-check'],stdout=log,stderr=log)
  for ident in ['clock-widget','weather-widget']:wait(lambda:xf('-p','/xfce-tweaks/status/'+ident)=='active')
  time.sleep(3)
  import gi;gi.require_version('Gdk','3.0');from gi.repository import Gdk
  screen=Gdk.get_default_root_window()
  def snap():return bytes(Gdk.pixbuf_get_from_window(screen,936,370,320,180).get_pixels())
  Gdk.pixbuf_get_from_window(screen,0,0,1280,900).savev(str(ROOT/'test-results/clock-before.png'),'png',[],[])
  before=snap();xf('-p','/xfce-tweaks/clock-widget/appearance/color','-n','-t','string','-s','#123344');time.sleep(1);Gdk.pixbuf_get_from_window(screen,0,0,1280,900).savev(str(ROOT/'test-results/clock-after.png'),'png',[],[]);assert snap()!=before
  print('PASS independent appearance updates')
  for ident,title,y in [('clock-widget','Clock',390),('weather-widget','Weather',590)]:
   xdo('mousemove',1100,y);xdo('click',1)
   window=wait(lambda:run('xdotool','search','--name','^'+title+' Widget Settings$'));xdo('windowactivate','--sync',window);xdo('key','alt+F4');time.sleep(.4)
   print('PASS '+title+' opens its own settings')
   xdo('mousemove',1100,y);xdo('mousedown',1);xdo('mousemove',750,y-100);xdo('mouseup',1)
   properties=xf('-lv');key=next(line.split()[0].rsplit('/',1)[0] for line in properties.splitlines() if '/'+ident+'/monitors/' in line and line.split()[0].endswith('/x'))
   left=int(xf('-p',key+'/x'));top=int(xf('-p',key+'/y'))
   assert left==586 and top==y-120,(left,top,y)
   xdo('mousemove',left+308,top+168);xdo('mousedown',1);xdo('mousemove',left+408,top+228);xdo('mouseup',1)
   assert int(xf('-p',key+'/width'))==420;assert int(xf('-p',key+'/height'))==240
   xf('-p','/xfce-tweaks/enabled/'+ident,'-s','false');wait(lambda:xf('-p','/xfce-tweaks/status/'+ident)=='disabled')
   xf('-p','/xfce-tweaks/enabled/'+ident,'-s','true');wait(lambda:xf('-p','/xfce-tweaks/status/'+ident)=='active')
   assert int(xf('-p',key+'/width'))==420
   print('PASS '+title+' drag, resize, disable, restore')
  xf('-p','/xfce-tweaks/clock-widget/clock/style','-n','-t','string','-s','analog');time.sleep(.4)
  Gdk.pixbuf_get_from_window(screen,0,0,1280,900).savev(str(ROOT/'test-results/clock-weather.png'),'png',[],[])
  xf('-p','/xfce-tweaks/enabled/desktop-peek','-n','-t','bool','-s','true')
  wait(lambda:xf('-p','/xfce-tweaks/status/desktop-peek')=='active')
  xdo('mousemove',500,820);xdo('click','--repeat','2','--delay','110',1)
  wait(lambda:xf('-p','/xfce-tweaks/desktop-peek/hidden')=='true')
  xdo('click','--repeat','2','--delay','110',1)
  wait(lambda:xf('-p','/xfce-tweaks/desktop-peek/hidden')=='false')
  print('PASS both widgets cooperate with desktop hide and restore')
  desktop.terminate();desktop.wait(timeout=5);desktop=subprocess.Popen([ROOT/'build-host/src/xfdesktop','--disable-wm-check'],stdout=log,stderr=log);time.sleep(2)
  assert int(xf('-p',key+'/width'))==420;assert desktop.poll() is None
  print('PASS restart restores layout; analog and digital render without errors')
 finally:
  if desktop:desktop.terminate();desktop.wait(timeout=5)
  wm.terminate();wm.wait(timeout=5);log.close()
