#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import os,subprocess,tempfile,time,sys
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xfce-tweaks-preview-test-') as temp:
    read_fd,write_fd=os.pipe()
    server=subprocess.Popen(['Xvfb','-displayfd',str(write_fd),'-screen','0','1400x900x24','-nolisten','tcp'],pass_fds=[write_fd])
    os.close(write_fd);display=os.read(read_fd,32).decode().strip();os.close(read_fd)
    env=dict(os.environ);env['DISPLAY']=':'+display;env['XDG_STATE_HOME']=temp
    prefix=Path(os.environ.get('XFCE_TWEAKS_PREFIX',Path.home()/'.local/lib/xfce-tweaks-modular'))
    preview=subprocess.Popen([sys.executable,str(root/'scripts/preview.py'),'--prefix',str(prefix)],env=env)
    try:
        deadline=time.monotonic()+15;window=None
        while time.monotonic()<deadline:
            if preview.poll() is not None:raise RuntimeError('Preview exited before opening')
            found=subprocess.run(['xdotool','search','--class','Xephyr'],env=env,text=True,capture_output=True)
            if found.returncode==0:window=found.stdout.splitlines()[0];break
            time.sleep(.2)
        assert window,'Xephyr preview window missing'
        ready=Path(temp)/'xfce-tweaks-preview/ready'
        deadline=time.monotonic()+15
        while not ready.exists() and time.monotonic()<deadline:
            assert preview.poll() is None
            time.sleep(.2)
        assert ready.exists(),'Manager and desktop did not become ready'
        assert preview.poll() is None
        subprocess.run(['xdotool','windowkill',window],env=env,check=True)
        preview.wait(timeout=15)
        assert (Path(temp)/'xfce-tweaks-preview/home/Desktop/Welcome.txt').exists()
        print('PASS: nested Xephyr preview starts with isolated configuration and closes cleanly')
    finally:
        if preview.poll() is None:preview.terminate();preview.wait(timeout=5)
        server.terminate();server.wait(timeout=5)
