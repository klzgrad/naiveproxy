#!/bin/bash

# Copyright (c) 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#
# Render a trace using gnuplot into a png file.
#

if [ -z "$1" ]; then
  echo "Usage: time_sequence_gnuplot.sh path/to/trace output.png"
  exit 1
fi

if [ ! -f "$1" ]; then
  echo "File $1 does not exist or is not a file"
  exit 1
fi

if [ -z "$2" ]; then
  echo "Output file not specified"
  exit 1
fi

if [ ! -x "$QUIC_TRACE_CONV_BIN" ]; then
  SCRIPT_SOURCE_DIR="$(dirname "${BASH_SOURCE[0]}")"
  QUIC_TRACE_CONV_BIN="$SCRIPT_SOURCE_DIR/../../../../../out/Default/quic_trace_to_time_sequence_gnuplot"
fi

if [ ! -x "$QUIC_TRACE_CONV_BIN" ]; then
  echo "Cannot find conversion tool binary"
  exit 1
fi

TMPDIR="$(mktemp -d)"
for event_type in send ack loss; do
  "$QUIC_TRACE_CONV_BIN" --sequence=$event_type < "$1" > "$TMPDIR/$event_type.txt"
done

if [ -z "$GNUPLOT_SIZE" ]; then
  GNUPLOT_SIZE="7680,4320"
fi
if [ -z "$GNUPLOT_TERMINAL" ]; then
  GNUPLOT_TERMINAL="png size $GNUPLOT_SIZE"
fi


gnuplot <<EOF
set terminal $GNUPLOT_TERMINAL
set output "$2"
plot \
   "$TMPDIR/send.txt" with lines lt rgb "blue" title "Sent", \
   "$TMPDIR/ack.txt" with lines lt rgb "green" title "Ack", \
   "$TMPDIR/loss.txt" with lines lt rgb "red" title "Loss"
EOF

rm -rf "$TMPDIR"
