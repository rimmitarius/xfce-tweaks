#!/usr/bin/env bash
set -euo pipefail
sudo modprobe -r xfce_hp_rgb
sudo rm -f /etc/modules-load.d/xfce-hp-rgb.conf
sudo find /lib/modules -path '*/extra/xfce_hp_rgb.ko' -delete
sudo depmod -a
