#!/bin/sh
for i in /tmp/trace.*; do
  cut -d' ' -f1 $i | LC_ALL=C sort -u | sed 's/\/$//' | LC_ALL=C sort -u >$i.sorted
done
cat /tmp/trace.*.sorted | LC_ALL=C sort -u >/tmp/detected-files
