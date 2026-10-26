#!/usr/bin/env python3
"""Fixture tests: never write real hardware."""
import importlib.util,json,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'modules/victus-control/assets'
spec=importlib.util.spec_from_file_location('hardware',ASSETS/'hardware.py');hw=importlib.util.module_from_spec(spec);spec.loader.exec_module(hw)
class ControlsTest(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.h=hw.Hardware(self.root)
  files={'class/dmi/id/board_name':'8BC2','class/dmi/id/sys_vendor':'HP','devices/platform/hp-wmi/hwmon/hwmon0/name':'hp','devices/platform/hp-wmi/hwmon/hwmon0/pwm1':'0','devices/platform/hp-wmi/hwmon/hwmon0/pwm1_enable':'2','class/hwmon/hwmon1/name':'coretemp','class/hwmon/hwmon1/temp1_input':'65000','devices/platform/hp-wmi/platform-profile/profile0/name':'hp-wmi','devices/platform/hp-wmi/platform-profile/profile0/profile':'balanced','devices/platform/hp-wmi/platform-profile/profile0/choices':'low-power balanced performance','devices/platform/xfce-hp-rgb/color':'FFFFFF','devices/platform/xfce-hp-rgb/brightness':'1'}
  for name,value in files.items():
   p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(value+'\n')
  (self.root/'class/hwmon/hwmon0').symlink_to(self.root/'devices/platform/hp-wmi/hwmon/hwmon0')
 def tearDown(self):self.tmp.cleanup()
 def test_snapshot(self):
  s=self.h.snapshot();self.assertTrue(s['supported']);self.assertTrue(s['manual']);self.assertTrue(s['rgb']);self.assertEqual(s['cpu_temperature'],65)
 def test_modes(self):
  self.h.apply('fan','max');self.assertEqual(self.h.snapshot()['fan_mode'],'0');self.h.set_percent(70);self.assertEqual(self.h.snapshot()['fan_mode'],'1');self.h.apply('fan','auto');self.assertEqual(self.h.snapshot()['fan_mode'],'2')
 def test_rejections(self):
  for action,value in [('fan','arbitrary'),('profile','turbo'),('rgb','../../oops'),('brightness','5')]:
   with self.assertRaises(ValueError):self.h.apply(action,value)
  for value in [-1,101,True,'70']:
   with self.assertRaises(ValueError):self.h.set_percent(value)
 def test_manual_full_range(self):
  for speed in [0,1,20,39,40,100]:
   self.h.set_percent(speed)
   self.assertEqual((self.h.hp()/'pwm1').read_text().strip(),str(round(speed*255/100)))
  points=hw.validate_curve([[40,0],[60,20],[90,100]])
  self.assertEqual(hw.curve_speed(points,35),0)
  self.assertEqual(hw.curve_speed(points,50),10)
 def test_session_zero_request(self):
  code="import sys;sys.path.insert(0,sys.argv[1]);from hardware import Hardware,fan_session;fan_session(Hardware(sys.argv[2]))"
  p=subprocess.Popen([sys.executable,'-c',code,str(ASSETS),str(self.root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  self.assertIn('ready',p.stdout.readline());p.stdin.write(json.dumps({'mode':'manual','percent':0})+'\n');p.stdin.flush()
  self.assertEqual(json.loads(p.stdout.readline())['requested_percent'],0)
  self.assertEqual((self.h.hp()/'pwm1').read_text().strip(),'0')
  p.stdin.close();p.wait(timeout=4);self.assertEqual(p.returncode,0);self.assertEqual(self.h.snapshot()['fan_mode'],'2');p.stdout.close();p.stderr.close()
 def test_board_allowlist(self):
  (self.root/'class/dmi/id/board_name').write_text('unknown')
  with self.assertRaises(ValueError):self.h.apply('fan','max')
 def test_power_rgb(self):
  self.h.apply('profile','low-power');self.h.apply('rgb','123ABC');self.h.apply('brightness','0');s=self.h.snapshot();self.assertEqual(s['profile'],'low-power');self.assertEqual(s['color'],'123ABC');self.assertEqual(s['brightness'],0)
 def add_cpu(self):
  for i in range(2):
   policy=self.root/f'devices/system/cpu/cpufreq/policy{i}';policy.mkdir(parents=True)
   for name,value in {'scaling_driver':'intel_pstate','scaling_governor':'powersave','scaling_available_governors':'powersave performance','energy_performance_preference':'balance_performance','energy_performance_available_preferences':'power balance_power balance_performance performance'}.items():(policy/name).write_text(value)
  p=self.root/'devices/system/cpu/intel_pstate/no_turbo';p.parent.mkdir();p.write_text('0')
 def test_coordinated_power(self):
  self.add_cpu()
  for profile,epp,turbo in [('low-power','power','1'),('performance','performance','0'),('balanced','balance_performance','0')]:
   self.h.apply('profile',profile);s=self.h.snapshot();self.assertEqual(s['cpu_preferences'],[epp]);self.assertEqual(s['no_turbo'],turbo);self.assertEqual(s['profile'],profile)
 def test_power_rollback(self):
  self.add_cpu();original_write=self.h.write
  def fail(path,value):
   if path.name=='energy_performance_preference' and path.parent.name=='policy1' and value=='power':raise OSError('fixture failure')
   original_write(path,value)
  self.h.write=fail
  with self.assertRaises(OSError):self.h.apply('profile','low-power')
  self.assertEqual(self.h.snapshot()['profile'],'balanced');self.assertEqual(self.h.snapshot()['cpu_preferences'],['balance_performance']);self.assertEqual(self.h.snapshot()['no_turbo'],'0')
 def test_user_curve_completion(self):
  points=hw.parse_curve('40:0, 60:40, 75:40, 85:60')
  self.assertEqual(points,[[40,0],[60,40],[75,40],[85,60]])
  self.assertEqual(hw.curve_speed(points,65),40)
  self.assertEqual(hw.curve_speed(points,87.5),60)
  self.assertEqual(hw.curve_speed(points,90),60)
  self.assertEqual(hw.parse_curve('40:0, 85:100'),[[40,0],[85,100]])
  for text in ['40:0, 30:50','40:-1, 80:100','bad','40:20:30, 80:100']:
   with self.assertRaises(ValueError):hw.parse_curve(text)
 def test_independent_curve_safety(self):
  # A deliberately inadequate curve must still force full speed at 90°C.
  points=hw.parse_curve('60:40, 75:40, 85:60, 91:100')
  control=hw.CurveController(self.h,points)
  self.assertEqual(control.step(100)['requested_percent'],40)
  sensor=self.root/'class/hwmon/hwmon1/temp1_input'
  sensor.write_text('90000');status=control.step(101)
  self.assertEqual(status['requested_percent'],100);self.assertTrue(status['thermal_override']);self.assertEqual(status['fan_mode'],'0')
  sensor.write_text('95000');self.assertEqual(control.step(102)['requested_percent'],100)
  weak=hw.CurveController(self.h,[[40,0],[100,0]])
  self.assertEqual(weak.step(103)['requested_percent'],100)
  sensor.write_text('65000');mode=self.h.hp()/'pwm1_enable';mode.write_text('2')
  weak.step(104);self.assertEqual(mode.read_text().strip(),'1')
  sensor.unlink()
  with self.assertRaises(RuntimeError):weak.step(105)
 def test_curve(self):
  points=hw.validate_curve([[40,40],[60,60],[90,100]]);self.assertEqual(hw.curve_speed(points,50),50);self.assertEqual(hw.curve_speed(points,100),100)
  for p in [[],[[60,70],[50,100]],[[40,-1],[90,100]],[[40,80],[90,70]]]:
   with self.assertRaises(ValueError):hw.validate_curve(p)
 def test_session_eof_and_thermal_override(self):
  code='''import sys,json
sys.path.insert(0,sys.argv[1]);from hardware import Hardware,fan_session
h=Hardware(sys.argv[2]);fan_session(h)
'''
  (self.root/'class/hwmon/hwmon1/temp1_input').write_text('95000')
  p=subprocess.Popen([sys.executable,'-c',code,str(ASSETS),str(self.root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  self.assertIn('ready',p.stdout.readline());p.stdin.write(json.dumps({'mode':'manual','percent':0})+'\n');p.stdin.flush();status=json.loads(p.stdout.readline());self.assertEqual(status['requested_percent'],100);self.assertTrue(status['thermal_override']);self.assertEqual(status['fan_mode'],'1')
  p.stdin.close();p.wait(timeout=4);self.assertEqual(p.returncode,0);self.assertEqual(self.h.snapshot()['fan_mode'],'2');p.stdout.close();p.stderr.close()
 def test_session_missing_sensor_restores_auto(self):
  (self.root/'class/hwmon/hwmon1/temp1_input').unlink()
  code="import sys;sys.path.insert(0,sys.argv[1]);from hardware import Hardware,fan_session;fan_session(Hardware(sys.argv[2]))"
  p=subprocess.Popen([sys.executable,'-c',code,str(ASSETS),str(self.root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  out,err=p.communicate(json.dumps({'mode':'curve','points':[[40,0],[90,100]]})+'\n',timeout=4)
  self.assertNotEqual(p.returncode,0);self.assertIn('temperature unavailable',err);self.assertEqual(self.h.snapshot()['fan_mode'],'2')
 def test_session_watchdog(self):
  code="import sys;sys.path.insert(0,sys.argv[1]);from hardware import Hardware,fan_session;fan_session(Hardware(sys.argv[2]))"
  p=subprocess.Popen([sys.executable,'-c',code,str(ASSETS),str(self.root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  p.stdin.write(json.dumps({'mode':'curve','points':[[40,40],[90,100]]})+'\n');p.stdin.flush()
  p.wait(timeout=15);self.assertNotEqual(p.returncode,0);self.assertEqual(self.h.snapshot()['fan_mode'],'2');p.stdin.close();p.stdout.close();p.stderr.close()
if __name__=='__main__':unittest.main()
