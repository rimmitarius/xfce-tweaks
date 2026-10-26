#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the modular desktop in a separate Xephyr window and private D-Bus session."""
from pathlib import Path
import argparse,os,shutil,signal,subprocess,sys,time,fcntl
ROOT=Path(__file__).resolve().parents[1]

def inside(prefix):
    lock=(prefix/'.build-lock').open('a');fcntl.flock(lock,fcntl.LOCK_SH|fcntl.LOCK_NB)
    children=[]
    try:
        # D-Bus activation must inherit the sandbox display and configuration.
        subprocess.run(['dbus-update-activation-environment','DISPLAY','HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'],check=True)
        children.append(subprocess.Popen(['xfwm4','--compositor=off','--sm-client-disable']))
        desktop=subprocess.Popen([str(prefix/'bin/xfdesktop'),'--disable-wm-check','--sm-client-disable']);children.append(desktop)
        manager=subprocess.Popen([str(prefix/'bin/xfce-tweaks-manager')]);children.append(manager)
        ready=False
        for attempt in range(50):
            if manager.poll() is not None:raise RuntimeError('XFCE Tweaks manager failed to start')
            found=subprocess.run(['xdotool','search','--onlyvisible','--name','^XFCE Tweaks$'],capture_output=True)
            if found.returncode==0:ready=True;break
            time.sleep(.2)
        if not ready:raise RuntimeError('Preview manager window did not appear')
        (Path(os.environ['XDG_CONFIG_HOME']).parent/'ready').write_text(str(desktop.pid))
        return desktop.wait()
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try:child.wait(timeout=5)
                except subprocess.TimeoutExpired:child.kill();child.wait()

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--inside',action='store_true');parser.add_argument('--prefix',type=Path,default=Path(os.environ.get('XFCE_TWEAKS_PREFIX',Path.home()/'.local/lib/xfce-tweaks-modular')));args=parser.parse_args()
    if args.inside:return inside(args.prefix)
    if not shutil.which('Xephyr'):raise SystemExit('Install preview tools: ./scripts/install-test-dependencies.sh')
    if not (args.prefix/'bin/xfdesktop').is_file():raise SystemExit('Build first: ./scripts/build.sh')
    state=Path(os.environ.get('XDG_STATE_HOME',Path.home()/'.local/state'))/'xfce-tweaks-preview'
    (state/'ready').unlink(missing_ok=True)
    home=state/'home';desktop=home/'Desktop';desktop.mkdir(parents=True,exist_ok=True)
    for name,text in [('Welcome.txt','This is the isolated XFCE Tweaks preview.\nRight-click to create a Desktop Group or choose a video wallpaper.\n'),('Another file.txt','Group these sample files to try Desktop Groups.\n')]:
        if not (desktop/name).exists():(desktop/name).write_text(text)
    env=dict(os.environ)
    for name in ['DBUS_SESSION_BUS_ADDRESS','SESSION_MANAGER','WAYLAND_DISPLAY','XFCE_TWEAKS_MODULE_PATH']:env.pop(name,None)
    env['HOME']=str(home);env['XDG_SESSION_TYPE']='x11';env['XDG_CURRENT_DESKTOP']='XFCE';env['NO_AT_BRIDGE']='1'
    for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_RUNTIME_DIR','runtime'),('XDG_STATE_HOME','state')]:
        path=state/folder;path.mkdir(parents=True,exist_ok=True);env[key]=str(path)
    Path(env['XDG_RUNTIME_DIR']).chmod(0o700)
    (Path(env['XDG_CONFIG_HOME'])/'user-dirs.dirs').write_text('XDG_DESKTOP_DIR="$HOME/Desktop"\n')
    read_fd,write_fd=os.pipe()
    server=subprocess.Popen(['Xephyr','-displayfd',str(write_fd),'-screen','1280x800','-resizeable','-nolisten','tcp','-noreset','-ac'],pass_fds=[write_fd])
    os.close(write_fd)
    try:
        display=os.read(read_fd,32).decode().strip()
        if not display.isdigit():raise RuntimeError('Xephyr did not start')
        env['DISPLAY']=':'+display
        print('Preview settings:',state,flush=True)
        return subprocess.call(['dbus-run-session','--',sys.executable,str(Path(__file__).resolve()),'--inside','--prefix',str(args.prefix)],env=env)
    finally:
        os.close(read_fd)
        if server.poll() is None:server.terminate();server.wait(timeout=5)
if __name__=='__main__':
    try:sys.exit(main())
    except KeyboardInterrupt:sys.exit(130)
