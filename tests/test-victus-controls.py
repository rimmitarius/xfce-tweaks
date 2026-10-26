#!/usr/bin/env python3
"""Private GTK + helper protocol integration; fixture sysfs only, no hardware writes."""
import os,sys,subprocess,tempfile,time,importlib.util
from pathlib import Path
if os.environ.get('XT_FAN_GUI_TEST')!='1':
 raise SystemExit(subprocess.run(['xvfb-run','-a','dbus-run-session','--',sys.executable,__file__],env=dict(os.environ,XT_FAN_GUI_TEST='1')).returncode)
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='victus-controls-') as directory:
 for key,folder in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache')]:
  path=Path(directory)/folder;path.mkdir();os.environ[key]=str(path)
 subprocess.run(['dbus-update-activation-environment','DISPLAY','HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME'],check=True)
 sys.path.insert(0,str(ROOT/'modules/victus-control/assets'))
 spec=importlib.util.spec_from_file_location('fixtures',ROOT/'tests/test-victus.py');fixtures=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixtures)
 fixture=fixtures.ControlsTest();fixture.setUp()
 import app
 from gi.repository import GLib
 app.Hardware=lambda:fixture.h
 original=app.subprocess.Popen
 def launch(args,**kwargs):
  if args[:1]==['pkexec']:
   code='import sys;sys.path.insert(0,sys.argv[1]);from hardware import Hardware,fan_session;fan_session(Hardware(sys.argv[2]))'
   args=[sys.executable,'-c',code,str(fixtures.ASSETS),str(fixture.root)]
  return original(args,**kwargs)
 app.subprocess.Popen=launch
 app.Gtk.main_quit=lambda:None
 panel=app.Controls();panel.show_all()
 def wait(predicate):
  until=time.monotonic()+6
  while time.monotonic()<until:
   while GLib.MainContext.default().pending():GLib.MainContext.default().iteration(False)
   if predicate():return
   time.sleep(.03)
  raise AssertionError(panel.result.get_text())
 daemon=None
 app.FAN_STATUS=Path(directory)/'fan-status.json'
 def background(points):
  global daemon
  code="import sys,json;from pathlib import Path;sys.path.insert(0,sys.argv[1]);import hardware;hardware.RUNTIME=Path(sys.argv[3]);hardware.fan_daemon(hardware.Hardware(sys.argv[2]),json.loads(sys.argv[4]))"
  daemon=original([sys.executable,'-c',code,str(fixtures.ASSETS),str(fixture.root),directory,__import__('json').dumps(points)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 panel.start_background_curve=background
 try:
  panel.curve.set_text('60:40, 75:40, 85:60, 91:100');panel.start_curve(None)
  wait(lambda:panel.refresh() and 'Background curve active: 40%' in panel.result.get_text())
  assert panel.curve.get_text().endswith('91:100')
  assert fixture.h.snapshot()['fan_mode']=='1'
  print('PASS GTK curve start accepts 91°C endpoint and displays background feedback')
  panel.destroy();time.sleep(2)
  assert daemon.poll() is None and fixture.h.snapshot()['fan_mode']=='1'
  assert time.time()-__import__('json').loads(app.FAN_STATUS.read_text())['updated']<3
  print('PASS closing the settings window leaves the independent curve running')
  daemon.terminate();daemon.wait(timeout=4);daemon.stdout.close();daemon.stderr.close()
  assert fixture.h.snapshot()['fan_mode']=='2'
  panel=app.Controls();panel.show_all()
  panel.start_fans({'mode':'manual','percent':0});wait(lambda:'Active manual: 0%' in panel.result.get_text())
  assert (fixture.h.hp()/'pwm1').read_text().strip()=='0'
  print('PASS manual zero request retains live acknowledgement')
  panel.stop_fans();assert fixture.h.snapshot()['fan_mode']=='2'
  panel.curve.set_text('40:0, 30:60');panel.start_curve(None)
  assert panel.session is None and 'increase' in panel.result.get_text()
  print('PASS stop restores Auto; malformed ordering clearly rejected')
 finally:
  panel.destroy()
  if daemon and daemon.poll() is None:daemon.terminate();daemon.wait(timeout=4)
  fixture.tearDown()
