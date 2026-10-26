#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import json,subprocess,os,time,re,sys,math
from collections import deque
from pathlib import Path
import gi
gi.require_version('Gtk','3.0')
from gi.repository import Gtk,GLib,Gio,Gdk
from hardware import Hardware
HELPER='/usr/libexec/xfce-tweaks-victus-helper'
FAN_STATUS=Path('/run/xfce-tweaks/fan-status.json')
class Controls(Gtk.Window):
    def __init__(self,monitor_key=None):
        self.monitor_key=monitor_key;self.size_spins={};self.appearance_controls={}
        super().__init__(title='HP Victus/Omen Control');self.set_border_width(18);self.set_default_size(600,700)
        self.hardware=Hardware();self.operation=None;self.timer=0;self.busy=False;self.power_samples=deque(maxlen=15);self.last_profile=None;self.appearance_timer=0;self.pending_opacity=None;self.session=None;self.heartbeat=0;self.session_watch=0;self.session_buffer=b'';self.session_ready=False
        self.connect('destroy',self.close)
        self.notebook=Gtk.Notebook();self.add(self.notebook)
        box=Gtk.Box(orientation=Gtk.Orientation.VERTICAL,spacing=12);box.set_border_width(12);scroller=Gtk.ScrolledWindow();scroller.set_policy(Gtk.PolicyType.NEVER,Gtk.PolicyType.AUTOMATIC);scroller.add(box);self.notebook.append_page(scroller,Gtk.Label(label="Controls"))
        self.identity=Gtk.Label(xalign=0);box.pack_start(self.identity,False,False,0)
        self.readings=Gtk.Label(xalign=0);self.readings.set_selectable(True);self.readings.set_line_wrap(True);box.pack_start(self.readings,False,False,0)
        self.result=Gtk.Label(xalign=0);self.result.set_line_wrap(True);self.result.set_selectable(True);box.pack_start(self.result,False,False,0)
        self.buttons=[]
        fan=Gtk.Box(spacing=8);box.pack_start(fan,False,False,0)
        for text,value in [('Automatic fans','auto'),('Maximum fans','max')]:
            button=Gtk.Button(label=text);button.connect('clicked',lambda b,v=value:self.apply('fan',v));fan.pack_start(button,True,True,0);self.buttons.append((button,'fan'))
        self.speed=Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL,0,100,1);self.speed.set_value(70)
        box.pack_start(Gtk.Label(label='Direct fan speed (%) — shared CPU/GPU request',xalign=0),False,False,0)
        box.pack_start(self.speed,False,False,0)
        manual=Gtk.Button(label='Start manual speed');manual.connect('clicked',lambda b:self.start_fans({'mode':'manual','percent':round(self.speed.get_value())}));box.pack_start(manual,False,False,0);self.buttons.append((manual,'manual'))
        box.pack_start(Gtk.Label(label='Fan curve: temperature °C : speed %, separated by commas',xalign=0),False,False,0)
        self.curve=Gtk.Entry();self.curve.set_text('40:40, 60:55, 75:75, 85:100');box.pack_start(self.curve,False,False,0)
        curve=Gtk.Button(label='Start fan curve');curve.connect('clicked',self.start_curve);box.pack_start(curve,False,False,0);self.buttons.append((curve,'manual'))
        power=Gtk.Box(spacing=8);box.pack_start(power,False,False,0)
        self.profiles=Gtk.ComboBoxText();power.pack_start(self.profiles,True,True,0)
        for value in self.hardware.snapshot()['profiles']:self.profiles.append(value,value.replace('-',' ').title())
        button=Gtk.Button(label='Apply power profile');button.connect('clicked',lambda b:self.apply('profile',self.profiles.get_active_id()));power.pack_start(button,False,False,0);self.buttons.append((button,'profile'))
        self.rgb_info=Gtk.Label(xalign=0);self.rgb_info.set_line_wrap(True);box.pack_start(self.rgb_info,False,False,0)
        rgb=Gtk.Box(spacing=8);box.pack_start(rgb,False,False,0)
        self.color=Gtk.ColorButton();rgb.pack_start(self.color,False,False,0)
        button=Gtk.Button(label='Apply keyboard color');button.connect('clicked',self.apply_color);rgb.pack_start(button,True,True,0);self.buttons.append((button,'rgb'))
        self.lights=Gtk.CheckButton(label='Keyboard lighting enabled');box.pack_start(self.lights,False,False,0)
        button=Gtk.Button(label='Apply keyboard lighting');button.connect('clicked',lambda b:self.apply('brightness','1' if self.lights.get_active() else '0'));box.pack_start(button,False,False,0);self.buttons.append((button,'rgb'))
        options=Gtk.Box(orientation=Gtk.Orientation.VERTICAL,spacing=8);options.set_border_width(12)
        appearance=Gtk.ScrolledWindow();appearance.set_policy(Gtk.PolicyType.NEVER,Gtk.PolicyType.AUTOMATIC);appearance.add(options);self.notebook.append_page(appearance,Gtk.Label(label='Appearance'))
        hint=Gtk.Label(label='Drag the widget’s bottom-right corner to resize. Its layout adapts automatically.',xalign=0);hint.set_line_wrap(True);options.pack_start(hint,False,False,0)
        row=Gtk.Box(spacing=8);options.pack_start(row,False,False,0)
        row.pack_start(Gtk.Label(label='Background'),False,False,0)
        background=Gtk.ColorButton();background.set_use_alpha(False);self.appearance_controls['color']=background;self.load_color(background,'color','#30343b');background.connect('color-set',lambda b:self.set_appearance('color',b.get_rgba().to_string(),'string'));row.pack_start(background,False,False,0)
        row.pack_start(Gtk.Label(label='Text'),False,False,0)
        text=Gtk.ColorButton();self.appearance_controls['text-color']=text;self.load_color(text,'text-color','#ffffff');text.connect('color-set',lambda b:self.set_appearance('text-color',b.get_rgba().to_string(),'string'));row.pack_start(text,False,False,0)
        shadow=Gtk.ColorButton();shadow.set_use_alpha(True);self.appearance_controls['text-shadow-color']=shadow;self.load_color(shadow,'text-shadow-color','#000000');shadow.connect('color-set',lambda b:self.set_appearance('text-shadow-color',b.get_rgba().to_string(),'string'));options.pack_start(Gtk.Label(label='Text shadow color',xalign=0),False,False,0);options.pack_start(shadow,False,False,0)
        shadow_strength=Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL,0,1,.01);self.appearance_controls['text-shadow']=shadow_strength;shadow_strength.set_value(self.appearance_number('text-shadow',.30));shadow_strength.set_digits(2);shadow_strength.connect('value-changed',lambda b:self.set_appearance('text-shadow',str(b.get_value()),'double'));options.pack_start(Gtk.Label(label='Text shadow strength',xalign=0),False,False,0);options.pack_start(shadow_strength,False,False,0)
        options.pack_start(Gtk.Label(label='Background opacity',xalign=0),False,False,0)
        opacity=Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL,0,100,1);self.appearance_controls['opacity']=opacity;opacity.set_value(self.appearance_number('opacity',.85)*100);opacity.connect('value-changed',lambda b:self.set_appearance('opacity',str(b.get_value()/100),'double'));options.pack_start(opacity,False,False,0)
        for key,label,default,minimum,maximum,step in [('hover-enter','Hover enter opacity',.95,0,1,.05),('hover-exit','Hover exit opacity',.85,0,1,.05),('radius','Corner radius',12,0,100,1),('border-width','Border width',1,0,20,1),('width','Widget width',360,200,1000,1),('height','Widget height',320,110,1000,1)]:
            line=Gtk.Box(spacing=8);line.pack_start(Gtk.Label(label=label,xalign=0),True,True,0)
            spin=Gtk.SpinButton.new_with_range(minimum,maximum,step);spin.set_value(self.appearance_number(key,default));spin.connect('value-changed',lambda b,k=key:self.set_appearance(k,str(b.get_value()),'double'));
            self.appearance_controls[key]=spin
            if key in ['width','height']:self.size_spins[key]=spin
            line.pack_end(spin,False,False,0);options.pack_start(line,False,False,0)
        border_row=Gtk.Box(spacing=8);border_row.pack_start(Gtk.Label(label='Border color',xalign=0),True,True,0)
        border=Gtk.ColorButton();border.set_use_alpha(True);self.appearance_controls['border-color']=border;self.load_color(border,'border-color','rgba(255,255,255,0.45)');border.connect('color-set',lambda b:self.set_appearance('border-color',b.get_rgba().to_string(),'string'));border_row.pack_end(border,False,False,0);options.pack_start(border_row,False,False,0)
        title=Gtk.Entry();self.appearance_controls['title']=title;title.set_text(self.get_appearance('title','Victus Control'));title.connect('changed',lambda b:self.set_appearance('title',b.get_text(),'string'));options.pack_start(Gtk.Label(label='Widget title',xalign=0),False,False,0);options.pack_start(title,False,False,0)
        for key,label,default in [('title-visible','Show title','true'),('title-bottom','Title at bottom','false')]:
            toggle=Gtk.CheckButton(label=label);self.appearance_controls[key]=toggle;toggle.set_active(self.get_appearance(key,default)=='true');toggle.connect('toggled',lambda b,k=key:self.set_appearance(k,str(b.get_active()).lower(),'bool'));options.pack_start(toggle,False,False,0)
        alignment=Gtk.ComboBoxText()
        for value in ['left','center','right']:alignment.append(value,value.title())
        self.appearance_controls['title-alignment']=alignment;alignment.set_active_id(self.get_appearance('title-alignment','center'));alignment.connect('changed',lambda b:self.set_appearance('title-alignment',b.get_active_id() or 'center','string'));options.pack_start(Gtk.Label(label='Title alignment',xalign=0),False,False,0);options.pack_start(alignment,False,False,0)
        image=Gtk.FileChooserButton(title='Choose widget background image',action=Gtk.FileChooserAction.OPEN)
        self.appearance_controls['image']=image;image_filter=Gtk.FileFilter();image_filter.set_name('Images');image_filter.add_pixbuf_formats();image.add_filter(image_filter)
        filename=self.get_appearance('image','')
        if filename:image.set_filename(filename)
        image.connect('file-set',lambda b:self.set_appearance('image',b.get_filename() or '','string'));options.pack_start(image,False,False,0)
        clear=Gtk.Button(label='Clear background image');clear.connect('clicked',lambda b:(image.unselect_all(),self.set_appearance('image','','string')));options.pack_start(clear,False,False,0)
        reset=Gtk.Button(label='Restore default appearance');reset.connect('clicked',lambda b:self.reset_appearance());options.pack_start(reset,False,False,0)
        note=Gtk.Label(label='Hardware changes require administrator authentication.\nCurves keep running after this window closes.\nManual speed runs only while this panel is open.\nAt 90°C or higher, fans are forced to 100%.\nUse Automatic fans to stop the background curve. Firmware may clamp requested speeds.',xalign=0);note.set_line_wrap(True);box.pack_end(note,False,False,0)
        self.preferences=Path(os.environ.get('XDG_CONFIG_HOME',str(Path.home()/'.config')))/'xfce-tweaks/victus-control.json'
        try:
            saved=json.loads(self.preferences.read_text());speed=saved.get('speed',70)
            if type(speed) is int and 0<=speed<=100:self.speed.set_value(speed)
            curve=saved.get('curve')
            if isinstance(curve,str) and len(curve)<256:self.curve.set_text(curve)
        except (OSError,ValueError,TypeError):pass
        self.refresh();self.timer=GLib.timeout_add_seconds(2,self.refresh);self.show_all()
    def appearance_number(self,key,default):
        try:
            value=float(self.get_appearance(key,str(default)));return value if math.isfinite(value) else default
        except ValueError:return default
    def set_monitor_key(self,key):
        self.monitor_key=key
        for name,spin in self.size_spins.items():spin.set_value(self.appearance_number(name,360 if name=='width' else 320))
    def get_appearance(self,key,default):
        if key in ['width','height'] and self.monitor_key:
            result=subprocess.run(['xfconf-query','-c','xfce4-desktop','-p',self.monitor_key+'/'+key],text=True,capture_output=True)
            if result.returncode==0:return result.stdout.strip()
        result=subprocess.run(['xfconf-query','-c','xfce4-desktop','-p','/xfce-tweaks/victus-control/appearance/'+key],text=True,capture_output=True)
        return result.stdout.strip() if result.returncode==0 else default
    def set_appearance(self,key,value,kind):
        if key=='opacity':
            self.pending_opacity=value
            if not self.appearance_timer:self.appearance_timer=GLib.timeout_add(100,self.flush_opacity)
            return
        self.write_appearance(key,value,kind)
    def flush_opacity(self):
        self.appearance_timer=0
        if self.pending_opacity is not None:self.write_appearance('opacity',self.pending_opacity,'double');self.pending_opacity=None
        return GLib.SOURCE_REMOVE
    def write_appearance(self,key,value,kind):
        path='/xfce-tweaks/victus-control/appearance/'+key
        if key in ['width','height'] and self.monitor_key:path=self.monitor_key+'/'+key;value=str(round(float(value)));kind='int'
        arguments=['xfconf-query','-c','xfce4-desktop','-p',path]
        result=subprocess.run(arguments+['-n','-t',kind,'-s',value],capture_output=True)
        if result.returncode:subprocess.run(arguments+['-s',value],capture_output=True)
    def load_color(self,button,key,default):
        color=Gdk.RGBA()
        if not color.parse(self.get_appearance(key,default)):color.parse(default)
        button.set_rgba(color)
    def reset_appearance(self):
        for key,default,kind in [('color','','string'),('text-color','','string'),('image','','string'),('opacity','0.85','double'),('hover-enter','0.95','double'),('hover-exit','0.85','double'),('radius','12','double'),('border-width','1','double'),('border-color','rgba(255,255,255,0.45)','string'),('text-shadow-color','#000000','string'),('text-shadow','0.30','double'),('title','Victus Control','string'),('title-visible','true','bool'),('title-bottom','false','bool'),('title-alignment','center','string'),('width','360','double'),('height','320','double')]:self.set_appearance(key,default,kind)
        for name,spin in self.size_spins.items():spin.set_value(360 if name=='width' else 320)
        for key,value in [('color','#30343b'),('text-color','#ffffff'),('text-shadow-color','#000000'),('border-color','rgba(255,255,255,0.45)')]:
            color=Gdk.RGBA();color.parse(value);self.appearance_controls[key].set_rgba(color)
        self.appearance_controls['opacity'].set_value(85)
        self.appearance_controls['text-shadow'].set_value(.30)
        for key,value in [('hover-enter',.95),('hover-exit',.85),('radius',12),('border-width',1)]:self.appearance_controls[key].set_value(value)
        self.appearance_controls['title'].set_text('Victus Control')
        self.appearance_controls['title-visible'].set_active(True);self.appearance_controls['title-bottom'].set_active(False)
        self.appearance_controls['title-alignment'].set_active_id('center');self.appearance_controls['image'].unselect_all()
        listed=subprocess.run(['xfconf-query','-c','xfce4-desktop','-l'],capture_output=True,text=True)
        for path in listed.stdout.splitlines():
            if path.startswith('/xfce-tweaks/victus-control/monitors/') and path.endswith(('/width','/height')):subprocess.run(['xfconf-query','-c','xfce4-desktop','-p',path,'-r'],capture_output=True)
    def refresh(self):
        for name,spin in self.size_spins.items():
            if not spin.has_focus():spin.set_value(self.appearance_number(name,360 if name=='width' else 320))
        info=self.hardware.snapshot();self.identity.set_text(f"{info['model']}\nBoard {info['board']} · BIOS {info['bios']} · {info['sku']}")
        fans=' / '.join('unavailable' if f is None else str(f)+' RPM' for f in info['fans']) or 'unavailable'
        temp='unavailable' if info['cpu_temperature'] is None else f"{info['cpu_temperature']:.0f} °C"
        mode={'0':'Maximum','1':'Manual / curve','2':'Automatic'}.get(info['fan_mode'],'Unknown')
        if self.last_profile!=info['profile']:self.power_samples.clear();self.last_profile=info['profile']
        battery=info['battery'];battery_text=f"{battery['capacity']}% · {battery['status']}" if battery else 'unavailable'
        power_text='Battery draw: unavailable (requires discharging battery)'
        if battery and battery['status']=='Discharging':
            try:
                watts=int(battery['power_now'])/1000000
                now=time.monotonic()
                if not self.power_samples or now-self.power_samples[-1][0]>=1.5:self.power_samples.append((now,watts))
                average=sum(v for _,v in self.power_samples)/len(self.power_samples)
                duration=round(self.power_samples[-1][0]-self.power_samples[0][0])
                power_text=f'Battery draw: {watts:.1f} W now · {average:.1f} W average ({duration}s)'
            except (ValueError,TypeError):pass
        cpu=' / '.join(info['cpu_preferences']) or 'unavailable'
        boost={'0':'enabled','1':'disabled'}.get(info['no_turbo'],'unavailable')
        sensors=" · ".join(f"{k}: {v:.0f} °C" for k,v in info['temperature_sensors'].items())
        self.readings.set_text(f"{sensors}\nCPU: {temp}\nFans: {fans} · {mode}\nPower profile: {info['profile'] or 'unavailable'}\nCPU preference: {cpu} · Turbo: {boost}\nBattery: {battery_text}\n{power_text}")
        if self.profiles.get_active_id() is None:self.profiles.set_active_id(info['profile'])
        self.rgb_info.set_text('Keyboard RGB is available (static color and on/off).' if info['rgb'] else 'Keyboard RGB requires the HP companion driver; see the module installation instructions.')
        helper=Path(HELPER).is_file()
        for button,cap in self.buttons:
            supported={'manual':info['manual'],'fan':info['fan_control'],'profile':bool(info['profiles']),'rgb':info['rgb']}[cap]
            button.set_sensitive(not self.busy and helper and info['supported'] and supported)
        if not hasattr(self,'initialized_rgb'):
            self.lights.set_active(bool(info['brightness']))
            if info['color']:
                color=Gdk.RGBA();color.parse('#'+info['color']);self.color.set_rgba(color)
            self.initialized_rgb=True
        self.color.set_sensitive(info['rgb'] and not self.busy)
        if not self.session and not self.busy:
            try:
                status=json.loads(FAN_STATUS.read_text())
                if status.get('mode')=='curve' and time.time()-status.get('updated',0)<5:
                    extra=' · FULL-SPEED THERMAL OVERRIDE' if status.get('thermal_override') else ''
                    self.result.set_text(f"Background curve active: {status['requested_percent']}% at {status['temperature']:.0f}°C{extra}\nContinues with this window closed. Automatic fans stops it.")
                elif status.get('error'):self.result.set_text('Curve stopped; firmware Auto restored: '+status['error'])
                elif status.get('mode')=='auto' and self.result.get_text().startswith('Background curve'):self.result.set_text('Background curve stopped. Firmware automatic fan control is active.')
                elif status.get('mode')=='curve' and self.result.get_text().startswith('Background curve'):self.result.set_text('Background curve status is stale; watchdog recovery may be in progress.')
            except (OSError,ValueError,KeyError,TypeError):pass
        if not helper:self.result.set_text('Install the privileged hardware helper using this module’s install-helper.sh to enable supported controls.')
        return True
    def start_curve(self,button):
        try:
            from hardware import parse_curve
            points=parse_curve(self.curve.get_text())
            self.curve.set_text(', '.join(f'{temperature}:{speed}' for temperature,speed in points))
            self.start_background_curve(points)
        except ValueError as exc:self.result.set_text(str(exc))
    def start_background_curve(self,points):
        if self.busy or not self.stop_fans():return
        self.busy=True;self.result.set_text('Starting background curve controller; authenticate if prompted…')
        self.operation=Gio.Subprocess.new(['pkexec',HELPER,'curve-start',json.dumps(points)],Gio.SubprocessFlags.STDOUT_PIPE|Gio.SubprocessFlags.STDERR_PIPE)
        self.operation.communicate_utf8_async(None,None,self.finished,None)
    def session_output(self,fd,condition):
        if not self.session:return False
        try:
            while True:
                data=os.read(fd,4096)
                if not data:break
                self.session_buffer+=data
                if len(self.session_buffer)>65536:raise ValueError('Controller response too large')
                while b'\n' in self.session_buffer:
                    line,self.session_buffer=self.session_buffer.split(b'\n',1)
                    try:status=json.loads(line)
                    except ValueError:continue
                    if status.get('ready'):self.session_ready=True;self.result.set_text('Controller authorized; waiting for first fan request…')
                    if 'requested_percent' in status:
                        fans=' / '.join(str(v)+' RPM' for v in status.get('fans',[])) or 'unavailable'
                        extra=' · THERMAL OVERRIDE' if status.get('thermal_override') else ''
                        self.result.set_text(f"Active {status['mode']}: {status['requested_percent']}% requested at {status['temperature']:.0f}°C · Mode {status.get('fan_mode','?')} · Reported fans {fans}{extra}\nRequest acknowledged by driver; RPM telemetry may be unreliable on this board.")
                    if status.get('restored'):self.result.set_text('Automatic fan control restored.')
        except BlockingIOError:pass
        except (OSError,ValueError):pass
        if condition & (GLib.IO_HUP|GLib.IO_ERR):self.session_watch=0;return False
        return True
    def close_session_output(self):
        if self.session_watch:GLib.source_remove(self.session_watch);self.session_watch=0
        if self.session and self.session.stdout:self.session.stdout.close()
    def send_session(self,command):
        if self.session and self.session.poll() is None:
            try:self.session.stdin.write(json.dumps(command)+'\n');self.session.stdin.flush()
            except (BrokenPipeError,OSError):pass
    def session_tick(self):
        if not self.session:return False
        if self.session.poll() is not None:
            error=self.session.stderr.read().strip()
            try:error=json.loads(error)['error']
            except (ValueError,KeyError):pass
            self.close_session_output()
            self.session.stderr.close()
            self.result.set_text('Fan controller stopped. '+error);self.session=None;self.heartbeat=0;self.session_watch=0;self.session_buffer=b'';self.session_ready=False;return False
        self.send_session({'heartbeat':True});return True
    def stop_fans(self):
        if self.heartbeat:GLib.source_remove(self.heartbeat);self.heartbeat=0
        if self.session:
            try:self.session.stdin.close()
            except OSError:pass
            try:self.session.wait(timeout=3);self.session.stderr.close()
            except subprocess.TimeoutExpired:
                self.result.set_text('Waiting for the previous fan controller to stop.');return False
            self.close_session_output();self.session=None
        return True
    def start_fans(self,command):
        if self.busy:return
        if self.session and self.session.poll() is not None:
            self.close_session_output();self.session.stderr.close();self.session.stdin.close();self.session=None
            if self.heartbeat:GLib.source_remove(self.heartbeat);self.heartbeat=0
        if not self.session:
            self.session=subprocess.Popen(['pkexec',HELPER,'fan-session'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            self.session_buffer=b'';self.session_ready=False
            os.set_blocking(self.session.stdout.fileno(),False)
            self.session_watch=GLib.io_add_watch(self.session.stdout.fileno(),GLib.IO_IN|GLib.IO_HUP|GLib.IO_ERR,self.session_output)
            self.heartbeat=GLib.timeout_add_seconds(2,self.session_tick)
        self.send_session(command)
        self.result.set_text('Fan controller requested; authenticate if prompted. Closing this panel restores automatic fans.')
    def apply_color(self,button):
        c=self.color.get_rgba();self.apply('rgb',''.join(f'{round(x*255):02x}' for x in [c.red,c.green,c.blue]))
    def apply(self,action,value):
        if self.busy or value is None:return
        if action in ['fan','profile'] and not self.stop_fans():return
        self.busy=True;self.result.set_text('Waiting for authorization…');self.refresh()
        self.operation=Gio.Subprocess.new(['pkexec',HELPER,action,value],Gio.SubprocessFlags.STDOUT_PIPE|Gio.SubprocessFlags.STDERR_PIPE)
        self.operation.communicate_utf8_async(None,None,self.finished,None)
    def finished(self,proc,result,data):
        try:
            ok,out,err=proc.communicate_utf8_finish(result)
            if proc.get_successful():self.result.set_text('Applied successfully.')
            else:
                try:message=json.loads(err)['error']
                except (ValueError,KeyError):message=err.strip() or 'Authorization cancelled or operation rejected.'
                self.result.set_text(message)
        except GLib.Error as exc:self.result.set_text(str(exc))
        self.busy=False;self.operation=None;self.refresh()
    def close(self,*unused):
        self.stop_fans()
        if self.appearance_timer:GLib.source_remove(self.appearance_timer);self.flush_opacity()
        try:
            self.preferences.parent.mkdir(parents=True,exist_ok=True)
            temporary=self.preferences.with_suffix('.tmp');temporary.write_text(json.dumps({'speed':round(self.speed.get_value()),'curve':self.curve.get_text()}));temporary.replace(self.preferences)
        except OSError:pass
        if self.timer:GLib.source_remove(self.timer)
        application=self.get_application()
        if application:application.quit()
        else:Gtk.main_quit()
class Application(Gtk.Application):
    def __init__(self):
        super().__init__(application_id='org.xfce.Tweaks.HpControls',flags=Gio.ApplicationFlags.HANDLES_COMMAND_LINE);self.monitor_key=None
    def do_command_line(self,command):
        args=command.get_arguments()[1:]
        if args:
            if len(args)!=2 or args[0]!='--monitor-key' or not re.fullmatch(r'/xfce-tweaks/victus-control/monitors/[0-9a-f]{64}',args[1]):return 2
            self.monitor_key=args[1]
        self.activate();return 0
    def do_activate(self):
        windows=self.get_windows()
        if windows:windows[0].set_monitor_key(self.monitor_key);windows[0].present()
        else:
            window=Controls(self.monitor_key);self.add_window(window);window.present()
if __name__=='__main__':Application().run(sys.argv)
