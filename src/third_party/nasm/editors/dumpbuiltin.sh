#!/bin/sh -
# Copyright 1996-20xx The NASM Authors - All Rights Reserved
# SPDX-License-Identifier: BSD-2-Clause

#
# Run the nasm binary and extract the list of macros that may or may
# not be defined in .mac files.
#

tmp="$(mktemp -d)"
[ -n "$tmp" ] || exit 1

if [ -n "$1" ]; then
    NASM="$1"
fi

: > "$tmp/junk.asm"
"$NASM" -f bin -o "$tmp/junk.bin" -Lsb -l "$tmp/junk.lst" "$tmp/junk.asm"
printf ';;; Automatically generated list of builtin macros\n'
sed -n -E -e 's/^[^;]*;;; *(%i?(define|defalias|macro)) ([^ ]*) .*$/\1 \3/p'\
    < "$tmp/junk.lst"
rm -rf "$tmp"
