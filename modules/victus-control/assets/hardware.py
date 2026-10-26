#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read Linux HP interfaces; privileged CLI exposes only validated fixed operations."""
from pathlib import Path
import argparse,json,os,re,sys,time,select,signal,fcntl,subprocess,socket
class Hardware:
    def __init__(self,root=Path('/sys')):
        self.root=Path(root)
        profiles=Path('/usr/share/xfce-tweaks/hp-hardware-profiles.json') if Path(__file__).name=='xfce-tweaks-victus-helper' else Path(__file__).with_name('profiles.json')
        self.profiles=json.loads(profiles.read_text())['boards']
    def board_profile(self):return self.profiles.get(self.read(self.path('class/dmi/id/board_name')),{})
    def companion(self):
        name=self.board_profile().get('rgb_companion')
        if not name or not re.fullmatch(r'[a-z0-9-]+',name):return None
        p=self.path('devices/platform')/name
        return p if (p/'color').exists() else None
    def read(self,path,default=None):
        try:return Path(path).read_text().strip()
        except OSError:return default
    def path(self,path):return self.root/path
    def hp(self):
        return next((p for p in self.path('class/hwmon').glob('hwmon*') if self.read(p/'name')=='hp' and
                     (p.resolve().is_relative_to(self.path('devices/platform/hp-wmi').resolve()))),None)
    def profile(self):
        return next((p for p in self.path('devices/platform/hp-wmi/platform-profile').glob('*') if self.read(p/'name')=='hp-wmi'),None)
    def keyboard(self):
        return next((p for p in self.path('class/leds').glob('*') if 'kbd_backlight' in p.name and
                     (p/'brightness').exists()),None)
    def cpu_policies(self):
        base=self.path('devices/system/cpu/cpufreq')
        return sorted(p for p in base.glob('policy*') if re.fullmatch(r'policy[0-9]+',p.name) and self.read(p/'scaling_driver')=='intel_pstate')
    def apply_profile(self,value):
        changes=[(self.profile()/'profile',value)]
        configuration=self.board_profile().get('cpu_profiles',{}).get(value)
        policies=self.cpu_policies()
        if configuration and policies:
            epp=configuration['epp']
            if epp not in ['power','balance_power','balance_performance','performance']:raise ValueError('Invalid configured CPU preference')
            for policy in policies:
                if epp not in self.read(policy/'energy_performance_available_preferences','').split():raise ValueError('CPU energy preference is unavailable')
                if 'powersave' not in self.read(policy/'scaling_available_governors','').split():raise ValueError('CPU powersave governor is unavailable')
                changes.extend([(policy/'scaling_governor','powersave'),(policy/'energy_performance_preference',epp)])
            turbo=self.path('devices/system/cpu/intel_pstate/no_turbo')
            if turbo.exists():
                requested=configuration['no_turbo']
                if type(requested) is not int or requested not in [0,1]:raise ValueError('Invalid configured turbo setting')
                changes.append((turbo,str(requested)))
        previous=[(path,self.read(path)) for path,_ in changes]
        if any(value is None for _,value in previous):raise ValueError('A power control could not be read')
        try:
            for path,value in changes:self.write(path,value)
        except BaseException:
            for path,value in previous:
                try:self.write(path,value)
                except (OSError,RuntimeError):pass
            raise
    def snapshot(self):
        hp=self.hp();profile=self.profile();led=self.keyboard()
        dmi=self.path('class/dmi/id')
        temp=[]
        for d in self.path('class/hwmon').glob('hwmon*'):
            if self.read(d/'name')=='coretemp':
                for f in d.glob('temp*_input'):
                    try:temp.append(int(self.read(f))/1000)
                    except (TypeError,ValueError):pass
        fans=[]
        if hp:
            for f in sorted(hp.glob('fan*_input')):
                try:fans.append(int(self.read(f)))
                except (TypeError,ValueError):fans.append(None)
        companion=self.companion()
        rgb=bool(companion and self.read(companion/'color')) or bool(led and (led/'multi_intensity').exists() and set((self.read(led/'multi_index','')).split())=={'red','green','blue'})
        battery=None
        for p in self.path('class/power_supply').glob('*'):
            if self.read(p/'type')=='Battery':
                battery={k:self.read(p/k) for k in ['capacity','status','power_now','energy_full','energy_full_design']};break
        return dict(model=self.read(dmi/'product_name','Unknown laptop'),sku=self.read(dmi/'product_sku','Unknown'),
                    board=self.read(dmi/'board_name','Unknown'),bios=self.read(dmi/'bios_version','Unknown'),
                    supported=bool(self.board_profile()) and self.read(dmi/'sys_vendor')==self.board_profile().get('vendor'),
                    fans=fans,fan_mode=self.read(hp/'pwm1_enable') if hp else None,
                    fan_control=bool(hp and (hp/'pwm1_enable').exists()),
                    manual=bool(hp and (hp/'pwm1').exists()),
                    profile=self.read(profile/'profile') if profile else None,
                    profiles=self.read(profile/'choices','').split() if profile else [],
                    cpu_temperature=max(temp) if temp else None,battery=battery,rgb=rgb,
                    cpu_preferences=sorted({self.read(p/'energy_performance_preference','unknown') for p in self.cpu_policies()}),
                    no_turbo=self.read(self.path('devices/system/cpu/intel_pstate/no_turbo')),
                    brightness=int(self.read(led/'brightness','0')) if led else (int(self.read(companion/'brightness','0')) if companion else None),
                    max_brightness=int(self.read(led/'max_brightness','0')) if led else (1 if companion else None),
                    color=self.read(companion/'color') if companion else None,
                    temperature_sensors=self.temperatures())
    def temperatures(self):
        result={}
        for d in self.path('class/hwmon').glob('hwmon*'):
            values=[]
            for f in d.glob('temp*_input'):
                try:
                    n=int(self.read(f))/1000
                    if 0<=n<=125:values.append(n)
                except (ValueError,TypeError):pass
            if values:result[self.read(d/'name',d.name)]=max(values)
        return result
    def write(self,path,value):
        resolved=path.resolve()
        if not resolved.is_relative_to(self.root.resolve()):raise ValueError('Invalid hardware path')
        with path.open('w') as f:f.write(str(value)+'\n')
        if self.read(path)!=str(value):raise RuntimeError('Driver did not confirm the requested value')
    def set_percent(self,value):
        info=self.snapshot();profile=self.board_profile()
        if not info['supported'] or not info['manual']:raise ValueError('Manual fan control is unavailable')
        if type(value) is not int or not profile['minimum_percent']<=value<=100:raise ValueError('Manual fan speed must be 0–100%')
        hp=self.hp()
        try:
            if self.read(hp/'pwm1_enable')!=str(profile['fan_modes']['manual']):
                self.write(hp/'pwm1_enable',profile['fan_modes']['manual'])
            # pwm1 reads observed RPM on this driver, not the requested duty: do not compare immediately.
            with (hp/profile['manual_pwm']).open('w') as f:f.write(str(round(value*255/100))+'\n')
        except BaseException:
            self.apply('fan','auto');raise
    def apply(self,action,value):
        info=self.snapshot()
        if not info['supported']:raise ValueError('Hardware writes are restricted to HP Victus board 8BC2')
        if action=='fan':
            if value not in ['auto','max']:raise ValueError('Fan mode must be auto or max')
            hp=self.hp()
            if not hp:raise ValueError('HP fan controls are unavailable')
            self.write(hp/'pwm1_enable',self.board_profile()['fan_modes'][value])
        elif action=='profile':
            if value not in ['low-power','balanced','performance'] or value not in info['profiles']:raise ValueError('Unsupported power profile')
            self.apply_profile(value)
        elif action=='brightness':
            if not re.fullmatch(r'[0-9]{1,6}',value) or info['brightness'] is None:raise ValueError('Keyboard brightness is unavailable or invalid')
            if not 0<=int(value)<=info['max_brightness']:raise ValueError('Brightness outside supported range')
            self.write((self.keyboard() or self.companion())/'brightness',int(value))
        elif action=='rgb':
            if not info['rgb']:raise ValueError('No supported keyboard RGB interface is exposed')
            if not re.fullmatch(r'[0-9a-fA-F]{6}',value):raise ValueError('Use six hexadecimal color digits')
            companion=self.companion()
            if companion:
                self.write(companion/'color',value.upper())
                return self.snapshot()
            led=self.keyboard();colors={c:int(value[i:i+2],16)*info['max_brightness']//255 for c,i in [('red',0),('green',2),('blue',4)]}
            self.write(led/'multi_intensity',' '.join(str(colors[c]) for c in self.read(led/'multi_index').split()))
        else:raise ValueError('Unsupported operation')
        return self.snapshot()
def validate_curve(points,minimum=0):
    if not isinstance(points,list) or not 2<=len(points)<=8:raise ValueError('A curve needs 2–8 points')
    previous_temp=0;previous_speed=0
    for point in points:
        if not isinstance(point,list) or len(point)!=2:raise ValueError('Invalid curve point')
        temp,speed=point
        if type(temp) is not int or type(speed) is not int or not 25<=temp<=100 or not minimum<=speed<=100 or temp<=previous_temp or speed<previous_speed:raise ValueError('Temperatures must increase and speeds must not decrease; speed range is 0–100%')
        previous_temp,previous_speed=temp,speed
    return points
def parse_curve(text):
    """Complete a user curve with an explicit critical-temperature endpoint."""
    try:points=[[int(v.strip()) for v in item.split(':')] for item in text.split(',')]
    except (ValueError,AttributeError):raise ValueError('Use temperature:speed pairs, for example 40:0, 60:40, 85:60')
    if not 2<=len(points)<=8 or any(len(p)!=2 for p in points):raise ValueError('A curve needs 2–8 temperature:speed points')
    # Validate the supplied points before appending anything: no sorting/clamping.
    last_temp=0;last_speed=0
    for temp,speed in points:
        if not 25<=temp<=100 or not 0<=speed<=100 or temp<=last_temp or speed<last_speed:raise ValueError('Temperatures must increase (25–100°C); speeds must not decrease (0–100%)')
        last_temp,last_speed=temp,speed
    return validate_curve(points)

def curve_speed(points,temp):
    if temp<=points[0][0]:return points[0][1]
    for (t0,p0),(t1,p1) in zip(points,points[1:]):
        if temp<=t1:return round(p0+(p1-p0)*(temp-t0)/(t1-t0))
    return points[-1][1]
def fan_session(hw):
    mode=None;points=None;percent=None;last_heartbeat=time.monotonic();last_apply=0;last_speed=None;last_drop=0
    def send(**data):print(json.dumps(data),flush=True)
    def interrupted(signum,frame):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
    send(ready=True)
    try:
        while True:
            available,_,_=select.select([sys.stdin],[],[],1)
            if available:
                line=sys.stdin.readline(4097)
                if not line:break
                if len(line)>4096:raise ValueError('Command too long')
                command=json.loads(line);last_heartbeat=time.monotonic()
                if not isinstance(command,dict):raise ValueError('Invalid command')
                if command.get('heartbeat') is True:continue
                mode=command.get('mode')
                if mode=='auto':break
                if mode=='manual':
                    percent=command.get('percent')
                    if type(percent) is not int or not 0<=percent<=100:raise ValueError('Manual fan speed must be 0–100%')
                elif mode=='curve':points=validate_curve(command.get('points'))
                else:raise ValueError('Only manual, curve and auto are accepted')
                last_speed=None
            now=time.monotonic()
            if now-last_heartbeat>10:raise RuntimeError('Control heartbeat lost; restoring automatic fans')
            if mode and now-last_apply>=1:
                temperature=hw.snapshot()['cpu_temperature']
                if temperature is None or not 0<=temperature<=125:raise RuntimeError('CPU temperature unavailable; restoring automatic fans')
                speed=percent if mode=='manual' else curve_speed(points,temperature)
                if temperature>=hw.board_profile()['critical_temperature']:speed=100
                if last_speed is not None and speed<last_speed:
                    if now-last_drop<8:speed=last_speed
                    else:speed=max(speed,last_speed-10);last_drop=now
                if speed!=last_speed:hw.set_percent(speed)
                if hw.root==Path('/sys') and os.geteuid()==0:
                    status=Path('/run/xfce-tweaks/fan-request');temporary=status.with_suffix('.tmp');temporary.write_text(str(speed));os.chmod(temporary,0o644);temporary.replace(status)
                last_speed=speed;last_apply=now
                observed=hw.snapshot()
                send(mode=mode,requested_percent=speed,temperature=temperature,fan_mode=observed['fan_mode'],fans=observed['fans'],thermal_override=temperature>=hw.board_profile()['critical_temperature'])
    finally:
        try:hw.apply('fan','auto')
        finally:
            if hw.root==Path('/sys') and os.geteuid()==0:Path('/run/xfce-tweaks/fan-request').unlink(missing_ok=True)
        send(restored='auto')
SERVICE='xfce-tweaks-victus-fans.service'
RUNTIME=Path('/run/xfce-tweaks')
def atomic_json(path,value,mode=0o644):
    temporary=path.with_suffix('.tmp');temporary.write_text(json.dumps(value));os.chmod(temporary,mode);temporary.replace(path)
def notify(message):
    address=os.environ.get('NOTIFY_SOCKET')
    if address:
        if address.startswith('@'):address='\0'+address[1:]
        with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as connection:
            connection.connect(address);connection.sendall(message.encode())
class CurveController:
    """Independent critical-temperature rule; curves cannot weaken it."""
    def __init__(self,hw,points):
        self.hw=hw;self.points=validate_curve(points);self.last_speed=None;self.last_drop=0;self.last_write=0
    def step(self,now=None):
        now=time.monotonic() if now is None else now
        snapshot=self.hw.snapshot();temperature=snapshot['cpu_temperature']
        if temperature is None or not 0<=temperature<=125:raise RuntimeError('CPU temperature unavailable; restoring automatic fans')
        critical=min(90,self.hw.board_profile()['critical_temperature'])
        override=temperature>=critical
        speed=100 if override else curve_speed(self.points,temperature)
        if self.last_speed is not None and speed<self.last_speed:
            if now-self.last_drop<8:speed=self.last_speed
            else:speed=max(speed,self.last_speed-10);self.last_drop=now
        expected_mode=str(self.hw.board_profile()['fan_modes']['max' if override else 'manual'])
        if speed!=self.last_speed or snapshot['fan_mode']!=expected_mode or now-self.last_write>=5:
            if override:self.hw.apply('fan','max')
            else:self.hw.set_percent(speed)
            self.last_write=now
        self.last_speed=speed;observed=self.hw.snapshot()
        if observed['fan_mode']!=expected_mode:raise RuntimeError('Firmware did not retain requested fan mode; restoring automatic fans')
        return dict(mode='curve',requested_percent=speed,temperature=temperature,fan_mode=observed['fan_mode'],fans=observed['fans'],thermal_override=override,updated=time.time(),pid=os.getpid())
def fan_daemon(hw,points):
    running=True
    def stop(signum,frame):
        nonlocal running
        running=False
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    controller=CurveController(hw,points);error=None;ready=False
    try:
        while running:
            status=controller.step()
            atomic_json(RUNTIME/'fan-status.json',status)
            request=RUNTIME/'fan-request';temporary=request.with_suffix('.tmp');temporary.write_text(str(status['requested_percent']));os.chmod(temporary,0o644);temporary.replace(request)
            if not ready:notify('READY=1');ready=True
            notify('WATCHDOG=1')
            time.sleep(1)
    except Exception as exc:
        error=str(exc);raise
    finally:
        try:hw.apply('fan','auto')
        finally:
            (RUNTIME/'fan-request').unlink(missing_ok=True)
            atomic_json(RUNTIME/'fan-status.json',dict(mode='auto',error=error,updated=time.time()))
def service_stop():
    subprocess.run(['systemctl','stop',SERVICE],check=True,capture_output=True,text=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('action',choices=['snapshot','fan','profile','rgb','brightness','fan-session','curve-start','fan-daemon','fan-reset']);parser.add_argument('value',nargs='?');args=parser.parse_args()
    hw=Hardware()
    if args.action in ['curve-start','fan-daemon','fan-reset']:
        if os.geteuid()!=0:raise PermissionError('Administrator authentication required')
        if not hw.snapshot()['supported']:raise ValueError('Unsupported HP board')
        RUNTIME.mkdir(mode=0o755,exist_ok=True)
        if args.action=='fan-reset':
            hw.apply('fan','auto');(RUNTIME/'fan-request').unlink(missing_ok=True)
            try:previous=json.loads((RUNTIME/'fan-status.json').read_text())
            except (OSError,ValueError):previous={}
            atomic_json(RUNTIME/'fan-status.json',dict(mode='auto',error=previous.get('error'),updated=time.time()));return
        if args.action=='curve-start':
            if not args.value or len(args.value)>4096:raise ValueError('Missing or oversized curve')
            points=validate_curve(json.loads(args.value))
            atomic_json(RUNTIME/'curve.json',points,0o600)
            subprocess.run(['systemctl','restart',SERVICE],check=True,capture_output=True,text=True)
            print(json.dumps({'started':'curve','points':points}));return
        points=validate_curve(json.loads((RUNTIME/'curve.json').read_text()))
        with (RUNTIME/'victus-fans.lock').open('a') as lock:
            try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:raise RuntimeError('A fan controller is already running')
            fan_daemon(hw,points)
        return
    if args.action=='fan-session':
        if os.geteuid()!=0:raise PermissionError('Administrator authentication required')
        service_stop()
        Path('/run/xfce-tweaks').mkdir(mode=0o755,exist_ok=True)
        os.chmod('/run/xfce-tweaks',0o755)
        with open('/run/xfce-tweaks/victus-fans.lock','a') as lock:
            try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:raise RuntimeError('A fan controller is already running')
            fan_session(hw)
        return
    if args.action=='snapshot':result=hw.snapshot()
    else:
        if os.geteuid()!=0:raise PermissionError('Authenticate through the installed hardware helper')
        if args.value is None:raise ValueError('A value is required')
        if args.action in ['fan','profile']:service_stop()
        result=hw.apply(args.action,args.value)
    print(json.dumps(result))
if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as exc:print(json.dumps({'error':str(exc)}),file=sys.stderr);sys.exit(1)
