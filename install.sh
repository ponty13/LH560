#!/bin/sh
# Build and install the LH-560 TSPL filter + PPD, optionally creating a queue.
# Usage: sudo ./install.sh [device-uri]     (list URIs with:  lpinfo -v)
set -e
cd "$(dirname "$0")"

# Build deps:  Debian/Ubuntu: libcups2-dev   Fedora/RHEL: cups-devel   Arch: libcups
cc -O2 -Wall -Wno-deprecated-declarations -o rastertolh560 rastertolh560.c \
   $(cups-config --cflags --libs)

SERVERBIN=$(cups-config --serverbin)
install -m 755 rastertolh560 "$SERVERBIN/filter/rastertolh560"
install -d /usr/share/cups/model
install -m 644 LH560.ppd /usr/share/cups/model/LH560.ppd
echo "Installed filter to $SERVERBIN/filter and PPD to /usr/share/cups/model"

if [ -n "$1" ]; then
  lpadmin -p LH560 -E -v "$1" -P LH560.ppd -o printer-is-shared=false
  echo "Queue 'LH560' created on $1"
else
  echo "No URI given. Find yours with 'lpinfo -v', then:"
  echo "  sudo lpadmin -p LH560 -E -v <uri> -P LH560.ppd"
fi
