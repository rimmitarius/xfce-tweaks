#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native single-instance appearance and content settings for desktop widgets."""
import argparse
import concurrent.futures
from pathlib import Path
import subprocess
import sys
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
import gi
gi.require_version('Gtk', '3.0'); gi.require_version('Xfconf', '0')
from gi.repository import Gtk, Gdk, Gio, GLib, Xfconf

class Settings(Gtk.Application):
    def __init__(self, identifier, monitor):
        super().__init__(application_id='org.xfce.Tweaks.' + identifier.replace('-', ''), flags=Gio.ApplicationFlags.HANDLES_COMMAND_LINE)
        self.identifier = identifier; self.prefix = '/xfce-tweaks/' + identifier; self.monitor = monitor
        self.pool = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        self.window = None; self.serial = 0
    def do_command_line(self, command):
        args = command.get_arguments()
        if '--monitor-key' in args: self.monitor = args[args.index('--monitor-key')+1]
        self.activate(); return 0
    def do_activate(self):
        if self.window: self.window.present(); return
        Xfconf.init(); self.channel = Xfconf.Channel.get('xfce4-desktop')
        self.window = Gtk.ApplicationWindow(application=self, title=('Weather' if self.identifier == 'weather-widget' else 'Clock') + ' Widget Settings')
        self.window.set_default_size(520, 620)
        book = Gtk.Notebook(); self.window.add(book)
        content = self.page(book, 'Weather' if self.identifier == 'weather-widget' else 'Time')
        if self.identifier == 'weather-widget': self.weather_page(content)
        else: self.clock_page(content)
        appearance = self.page(book, 'Appearance')
        self.combo(appearance, 'Preset', 'appearance/preset', [('theme','System theme'),('dark','Midnight'),('light','Paper'),('glass','Glass')], 'theme', self.preset)
        for label, key, fallback in [('Background color','color','#242933'),('Text color','text-color','#ffffff'),('Border color','border-color','rgba(255,255,255,0.45)')]:
            button = Gtk.ColorButton(); button.set_use_alpha(True); color = Gdk.RGBA(); color.parse(self.get('appearance/'+key, fallback)); button.set_rgba(color)
            button.connect('color-set', lambda b, k=key: self.set('appearance/'+k, b.get_rgba().to_string())); self.row(appearance,label,button)
        self.check(appearance,'Use theme colors','appearance/theme',True,self.theme)
        image = Gtk.FileChooserButton(title='Choose widget background image', action=Gtk.FileChooserAction.OPEN)
        filt=Gtk.FileFilter();filt.set_name('Images');filt.add_pixbuf_formats();image.add_filter(filt)
        path=self.get('appearance/image','')
        if path:image.set_filename(path)
        image.connect('file-set',lambda b:self.set('appearance/image',b.get_filename() or ''));self.row(appearance,'Background image',image)
        clear=Gtk.Button(label='Clear background image');clear.connect('clicked',lambda b:(image.unselect_all(),self.set('appearance/image','')));appearance.pack_start(clear,False,False,0)
        for label,key,default,maximum in [('Background opacity','opacity',.85,1),('Hover enter opacity','hover-enter',.95,1),('Hover exit opacity','hover-exit',.85,1),('Rounded corners','radius',12,80),('Border width','border-width',1,20),('Text scale','text-scale',1,2)]:
            scale=Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL,0 if key!='text-scale' else .5,maximum,.01 if maximum<=2 else 1);scale.set_value(self.channel.get_double(self.prefix+'/appearance/'+key,default));scale.set_digits(2 if maximum<=2 else 0)
            scale.connect('value-changed',lambda b,k=key:self.channel.set_double(self.prefix+'/appearance/'+k,b.get_value()));self.row(appearance,label,scale)
        shadow=Gtk.ColorButton(); shadow.set_use_alpha(True); shadow_color=Gdk.RGBA(); shadow_color.parse(self.get('appearance/text-shadow-color','#000000')); shadow.set_rgba(shadow_color)
        shadow.connect('color-set',lambda b:self.set('appearance/text-shadow-color',b.get_rgba().to_string())); self.row(appearance,'Text shadow color',shadow)
        shadow_strength=Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL,0,1,.01); shadow_strength.set_value(self.channel.get_double(self.prefix+'/appearance/text-shadow',.30)); shadow_strength.set_digits(2)
        shadow_strength.connect('value-changed',lambda b:self.channel.set_double(self.prefix+'/appearance/text-shadow',b.get_value())); self.row(appearance,'Text shadow strength',shadow_strength)
        reset=Gtk.Button(label='Restore default appearance');reset.connect('clicked',self.reset_appearance);appearance.pack_start(reset,False,False,0)
        helptext=Gtk.Label(label='Drag the card to move it.\nDrag its lower-right corner to resize.\nEach monitor remembers its own position and size.');appearance.pack_start(helptext,False,False,10)
        self.window.show_all()
    def get(self,key,default):return self.channel.get_string(self.prefix+'/'+key,default)
    def set(self,key,value):self.channel.set_string(self.prefix+'/'+key,str(value))
    def page(self,book,title):
        box=Gtk.Box(orientation=Gtk.Orientation.VERTICAL,spacing=12,margin=16);scroll=Gtk.ScrolledWindow();scroll.set_policy(Gtk.PolicyType.NEVER,Gtk.PolicyType.AUTOMATIC);scroll.add(box);book.append_page(scroll,Gtk.Label(label=title));return box
    def row(self,box,label,control):
        row=Gtk.Box(spacing=12);text=Gtk.Label(label=label,xalign=0);text.set_size_request(150,-1);row.pack_start(text,False,False,0);row.pack_start(control,True,True,0);box.pack_start(row,False,False,0)
    def combo(self,box,label,key,values,default,callback=None):
        combo=Gtk.ComboBoxText()
        for value,text in values:combo.append(value,text)
        combo.set_active_id(self.get(key,default));combo.connect('changed',lambda b:(self.set(key,b.get_active_id()),callback(b.get_active_id()) if callback else None));self.row(box,label,combo);return combo
    def check(self,box,label,key,default,callback=None):
        button=Gtk.CheckButton(label=label);button.set_active(self.get(key,str(default).lower())=='true');button.connect('toggled',lambda b:(self.set(key,str(b.get_active()).lower()),callback(b.get_active()) if callback else None));box.pack_start(button,False,False,0)
    def theme(self,enabled):
        if enabled:self.set('appearance/color','');self.set('appearance/text-color','')
    def preset(self,name):
        values={'theme':('','',.85),'dark':('#202631','#f5f7ff',.94),'light':('#faf8f2','#242933',.95),'glass':('#202631','#ffffff',.25)}
        color,text,opacity=values[name];self.set('appearance/color',color);self.set('appearance/text-color',text)
        for key in ['opacity','hover-enter','hover-exit']:self.channel.set_double(self.prefix+'/appearance/'+key,opacity)
        self.set('appearance/theme',str(name=='theme').lower())
    def reset_appearance(self,*unused):
        values={'color':'','text-color':'','image':'','border-color':'rgba(255,255,255,0.45)','text-shadow-color':'#000000','title':'Clock' if self.identifier=='clock-widget' else 'Weather','preset':'theme'}
        for key,value in values.items():self.channel.set_string(self.prefix+'/appearance/'+key,value)
        for key,value in {'opacity':.85,'hover-enter':.95,'hover-exit':.85,'radius':12,'border-width':1,'text-scale':1.,'text-shadow':.30}.items():self.channel.set_double(self.prefix+'/appearance/'+key,value)
        self.channel.set_bool(self.prefix+'/appearance/theme',True);self.channel.set_bool(self.prefix+'/appearance/title-visible',True);self.channel.set_bool(self.prefix+'/appearance/title-bottom',False)
        self.channel.set_string(self.prefix+'/appearance/title-alignment','center')
        self.reset_monitor_sizes()
        # Reopen the page so every control reflects the values just restored.
        self.window.destroy();self.window=None;GLib.idle_add(self.do_activate)
    def reset_monitor_sizes(self):
        result=subprocess.run(['xfconf-query','-c','xfce4-desktop','-l'],capture_output=True,text=True)
        for path in result.stdout.splitlines():
            if path.startswith(self.prefix+'/monitors/') and path.endswith(('/width','/height')):
                subprocess.run(['xfconf-query','-c','xfce4-desktop','-p',path,'-r'],capture_output=True)
    def clock_page(self,box):
        self.combo(box,'Display','clock/style',[('digital','Digital'),('analog','Analog')],'digital')
        self.combo(box,'Time format','clock/format',[('24','24 hour'),('12','12 hour')],'24')
        self.check(box,'Show seconds','clock/seconds',False)
        entry=Gtk.Entry();entry.set_placeholder_text('System time zone (or America/New_York)');entry.set_text(self.get('clock/timezone',''));self.row(box,'Time zone',entry)
        status=Gtk.Label(xalign=0);box.pack_start(status,False,False,0)
        def change(e):
            value=e.get_text().strip()
            try:
                if value:ZoneInfo(value)
                self.set('clock/timezone',value);status.set_text('')
            except (ZoneInfoNotFoundError,ValueError):status.set_text('Enter a valid IANA time zone, e.g. Europe/London.')
        entry.connect('changed',change)
    def weather_page(self,box):
        sys.path.insert(0,str(Path(__file__).absolute().parent));from weather import search
        location=Gtk.Label(label=self.get('weather/location','No location selected'),xalign=0);box.pack_start(location,False,False,0)
        entry=Gtk.SearchEntry();entry.set_placeholder_text('City or postal code');box.pack_start(entry,False,False,0)
        button=Gtk.Button(label='Search locations');box.pack_start(button,False,False,0)
        result=Gtk.ComboBoxText();box.pack_start(result,False,False,0)
        apply=Gtk.Button(label='Use selected location');apply.set_sensitive(False);box.pack_start(apply,False,False,0)
        status=Gtk.Label(xalign=0);status.set_line_wrap(True);box.pack_start(status,False,False,0);self.locations=[]
        def complete(future,serial):
            if serial!=self.serial:return False
            button.set_sensitive(True);result.remove_all();self.locations=[]
            try:self.locations=future.result()
            except Exception:status.set_text('Could not reach location search. Check your connection.');return False
            for i,item in enumerate(self.locations):result.append(str(i),', '.join(str(item[k]) for k in ['name','admin1','country'] if item.get(k)))
            apply.set_sensitive(bool(self.locations));result.set_active(0);status.set_text('Choose a result and apply.' if self.locations else 'No matching locations. Try a nearby city.');return False
        def begin(*unused):
            query=entry.get_text().strip()
            if len(query)<2:return
            self.serial+=1;serial=self.serial;button.set_sensitive(False);apply.set_sensitive(False);status.set_text('Searching…')
            self.pool.submit(search,query).add_done_callback(lambda future:GLib.idle_add(complete,future,serial))
        button.connect('clicked',begin);entry.connect('activate',begin)
        def choose(b):
            index=result.get_active()
            if index<0:return
            item=self.locations[index];self.set('weather/location',', '.join(str(item[k]) for k in ['name','admin1','country'] if item.get(k)))
            self.set('weather/latitude',item['latitude']);self.set('weather/longitude',item['longitude']);location.set_text(self.get('weather/location',''));status.set_text('Location saved. Weather is updating…')
        apply.connect('clicked',choose)
        self.combo(box,'Units','weather/units',[('celsius','Celsius · km/h'),('fahrenheit','Fahrenheit · mph')],'celsius')
        attribution=Gtk.LinkButton.new_with_label('https://open-meteo.com/','Weather data by Open-Meteo');box.pack_start(attribution,False,False,12)
        note=Gtk.Label(label='Free API for noncommercial use. Updates every 15 minutes.\nOnly your selected coordinates are sent for forecasts.\nLocation searches send the city/postal code you enter.\nCurrent conditions are weather model estimates.');note.set_line_wrap(True);box.pack_start(note,False,False,0)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--id',choices=['clock-widget','weather-widget'],required=True);parser.add_argument('--monitor-key',default='');args=parser.parse_args()
    application=Settings(args.id,args.monitor_key)
    raise SystemExit(application.run(sys.argv))
