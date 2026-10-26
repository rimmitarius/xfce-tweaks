#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
from pathlib import Path
import os
import subprocess
import sys
import time
import gi
gi.require_version('Gtk','3.0')
from gi.repository import Gtk, GLib, Gio
from registry import discover, import_package, remove_tweak, user_modules

def command(*args):
    result=subprocess.run(['xfconf-query','-c','xfce4-desktop',*args],text=True,capture_output=True)
    if result.returncode: return None
    return result.stdout.strip()

def set_property(path, value, kind='bool'):
    result=command('-p',path,'-n','-t',kind,'-s',str(value).lower())
    if result is None:
        result=command('-p',path,'-s',str(value).lower())
    if result is None: raise RuntimeError('Could not update XFCE settings')

class Manager(Gtk.Window):
    def __init__(self, roots, managed_roots):
        super().__init__(title='XFCE Tweaks')
        self.roots=roots;self.managed_roots=managed_roots;self.switches={};self.labels={};self.defaults={};self.items={};self.host_available=False;self.closing=False;self.catalogue=();self.catalogue_source=0;self.catalogue_monitors=[]
        self.set_default_size(650,450);self.set_border_width(16)
        self.connect('destroy',Gtk.main_quit)
        box=Gtk.Box(orientation=Gtk.Orientation.VERTICAL,spacing=12);self.add(box)
        label=Gtk.Label(label='Desktop extensions',xalign=0)
        label.get_style_context().add_class('title');box.pack_start(label,False,False,0)
        self.notice=Gtk.Label(label='Changes apply immediately to an extension-enabled xfdesktop.\nYour existing desktop build is not automatically replaced.',xalign=0)
        self.notice.set_line_wrap(True);box.pack_start(self.notice,False,False,0)
        scroller=Gtk.ScrolledWindow();box.pack_start(scroller,True,True,0)
        self.list=Gtk.ListBox();self.list.set_selection_mode(Gtk.SelectionMode.NONE);scroller.add(self.list)
        buttons=Gtk.Box(spacing=8);box.pack_start(buttons,False,False,0)
        import_button=Gtk.Button(label='Import Tweak…');import_button.connect('clicked',self.import_tweak);buttons.pack_start(import_button,False,False,0)
        refresh=Gtk.Button(label='Refresh');refresh.connect('clicked',lambda b:self.reload());buttons.pack_start(refresh,False,False,0)
        self.host_watch=Gio.bus_watch_name(Gio.BusType.SESSION,'org.xfce.Tweaks',Gio.BusNameWatcherFlags.NONE,
            lambda *args:self.host_changed(True),lambda *args:self.host_changed(False))
        self.connect('destroy',lambda w:Gio.bus_unwatch_name(self.host_watch))
        self.reload();self.show_all()
        for root in self.roots:
            directory=Gio.File.new_for_path(str(root))
            try:monitor=directory.monitor_directory(Gio.FileMonitorFlags.WATCH_MOVES,None)
            except GLib.Error:continue
            monitor.connect('changed',self.catalogue_changed);self.catalogue_monitors.append(monitor)
        self.connect('destroy',self.stop_catalogue_watch)
        # Watch Xfconf events; no periodic polling at idle.
        self.monitor=Gio.Subprocess.new(['xfconf-query','-c','xfce4-desktop','--monitor'],Gio.SubprocessFlags.STDOUT_PIPE|Gio.SubprocessFlags.STDERR_SILENCE)
        self.stream=Gio.DataInputStream.new(self.monitor.get_stdout_pipe());self.read_event()
        self.connect('destroy',lambda w:self.monitor.force_exit())

    def host_changed(self,available):
        self.host_available=available
        self.notice.set_text('Connected to the modular desktop. Switches apply immediately.' if available else
            'The modular desktop is not running in this session. These switches cannot control the old desktop build.\nOpen Preview Modular Desktop to test, or activate the modular host for this session.')
        self.update_status()

    def read_event(self): self.stream.read_line_async(GLib.PRIORITY_DEFAULT,None,self.event,None)
    def event(self,stream,result,data):
        line,_=stream.read_line_finish(result)
        if line is not None:
            GLib.idle_add(self.catalogue_changed);self.read_event()

    def stop_catalogue_watch(self,*unused):
        self.closing=True
        for monitor in self.catalogue_monitors:monitor.cancel()
        if self.catalogue_source:GLib.source_remove(self.catalogue_source);self.catalogue_source=0

    def catalogue_changed(self,*unused):
        if self.closing:return GLib.SOURCE_REMOVE
        if not self.catalogue_source:self.catalogue_source=GLib.timeout_add(200,self.sync_catalogue)

    def catalogue_signature(self):
        return tuple(tuple(str(item.get(k,'')) for k in ['id','name','version','description','directory']) for item in discover(self.roots))

    def sync_catalogue(self):
        self.catalogue_source=0
        if self.catalogue_signature()!=self.catalogue:
            self.reload()
            if self.host_available:set_property('/xfce-tweaks/rescan',time.time_ns()//1000000,'uint64')
        else:self.update_status()
        return GLib.SOURCE_REMOVE

    def enabled(self,item):
        v=command('-p','/xfce-tweaks/enabled/'+item['id'])
        return item['defaultenabled'] if v is None else v=='true'

    def reload(self):
        self.catalogue=self.catalogue_signature()
        for row in self.list.get_children():self.list.remove(row)
        self.switches.clear();self.labels.clear()
        self.items.clear()
        for item in discover(self.roots):
            row=Gtk.Box(spacing=16);row.set_border_width(12)
            text=Gtk.Box(orientation=Gtk.Orientation.VERTICAL,spacing=4);row.pack_start(text,True,True,0)
            title=Gtk.Label(label=item.get('name',item['id'])+'  '+item.get('version',''),xalign=0);text.pack_start(title,False,False,0)
            description=Gtk.Label(label=item.get('description',''),xalign=0);description.set_line_wrap(True);text.pack_start(description,False,False,0)
            status=Gtk.Label(xalign=0);text.pack_start(status,False,False,0);self.labels[item['id']]=status
            switch=Gtk.Switch();switch.set_valign(Gtk.Align.CENTER);switch.set_active(self.enabled(item));switch.connect('notify::active',self.toggle,item['id'])
            self.switches[item['id']]=switch;self.defaults[item['id']]=item['defaultenabled'];self.items[item['id']]=item
            actions=Gtk.Box(spacing=8);actions.pack_start(switch,False,False,0)
            uninstall=Gtk.Button(label='Uninstall');uninstall.set_valign(Gtk.Align.CENTER)
            uninstall.set_sensitive(self.can_uninstall(item))
            if not uninstall.get_sensitive():uninstall.set_tooltip_text('This Tweak is provided by a read-only module source.')
            uninstall.connect('clicked',self.uninstall_tweak,item)
            actions.pack_start(uninstall,False,False,0);row.pack_end(actions,False,False,0);self.list.add(row)
        self.update_status();self.list.show_all()

    def update_status(self):
        for identifier,switch in self.switches.items():
            desired=command('-p','/xfce-tweaks/enabled/'+identifier)
            enabled=self.defaults[identifier] if desired is None else desired=='true'
            if switch.get_active()!=enabled:switch.set_active(enabled)
            status=command('-p','/xfce-tweaks/status/'+identifier)
            switch.set_sensitive(self.host_available)
            self.labels[identifier].set_text('Status: '+((status or 'Waiting for host status') if self.host_available else 'Unavailable — modular host is not running'))
        return GLib.SOURCE_REMOVE

    def toggle(self,switch,param,identifier):
        if not self.host_available:return
        try:set_property('/xfce-tweaks/enabled/'+identifier,switch.get_active())
        except RuntimeError as exc:self.error(str(exc))

    def error(self,message):
        dialog=Gtk.MessageDialog(transient_for=self,modal=True,message_type=Gtk.MessageType.ERROR,buttons=Gtk.ButtonsType.CLOSE,text=message)
        dialog.run();dialog.destroy()

    def can_uninstall(self,item):
        if os.geteuid()==0:return False
        directory=Path(item['directory']).resolve()
        return (directory.name==item['id'] and directory.parent in {Path(root).resolve() for root in self.managed_roots}
                and os.access(directory.parent,os.W_OK))

    def uninstall_tweak(self,button,item):
        dialog=Gtk.MessageDialog(transient_for=self,modal=True,message_type=Gtk.MessageType.WARNING,
            buttons=Gtk.ButtonsType.CANCEL,text='Uninstall '+item.get('name',item['id'])+'?')
        dialog.format_secondary_text('This removes the Tweak files from the manager and disables it first. Saved settings are kept. Restart the desktop before reinstalling this same Tweak ID.')
        dialog.add_button('_Uninstall',Gtk.ResponseType.YES)
        response=dialog.run();dialog.destroy()
        if response!=Gtk.ResponseType.YES:return
        was_enabled=self.switches[item['id']].get_active()
        try:
            set_property('/xfce-tweaks/enabled/'+item['id'],False)
            set_property('/xfce-tweaks/rescan',time.time_ns()//1000000,'uint64')
            remove_tweak(item,self.managed_roots)
            set_property('/xfce-tweaks/rescan',time.time_ns()//1000000,'uint64')
            self.reload()
        except Exception as exc:
            if was_enabled:
                try:set_property('/xfce-tweaks/enabled/'+item['id'],True)
                except Exception:pass
            self.error(str(exc))

    def import_tweak(self,button):
        chooser=Gtk.FileChooserDialog(title='Import a trusted Tweak',transient_for=self,action=Gtk.FileChooserAction.OPEN)
        chooser.add_buttons('_Cancel',Gtk.ResponseType.CANCEL,'_Import',Gtk.ResponseType.ACCEPT)
        label=Gtk.Label(label='Tweaks run native code inside xfdesktop. Import packages only from developers you trust.');label.set_line_wrap(True);chooser.set_extra_widget(label)
        filter=Gtk.FileFilter();filter.set_name('XFCE Tweak packages');filter.add_pattern('*.xfce-tweak');chooser.add_filter(filter)
        response=chooser.run();filename=chooser.get_filename();chooser.destroy()
        if response!=Gtk.ResponseType.ACCEPT:return
        try:
            item=import_package(filename,self.roots)
            # New imports start disabled, regardless of their manifest defaults.
            set_property('/xfce-tweaks/enabled/'+item['id'],False)
            set_property('/xfce-tweaks/rescan',time.time_ns()//1000000,'uint64')
            self.reload()
        except Exception as exc:self.error(str(exc))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--modules',action='append',default=[]);args=parser.parse_args()
    prefix=Path(__file__).resolve().parents[1]
    roots=[user_modules(),*args.modules,*os.environ.get('XFCE_TWEAKS_MODULE_PATH','').split(os.pathsep),prefix/'lib/xfce-tweaks/modules']
    home=Path.home().resolve()
    candidates=[user_modules(),*args.modules,prefix/'lib/xfce-tweaks/modules']
    managed_roots=[]
    for root in candidates:
        resolved=Path(root).resolve()
        if resolved==user_modules().resolve() or resolved.is_relative_to(home):managed_roots.append(resolved)
    Manager([str(x) for x in roots if str(x)],managed_roots);Gtk.main()
if __name__=='__main__':main()
