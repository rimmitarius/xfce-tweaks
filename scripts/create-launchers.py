#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def desktop_quote(text):
    return '"'+str(text).replace('\\','\\\\').replace('"','\\"').replace('$','\\$').replace('`','\\`').replace('%','%%')+'"'
for title,program in [('XFCE Tweaks Manager','scripts/manager.sh'),('Preview Modular Desktop','scripts/preview.py')]:
    file=root/(title+'.desktop')
    file.write_text('[Desktop Entry]\nType=Application\nName='+title+'\nExec='+desktop_quote(root/program)+'\nTerminal=false\nIcon=preferences-desktop\nCategories=Settings;DesktopSettings;\n')
    file.chmod(0o755)
