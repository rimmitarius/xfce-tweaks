#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Activate the modular host in the existing supervised XFCE session, with rollback."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

def run(*args,check=True):
    return subprocess.run(args,text=True,capture_output=True,check=check)
def system(*args,check=True):return run('systemctl','--user',*args,check=check)
def atomic(path,text):
    path.parent.mkdir(parents=True,exist_ok=True)
    fd,name=tempfile.mkstemp(dir=path.parent,prefix='.xfce-tweaks-')
    with os.fdopen(fd,'w') as stream:
        stream.write(text);stream.flush();os.fsync(stream.fileno())
    os.replace(name,path)
def verify(prefix,unit):
    expected=str(prefix/'bin/xfdesktop')
    for attempt in range(15):
        state=system('show',unit,'-p','ActiveState','-p','MainPID','-p','ExecStart').stdout
        present=run('gdbus','call','--session','--dest','org.freedesktop.DBus','--object-path','/org/freedesktop/DBus',
                    '--method','org.freedesktop.DBus.NameHasOwner','org.xfce.Tweaks',check=False)
        if 'ActiveState=active' in state and expected in state and '(true,)' in present.stdout:
            pid=next(x for x in state.splitlines() if x.startswith('MainPID='))
            time.sleep(2)
            again=system('show',unit,'-p','ActiveState','-p','MainPID').stdout
            if 'MainPID=0'!=pid and pid in again and 'ActiveState=active' in again:return
        time.sleep(1)
    raise RuntimeError('Modular desktop failed startup verification')
def restore(backup,unit_override=None):
    record=json.loads((backup/'startup.json').read_text())
    unit=record.get('unit') or unit_override
    if not unit:raise RuntimeError('The backup has no service name; pass --unit NAME.service')
    system('stop',unit,check=False)
    for item in record['files']:
        target=Path(item['path']);target.unlink(missing_ok=True)
        if item['saved']:
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(backup/item['saved'],target)
    system('daemon-reload');system('start',unit)
    print('Restored previous desktop startup from',backup)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['activate','rollback'])
    parser.add_argument('--backup',type=Path)
    parser.add_argument('--unit',default=os.environ.get('XFCE_TWEAKS_SYSTEMD_UNIT'),help='dedicated systemd --user service unit to manage')
    args=parser.parse_args()
    if os.geteuid()==0:parser.error('Run as your desktop user without sudo')
    if os.environ.get('XDG_SESSION_TYPE')!='x11':parser.error('Requires your XFCE X11 session')
    config=Path(os.environ.get('XDG_CONFIG_HOME',Path.home()/'.config'))
    data=Path(os.environ.get('XDG_DATA_HOME',Path.home()/'.local/share'))
    state=Path(os.environ.get('XDG_STATE_HOME',Path.home()/'.local/state'))/'xfce-tweaks'
    marker=state/'live-installation.json'
    if args.action=='rollback':
        backup=args.backup or Path(json.loads(marker.read_text())['backup'])
        restore(backup,args.unit);return
    prefix=Path(os.environ.get('XFCE_TWEAKS_PREFIX',Path.home()/'.local/lib/xfce-tweaks-modular')).resolve()
    if not (prefix/'bin/xfdesktop').is_file():parser.error('Build the modular host first')
    if not args.unit or '/' in args.unit or not args.unit.endswith('.service'):
        parser.error('activation requires --unit NAME.service for a dedicated systemd --user service')
    current=system('show',args.unit,'-p','ExecStart','-p','ActiveState').stdout
    if str(prefix/'bin/xfdesktop') in current and 'ActiveState=active' in current:
        verify(prefix,args.unit);print('Modular host is already active');return
    if 'ActiveState=active' not in current:parser.error(f'Expected the existing running {args.unit}')
    backup=state/'live-backups'/str(time.time_ns());backup.mkdir(parents=True,mode=0o700)
    override=config/'systemd/user'/f'{args.unit}.d/xfce-tweaks.conf'
    settings=data/'applications/xfce-backdrop-settings.desktop'
    record={'unit':args.unit,'files':[],'previous':current}
    for index,path in enumerate([override,settings]):
        name=f'startup-{index}' if path.exists() else None
        if name:shutil.copy2(path,backup/name)
        record['files'].append({'path':str(path),'saved':name})
    atomic(backup/'startup.json',json.dumps(record,indent=2))
    for path in [config/'xfce4/desktop-groups.ini',config/'xfce4/xfconf/xfce-perchannel-xml/xfce4-desktop.xml',config/'systemd/user'/args.unit]:
        if path.exists():shutil.copy2(path,backup/path.name)
    icon_positions=config/'xfce4/desktop'
    if icon_positions.is_dir():shutil.copytree(icon_positions,backup/'desktop-icon-positions')
    try:
        quoted=str(prefix/'bin/xfdesktop').replace('\\','\\\\').replace('"','\\"').replace('%','%%')
        atomic(override,'# Managed by XFCE Modular Tweaks; see live-installation.json for rollback\n[Service]\nExecStart=\nExecStart="'+quoted+'" --sm-client-disable\n')
        if settings.exists():
            text=''.join(('Exec='+str(prefix/'bin/xfdesktop-settings')+'\n') if line.startswith('Exec=') else line for line in settings.read_text().splitlines(keepends=True))
            atomic(settings,text)
        system('daemon-reload');system('restart',args.unit);verify(prefix,args.unit)
        atomic(marker,json.dumps({'prefix':str(prefix),'backup':str(backup)},indent=2))
    except BaseException:
        restore(backup);raise
    print('Modular desktop is active. Backup:',backup)
if __name__=='__main__':
    try:main()
    except (RuntimeError,subprocess.CalledProcessError,OSError) as exc:
        print('Activation failed:',exc)
        if isinstance(exc,subprocess.CalledProcessError):print(exc.stderr)
        raise SystemExit(1)
