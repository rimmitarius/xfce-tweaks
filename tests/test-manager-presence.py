#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A separate session with no host must not show usable switches or stale status."""
from pathlib import Path
import os,subprocess,sys,tempfile,shutil
ROOT=Path(__file__).resolve().parents[1]
if len(sys.argv)>1 and sys.argv[1]=='--inside':
    sys.path.insert(0,str(ROOT/'manager'))
    import app
    from gi.repository import Gtk,GLib
    app.set_property('/xfce-tweaks/status/desktop-groups','active','string')
    manager=app.Manager([sys.argv[2]])
    errors=[]
    def verify():
        try:
            assert manager.switches,'No modules to check'
            assert not manager.host_available
            assert all(not switch.get_sensitive() for switch in manager.switches.values())
            assert 'not running' in manager.notice.get_text()
            assert all('Unavailable' in label.get_text() for label in manager.labels.values())
            fixture=Path(sys.argv[2])/'new-hp';fixture.mkdir()
            (fixture/'tweak.ini').write_text('[Tweak]\nId=new-hp\nName=HP Control\nABI=1\nLibrary=libnew-hp.so\nDefaultEnabled=false\n')
            print('PASS: no-host manager blocks switches and ignores stale active status')
        except BaseException as exc:errors.append(str(exc));print('FAIL:',exc)
        GLib.timeout_add(700,verify_import);return False
    def verify_import():
        try:
            assert 'new-hp' in manager.switches,'New module did not appear automatically'
            assert not manager.switches['new-hp'].get_sensitive()
            print('PASS: manager discovers externally imported module without reopening')
        except BaseException as exc:errors.append(str(exc));print('FAIL:',exc)
        manager.destroy();return False
    GLib.timeout_add(600,verify);Gtk.main();sys.exit(bool(errors))
with tempfile.TemporaryDirectory(prefix='xfce-tweaks-presence-') as tmp:
    read_fd,write_fd=os.pipe()
    server=subprocess.Popen(['Xvfb','-displayfd',str(write_fd),'-screen','0','800x600x24','-nolisten','tcp'],pass_fds=[write_fd])
    os.close(write_fd);display=os.read(read_fd,32).decode().strip();os.close(read_fd)
    env=dict(os.environ);env['DISPLAY']=':'+display
    for key in ['DBUS_SESSION_BUS_ADDRESS','SESSION_MANAGER']:env.pop(key,None)
    prefix=Path(os.environ.get('XFCE_TWEAKS_PREFIX',Path.home()/'.local/lib/xfce-tweaks-modular'))
    for key,folder in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_RUNTIME_DIR','runtime')]:
        path=Path(tmp)/folder;path.mkdir(mode=0o700);env[key]=str(path)
    modules=Path(tmp)/'modules';modules.mkdir()
    for manifest in (prefix/'lib/xfce-tweaks/modules').glob('*/tweak.ini'):
        target=modules/manifest.parent.name;target.mkdir();shutil.copy(manifest,target/'tweak.ini')
    try:
        result=subprocess.run(['dbus-run-session','--',sys.executable,str(Path(__file__).resolve()),'--inside',str(modules)],env=env,timeout=15)
        if result.returncode:raise SystemExit(result.returncode)
    finally:server.terminate();server.wait(timeout=5)
