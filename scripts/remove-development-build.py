#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Remove only the isolated modular build, preserving the project and live desktop."""
from pathlib import Path
import fcntl,os,shutil,subprocess,sys
prefix=Path(os.environ.get('XFCE_TWEAKS_PREFIX',Path.home()/'.local/lib/xfce-tweaks-modular')).resolve()
if not (prefix/'.xfce-tweaks-development-prefix').is_file():raise SystemExit('Not a marked development prefix; refusing to remove it')
if prefix in [Path('/'),Path.home(),Path('/usr'),Path('/usr/local')]:raise SystemExit('Unsafe prefix')
with (prefix/'.build-lock').open('a') as lock:
    try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except BlockingIOError:raise SystemExit('Close the modular preview first')
    services=subprocess.run(['systemctl','--user','list-units','--type=service','--state=running','--no-legend','--plain'],text=True,capture_output=True)
    if services.returncode==0:
        for line in services.stdout.splitlines():
            fields=line.split()
            if not fields:continue
            active=subprocess.run(['systemctl','--user','show',fields[0],'-p','ExecStart'],text=True,capture_output=True)
            if str(prefix/'bin/xfdesktop') in active.stdout:
                raise SystemExit(f'This build is running as {fields[0]}; stop or restore that service before removing it')
    shutil.rmtree(prefix)
print('Removed development build. Project sources, imported Tweaks, preview data and existing desktop installation retained.')
