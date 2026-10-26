#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse, sys, subprocess, gi
gi.require_version('Gtk','3.0'); gi.require_version('Xfconf','0')
from gi.repository import Gtk, Gdk, Gio, Xfconf
NAMES={'cpu-monitor':'CPU Monitor','gpu-monitor':'GPU Monitor','memory-monitor':'Memory Monitor','storage-monitor':'Storage Monitor'}
class Settings(Gtk.Application):
 def __init__(self,ident,monitor):
  super().__init__(application_id='org.xfce.Tweaks.'+ident.replace('-',''),flags=Gio.ApplicationFlags.HANDLES_COMMAND_LINE);self.ident=ident;self.prefix='/xfce-tweaks/'+ident;self.monitor=monitor
 def do_command_line(self,cmd):
  a=cmd.get_arguments()
  if '--monitor-key' in a:self.monitor=a[a.index('--monitor-key')+1]
  self.activate();return 0
 def do_activate(self):
  if getattr(self,'window',None):self.window.present();return
  Xfconf.init();self.channel=Xfconf.Channel.get('xfce4-desktop');self.window=Gtk.ApplicationWindow(application=self,title=NAMES[self.ident]+' Settings');self.window.set_default_size(520,600);book=Gtk.Notebook();self.window.add(book)
  page=self.page(book,'Monitor')
  if self.ident=='gpu-monitor':self.combo(page,'GPU telemetry','monitor/gpu',[('all','Show available GPUs'),('nvidia','NVIDIA only'),('intel','Intel only')],'all')
  elif self.ident=='storage-monitor':
   mounts=self.get('monitor/mounts',self.get('monitor/mount','/'));view=Gtk.TextView();view.set_wrap_mode(Gtk.WrapMode.WORD_CHAR);view.get_buffer().set_text(mounts);view.set_size_request(-1,90);scroll=Gtk.ScrolledWindow();scroll.set_min_content_height(90);scroll.add(view);self.row(page,'Drives / mount paths',scroll)
   note=Gtk.Label(label='Enter one mounted path per line. Browse can add several mounted drives at once.');note.set_xalign(0);note.set_line_wrap(True);page.pack_start(note,False,False,0)
   view.get_buffer().connect('changed',lambda b:self.set('monitor/mounts',b.get_text(b.get_start_iter(),b.get_end_iter(),True)))
   button=Gtk.Button(label='Browse and add drives…');page.pack_start(button,False,False,0)
   def browse(b):
    d=Gtk.FileChooserDialog(title='Choose mounted drive paths',parent=self.window,action=Gtk.FileChooserAction.SELECT_FOLDER,buttons=('_Cancel',Gtk.ResponseType.CANCEL,'_Add selected',Gtk.ResponseType.ACCEPT));d.set_select_multiple(True)
    if d.run()==Gtk.ResponseType.ACCEPT:
     buf=view.get_buffer();existing=buf.get_text(buf.get_start_iter(),buf.get_end_iter(),True).splitlines();paths=d.get_filenames()
     for path in paths:
      if path not in existing:existing.append(path)
     buf.set_text('\n'.join(existing))
    d.destroy()
   button.connect('clicked',browse)
  else:
   note=Gtk.Label(label='Updates once per second using Linux procfs, sysfs and hardware telemetry.');note.set_xalign(0);note.set_line_wrap(True);page.pack_start(note,False,False,0)
  a=self.page(book,'Appearance')
  title=Gtk.Entry();title.set_text(self.get('appearance/title',NAMES[self.ident]));title.connect('changed',lambda w:self.set('appearance/title',w.get_text()));self.row(a,'Widget title',title)
  self.check_bool(a,'Show title','appearance/title-visible',True)
  self.check_bool(a,'Title at bottom','appearance/title-bottom',False)
  self.combo(a,'Title alignment','appearance/title-alignment',[('left','Left'),('center','Center'),('right','Right')],'center')
  self.combo(a,'Preset','appearance/preset',[('theme','System theme'),('dark','Midnight'),('light','Paper'),('glass','Glass')],'theme',self.preset)
  for label,key,fallback in [('Background color','color','#242933'),('Text color','text-color','#ffffff'),('Border color','border-color','rgba(255,255,255,0.45)'),('Text shadow color','text-shadow-color','#000000')]:
   b=Gtk.ColorButton();b.set_use_alpha(True);c=Gdk.RGBA();c.parse(self.get('appearance/'+key,fallback));b.set_rgba(c);b.connect('color-set',lambda w,k=key:self.set('appearance/'+k,w.get_rgba().to_string()));self.row(a,label,b)
  self.check(a,'Use theme colors','appearance/theme',True,self.theme)
  image=Gtk.FileChooserButton(title='Choose widget background image',action=Gtk.FileChooserAction.OPEN);flt=Gtk.FileFilter();flt.set_name('Images');flt.add_pixbuf_formats();image.add_filter(flt);path=self.get('appearance/image','')
  if path:image.set_filename(path)
  image.connect('file-set',lambda b:self.set('appearance/image',b.get_filename() or ''));self.row(a,'Background image',image);clear=Gtk.Button(label='Clear background image');clear.connect('clicked',lambda b:(image.unselect_all(),self.set('appearance/image','')));a.pack_start(clear,False,False,0)
  for label,key,default,maximum in [('Background opacity','opacity',.85,1),('Hover enter opacity','hover-enter',.95,1),('Hover exit opacity','hover-exit',.85,1),('Rounded corners','radius',12,80),('Border width','border-width',1,20),('Text scale','text-scale',1,2),('Text shadow strength','text-shadow',.3,1),('Widget width','width',360,1000),('Widget height','height',280,1000)]:
   scale=Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL,0 if key not in ('text-scale','width','height') else (.5 if key=='text-scale' else (200 if key=='width' else 110)),maximum,.01 if maximum<=2 else 1);scale.set_value(self.channel.get_double(self.prefix+'/appearance/'+key,default));scale.set_digits(2 if maximum<=2 else 0);scale.connect('value-changed',lambda b,k=key:self.channel.set_double(self.prefix+'/appearance/'+k,b.get_value()));self.row(a,label,scale)
  reset=Gtk.Button(label='Restore default appearance');reset.connect('clicked',self.reset_appearance);a.pack_start(reset,False,False,0)
  self.window.show_all()
 def page(self,book,title):
  box=Gtk.Box(orientation=Gtk.Orientation.VERTICAL,spacing=12,margin=16);sc=Gtk.ScrolledWindow();sc.set_policy(Gtk.PolicyType.NEVER,Gtk.PolicyType.AUTOMATIC);sc.add(box);book.append_page(sc,Gtk.Label(label=title));return box
 def row(self,box,label,control):
  row=Gtk.Box(spacing=12);lab=Gtk.Label(label=label,xalign=0);lab.set_size_request(150,-1);row.pack_start(lab,False,False,0);row.pack_start(control,True,True,0);box.pack_start(row,False,False,0)
 def get(self,key,default):return self.channel.get_string(self.prefix+'/'+key,default)
 def set(self,key,value):self.channel.set_string(self.prefix+'/'+key,str(value))
 def check(self,box,label,key,default,callback=None):
  b=Gtk.CheckButton(label=label);b.set_active(self.get(key,str(default).lower())=='true');b.connect('toggled',lambda w:(self.set(key,str(w.get_active()).lower()),callback(w.get_active()) if callback else None));box.pack_start(b,False,False,0)
 def check_bool(self,box,label,key,default):
  b=Gtk.CheckButton(label=label);b.set_active(self.channel.get_bool(self.prefix+'/'+key,default));b.connect('toggled',lambda w:self.channel.set_bool(self.prefix+'/'+key,w.get_active()));box.pack_start(b,False,False,0)
 def combo(self,box,label,key,values,default,callback=None):
  c=Gtk.ComboBoxText()
  for value,text in values:c.append(value,text)
  c.set_active_id(self.get(key,default));c.connect('changed',lambda w:(self.set(key,w.get_active_id()),callback(w.get_active_id()) if callback else None));self.row(box,label,c)
 def theme(self,on):
  if on:self.set('appearance/color','');self.set('appearance/text-color','')
 def preset(self,name):
  vals={'theme':('','',.85),'dark':('#202631','#f5f7ff',.94),'light':('#faf8f2','#242933',.95),'glass':('#202631','#ffffff',.25)};bg,fg,op=vals[name];self.set('appearance/color',bg);self.set('appearance/text-color',fg)
  for k in ('opacity','hover-enter','hover-exit'):self.channel.set_double(self.prefix+'/appearance/'+k,op)
  self.set('appearance/theme',str(name=='theme').lower())
 def reset_appearance(self,*unused):
  defaults={'color':'','text-color':'','image':'','border-color':'rgba(255,255,255,0.45)','text-shadow-color':'#000000','title':NAMES[self.ident],'preset':'theme'}
  for key,value in defaults.items():self.channel.set_string(self.prefix+'/appearance/'+key,value)
  for key,value in {'opacity':.85,'hover-enter':.95,'hover-exit':.85,'radius':12,'border-width':1,'text-scale':1.,'text-shadow':.30,'width':360,'height':280}.items():self.channel.set_double(self.prefix+'/appearance/'+key,value)
  for key,value in {'theme':True,'title-visible':True,'title-bottom':False}.items():self.channel.set_bool(self.prefix+'/appearance/'+key,value)
  self.channel.set_string(self.prefix+'/appearance/title-alignment','center')
  listed=subprocess.run(['xfconf-query','-c','xfce4-desktop','-l'],capture_output=True,text=True)
  for path in listed.stdout.splitlines():
   if path.startswith(self.prefix+'/monitors/') and path.endswith(('/width','/height')):subprocess.run(['xfconf-query','-c','xfce4-desktop','-p',path,'-r'],capture_output=True)
  self.window.destroy();self.window=None;GLib.idle_add(self.do_activate)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--id',choices=NAMES,required=True);p.add_argument('--monitor-key',default='');a=p.parse_args();raise SystemExit(Settings(a.id,a.monitor_key).run(sys.argv))
