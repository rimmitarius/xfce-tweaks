#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Create, build and package a Tweak independently of the xfdesktop host."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'manager'))
from registry import metadata,validate_elf

def main():
    parser=argparse.ArgumentParser(description=__doc__);commands=parser.add_subparsers(dest='action',required=True)
    new=commands.add_parser('new');new.add_argument('id');new.add_argument('directory',type=Path)
    build=commands.add_parser('build');build.add_argument('directory',type=Path)
    package=commands.add_parser('package');package.add_argument('directory',type=Path);package.add_argument('--library',type=Path);package.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.action=='new':
        if not re.fullmatch(r'[a-z][a-z0-9-]{0,63}',args.id):parser.error('Use a lowercase ID with letters, numbers and hyphens')
        shutil.copytree(ROOT/'examples/hello-desktop',args.directory,ignore=shutil.ignore_patterns('build','*.xfce-tweak'))
        for file in args.directory.iterdir():
            if file.is_file():file.write_text(file.read_text().replace('hello-desktop',args.id).replace('Hello Desktop',args.id.replace('-',' ').title()))
        print('Created:',args.directory)
    elif args.action=='build':
        source=args.directory.resolve();metadata(source/'tweak.ini')
        prefix=Path(os.environ.get('XFCE_TWEAKS_PREFIX',Path.home()/'.local/lib/xfce-tweaks-modular'))
        env=dict(os.environ);env['PKG_CONFIG_PATH']=str(prefix/'lib/pkgconfig')+os.pathsep+env.get('PKG_CONFIG_PATH','')
        command=['meson','setup',str(source/'build'),str(source)]
        if (source/'build/build.ninja').exists():command+=['--reconfigure']
        subprocess.run(command,env=env,check=True);subprocess.run(['meson','compile','-C',str(source/'build')],env=env,check=True)
    else:
        source=args.directory.resolve();item=metadata(source/'tweak.ini')
        library=(args.library or source/'build'/item['library']).resolve();validate_elf(library)
        output=args.output or source/(item['id']+'-'+item.get('version','0')+'.xfce-tweak')
        strip=shutil.which('strip')
        if strip is None:raise SystemExit('The binutils strip tool is required to remove local build paths from package binaries')
        readelf=shutil.which('readelf')
        if readelf is None:raise SystemExit('The binutils readelf tool is required to check package binaries for local RPATHs')
        output.parent.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='xfce-tweak-package-') as temporary:
            packaged_library=Path(temporary)/item['library'];shutil.copyfile(library,packaged_library)
            subprocess.run([strip,'--strip-debug',str(packaged_library)],check=True)
            dynamic=subprocess.run([readelf,'-d',str(packaged_library)],check=True,capture_output=True,text=True).stdout
            if any('(RPATH)' in line or '(RUNPATH)' in line for line in dynamic.splitlines()):
                raise SystemExit('The module binary contains an RPATH/RUNPATH; rebuild it without local library paths before packaging')
            with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as archive:
                archive.write(source/'tweak.ini','tweak.ini');archive.write(packaged_library,item['library'])
                for file in source.iterdir():
                    if file.is_file() and (file.suffix in ['.c','.h','.md'] or file.name in ['meson.build','LICENSE','COPYING']):archive.write(file,'source/'+file.name)
                assets=source/'assets'
                if assets.is_dir():
                    for file in sorted(assets.rglob('*')):
                        if file.is_file() and '__pycache__' not in file.parts:
                            archive.write(file,'assets/'+str(file.relative_to(assets)))
                driver=source/'driver'
                if driver.is_dir():
                    for file in sorted(driver.iterdir()):
                        if file.is_file() and (file.suffix in ['.c','.sh'] or file.name=='Makefile'):
                            archive.write(file,'source/driver/'+file.name)
        print('Package:',output)
if __name__=='__main__':main()
