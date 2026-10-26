#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual manager widgets against the private running desktop."""
from pathlib import Path
import os,sys,time,traceback,json
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'manager'))
import app
from gi.repository import Gtk,GLib,Gdk
manager=app.Manager([str(app.user_modules()),*os.environ['XFCE_TWEAKS_MODULE_PATH'].split(os.pathsep)])
checks=[];failed=[];steps=[]

def expect(condition,message):
    if not condition: raise AssertionError(message)
    checks.append(message);print('PASS:',message,flush=True)

def status(identifier):return app.command('-p','/xfce-tweaks/status/'+identifier)
def screenshot():
    root=Gdk.get_default_root_window()
    Gdk.pixbuf_get_from_window(root,0,0,root.get_width(),root.get_height()).savev(str(Path(os.environ['DG_TEST_RESULTS'])/'manager.png'),'png',[],[])

def disable_groups():
    expect(status('desktop-groups')=='active','manager discovers active Desktop Groups')
    screenshot();manager.switches['desktop-groups'].set_active(False)
def groups_off():
    expect(status('desktop-groups')=='disabled','GUI switch disables Desktop Groups without restarting xfdesktop')
    manager.switches['desktop-groups'].set_active(True)
def groups_on():
    expect(status('desktop-groups')=='active','GUI switch re-enables Desktop Groups')
    manager.switches['video-wallpaper'].set_active(False)
def video_off():
    expect(status('video-wallpaper')=='disabled','GUI switch disables Video Wallpaper')
    manager.switches['video-wallpaper'].set_active(True)
def video_on():
    expect(status('video-wallpaper')=='active','GUI switch re-enables Video Wallpaper')
    def choose_file():
        for w in Gtk.Window.list_toplevels():
            if isinstance(w,Gtk.FileChooserDialog):
                w.set_filename(str(ROOT/'tests/example-tweak/example-tweak-0.1.0.xfce-tweak'))
                GLib.timeout_add(250,lambda:(w.response(Gtk.ResponseType.ACCEPT),False)[1]);return False
        return True
    GLib.timeout_add(100,choose_file)
    manager.import_tweak(None)
def imported():
    expect('example-tweak' in manager.switches,'GUI imports a packaged SDK Tweak')
    expect(not manager.switches['example-tweak'].get_active(),'imported Tweak starts disabled')
    manager.switches['example-tweak'].set_active(True)
def example_on():
    expect(status('example-tweak')=='active','newly imported Tweak loads without rebuilding or restarting xfdesktop')
    manager.switches['example-tweak'].set_active(False)
def example_off():
    expect(status('example-tweak')=='disabled','imported Tweak can be disabled through the GUI')
steps=[disable_groups,groups_off,groups_on,video_off,video_on,imported,example_on,example_off]

def step():
    try:
        if not steps:
            manager.destroy();return False
        steps.pop(0)();return True
    except BaseException as exc:
        traceback.print_exc();failed.append(str(exc));manager.destroy();return False
GLib.timeout_add(600,step)
Gtk.main()
(Path(os.environ['DG_TEST_RESULTS'])/'gui-results.json').write_text(json.dumps({'checks':checks,'errors':failed},indent=2))
sys.exit(bool(failed))
