#!/usr/bin/env bash
set -euo pipefail
sudo dnf install xorg-x11-server-Xephyr xorg-x11-server-Xvfb xdotool xfwm4 \
  xorg-x11-utils python3-gobject ffmpeg-free
