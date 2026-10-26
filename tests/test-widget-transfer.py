#!/usr/bin/env python3
"""Real pointer drags between two native GTK view windows; private X server/bus."""
import os,sys,subprocess,tempfile,time,selectors
from pathlib import Path
if os.environ.get('XT_TRANSFER_TEST')!='1':
 raise SystemExit(subprocess.run(['xvfb-run','-a','-s','-screen 0 1600x700x24','dbus-run-session','--',sys.executable,__file__,*sys.argv[1:]],env=dict(os.environ,XT_TRANSFER_TEST='1',NO_AT_BRIDGE='1',GIO_USE_VFS='local')).returncode)
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='widget-transfer-') as directory:
 for key,folder in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache')]:
  path=Path(directory)/folder;path.mkdir();os.environ[key]=str(path)
 subprocess.run(['dbus-update-activation-environment','HOME','DISPLAY','XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME'],check=True)
 def mouse(*args):subprocess.run(['xdotool',*map(str,args)],check=True);time.sleep(.15)
 def drag(x1,y1,x2,y2,release=True):
  mouse('mousemove',x1,y1);mouse('mousedown',1)
  for i in range(1,9):mouse('mousemove',round(x1+(x2-x1)*i/8),round(y1+(y2-y1)*i/8))
  if release:mouse('mouseup',1)
 for ident in ['clock-widget','weather-widget','victus-control']:
  binary=ROOT/'build-modules'/('test-transfer-'+ident)
  process=subprocess.Popen([binary],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
  try:
   assert process.stdout.readline().strip()=='READY'
   time.sleep(.5)
   def verify(command):
    process.stdin.write(command+'\n');process.stdin.flush()
    selector=selectors.DefaultSelector();selector.register(process.stdout,selectors.EVENT_READ)
    assert selector.select(5),'No acknowledgement';reply=process.stdout.readline().strip();assert reply.startswith('PASS'),reply;print(ident,reply,flush=True);selector.close()
   drag(120,110,1000,200,False);verify('preview');mouse('mouseup',1);verify('forward')
   drag(1000,200,120,110);verify('back')
   drag(120,110,1000,200);verify('forward')
   process.stdin.write('quit\n');process.stdin.flush();process.wait(timeout=5);assert process.returncode==0
   subprocess.run([binary,'restore'],check=True)
  finally:
   if process.poll() is None:process.terminate();process.wait(timeout=5)
