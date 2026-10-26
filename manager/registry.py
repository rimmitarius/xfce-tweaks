# SPDX-License-Identifier: GPL-2.0-or-later
"""Tweak discovery and bounded, path-safe import. This file does not execute modules."""
import configparser
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import tempfile
import zipfile

ABI = 1
MAX_PACKAGE = 32 * 1024 * 1024

def metadata(path):
    import re
    config = configparser.ConfigParser(interpolation=None)
    config.read(path, encoding='utf-8')
    item = dict(config['Tweak'])
    identifier = item.get('id','')
    library = item.get('library','')
    if not re.fullmatch(r'[a-z][a-z0-9-]{0,63}', identifier):
        raise ValueError('Invalid Tweak ID')
    if int(item.get('abi',0)) != ABI: raise ValueError('Unsupported Tweak ABI')
    if not library.endswith('.so') or Path(library).name != library or '\\' in library:
        raise ValueError('Library must be a .so filename')
    item['defaultenabled'] = item.get('defaultenabled','false').lower() == 'true'
    item['directory'] = str(Path(path).parent)
    return item

def user_modules():
    return Path(os.environ.get('XDG_DATA_HOME', Path.home()/'.local/share'))/'xfce-tweaks/modules'

def discover(roots):
    found = {}
    for root in roots:
        for manifest in sorted(Path(root).glob('*/tweak.ini')):
            if manifest.parent.name.startswith('.'): continue
            try:
                item = metadata(manifest)
                found.setdefault(item['id'], item)
            except (ValueError, KeyError, configparser.Error): continue
    return list(found.values())

def validate_elf(path):
    header = path.read_bytes()[:20]
    if len(header)<20 or header[:4]!=b'\x7fELF' or header[4]!=2 or header[5]!=1:
        raise ValueError('A 64-bit little-endian Linux shared library is required')
    machine = int.from_bytes(header[18:20], 'little')
    expected = {'x86_64':62,'aarch64':183}.get(platform.machine())
    if machine != expected: raise ValueError('Tweak was built for a different processor')
    if int.from_bytes(header[16:18],'little') != 3: raise ValueError('ELF must be a shared library')

def import_package(archive, roots):
    destination = user_modules()
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.import-', dir=destination) as tmp:
        stage = Path(tmp)
        with zipfile.ZipFile(archive) as package:
            if len(package.infolist()) > 128 or sum(x.file_size for x in package.infolist()) > MAX_PACKAGE:
                raise ValueError('Tweak package is too large')
            seen = set()
            for member in package.infolist():
                name = PurePosixPath(member.filename)
                mode = member.external_attr >> 16
                if name.is_absolute() or '..' in name.parts or '\\' in member.filename or member.filename in seen:
                    raise ValueError('Unsafe archive path')
                if (mode & 0o170000) not in (0, 0o100000, 0o040000): raise ValueError('Links and special files are not allowed')
                seen.add(member.filename)
                target = stage.joinpath(*name.parts)
                if member.is_dir(): target.mkdir(parents=True,exist_ok=True)
                else:
                    target.parent.mkdir(parents=True,exist_ok=True)
                    with package.open(member) as source, target.open('xb') as output: shutil.copyfileobj(source,output)
        item = metadata(stage/'tweak.ini')
        if any(x['id']==item['id'] for x in discover(roots)):
            raise ValueError('That Tweak ID is already installed; replacing loaded code requires restarting the test desktop')
        validate_elf(stage/item['library'])
        final = destination/item['id']
        if final.exists(): raise ValueError('Destination already exists')
        stage.rename(final)
        # TemporaryDirectory tolerates its original name disappearing.
    return metadata(final/'tweak.ini')

def remove_tweak(item, managed_roots):
    """Remove an installed module directory, but only from explicitly managed roots."""
    directory = Path(item.get('directory', '')).resolve()
    identifier = item.get('id', '')
    if not identifier or directory.name != identifier:
        raise ValueError('Tweak directory does not match its ID')
    allowed = {Path(root).resolve() for root in managed_roots}
    if directory.parent not in allowed:
        raise ValueError('This Tweak is from a read-only module source and cannot be uninstalled here')
    manifest = directory/'tweak.ini'
    current = metadata(manifest)
    if current['id'] != identifier:
        raise ValueError('Tweak identity changed; refusing to remove it')
    shutil.rmtree(directory)
    return identifier
