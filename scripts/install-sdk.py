#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import os,shutil,sys
root=Path(__file__).resolve().parents[1]
prefix=Path(sys.argv[1] if len(sys.argv)>1 else os.environ.get('XFCE_TWEAKS_PREFIX',Path.home()/'.local/lib/xfce-tweaks-modular'))
include=prefix/'include/xfce-tweaks-1';include.mkdir(parents=True,exist_ok=True)
shutil.copy2(root/'sdk/xfce-tweaks.h',include/'xfce-tweaks.h')
pc=prefix/'lib/pkgconfig';pc.mkdir(parents=True,exist_ok=True)
(pc/'xfce-tweaks-1.pc').write_text('prefix='+str(prefix)+'\nincludedir=${prefix}/include\nName: XFCE Tweaks SDK\nDescription: Native xfdesktop extension ABI 1\nVersion: 0.1.0\nRequires: gtk+-3.0 gmodule-2.0 libxfconf-0\nCflags: -I${includedir}/xfce-tweaks-1\nLibs:\n')
print('SDK installed:',prefix)
