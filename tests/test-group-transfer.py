#!/usr/bin/env python3
"""Group + native-handle transfer via actual X11 pointer events on a private bus."""
import os,sys,subprocess,tempfile,time,selectors
from pathlib import Path
if os.environ.get('XT_GROUP_TRANSFER')!='1':
 raise SystemExit(subprocess.run(['xvfb-run','-a','-s','-screen 0 1600x700x24','dbus-run-session','--',sys.executable,__file__],env=dict(os.environ,XT_GROUP_TRANSFER='1',NO_AT_BRIDGE='1',GIO_USE_VFS='local')).returncode)
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='group-transfer-') as folder:
 for key,name in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache')]:
  directory=Path(folder)/name;directory.mkdir();os.environ[key]=str(directory)
 binary=ROOT/'build-modules/test-group-transfer';p=subprocess.Popen([binary],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
 def mouse(*args):subprocess.run(['xdotool',*map(str,args)],check=True);time.sleep(.1)
 def move(x1,y1,x2,y2):
  for i in range(1,10):mouse('mousemove',round(x1+(x2-x1)*i/9),round(y1+(y2-y1)*i/9))
 def verify(command):
  p.stdin.write(command+'\n');p.stdin.flush();selector=selectors.DefaultSelector();selector.register(p.stdout,selectors.EVENT_READ);assert selector.select(5);line=p.stdout.readline().strip();assert line.startswith('PASS'),line;print(line,flush=True);selector.close()
 try:
  assert p.stdout.readline().strip()=='READY';time.sleep(.4)
  mouse('mousemove',120,110);mouse('mousedown',1);move(120,110,1000,200);verify('right');mouse('mouseup',1);verify('released')
  mouse('mousemove',1000,200);mouse('mousedown',1);move(1000,200,120,110);verify('left');mouse('mouseup',1);verify('released')
  mouse('mousemove',120,110);mouse('mousedown',1);move(120,110,1000,200);mouse('mouseup',1);verify('right')
  p.stdin.write('quit\n');p.stdin.flush();p.wait(timeout=5);assert p.returncode==0;subprocess.run([binary,'restore'],check=True)
 finally:
  if p.poll() is None:p.terminate();p.wait(timeout=4)
