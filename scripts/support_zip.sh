#!/bin/sh
D=/usr/data/grumpyscreen
dmesg > /tmp/dmesg.txt 2>&1
ps > /tmp/ps.txt 2>&1
tar czf $D/support.tar.gz -C $D grumpyscreen.cfg VERSION release.info calibration.json -C /tmp dmesg.txt ps.txt
rm -f /tmp/dmesg.txt /tmp/ps.txt
echo "creato $D/support.tar.gz"
