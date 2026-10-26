#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
# Run as your normal user. Only distribution dependencies need root privileges.
sudo dnf install gcc gcc-c++ binutils meson ninja-build pkgconf-pkg-config gettext \
  glib2-devel gtk3-devel libxfce4util-devel libxfce4ui-devel xfconf-devel libwnck3-devel \
  libX11-devel libXext-devel libXrandr-devel libXinerama-devel libSM-devel \
  startup-notification-devel libnotify-devel garcon-devel exo-devel \
  Thunar-devel libyaml-devel gstreamer1-devel gstreamer1-plugins-base-devel \
  gstreamer1-plugins-base gstreamer1-plugins-good gstreamer1-plugins-good-gtk \
  python3 xfdesktop xfconf xfce4-session
