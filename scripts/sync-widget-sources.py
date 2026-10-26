#!/usr/bin/env python3
"""Export shared widget support into independently buildable Tweak sources."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
for identifier in ['clock-widget','weather-widget']:
    for source,target in [('widget.h','widget.h'),('content.h','content.h'),('widget-transfer.h','widget-transfer.h'),('settings.py','assets/app.py')]:
        path=ROOT/'modules'/identifier/target
        if path.is_symlink():path.unlink()
        path.write_bytes((ROOT/'modules/common'/source).read_bytes())

(ROOT/'modules/victus-control/widget-transfer.h').write_bytes((ROOT/'modules/common/widget-transfer.h').read_bytes())
