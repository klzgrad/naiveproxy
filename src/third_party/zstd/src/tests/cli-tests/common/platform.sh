#!/bin/sh

UNAME=$(uname)

isWindows=false
INTOVOID="/dev/null"
case "$UNAME" in
  GNU) DEVDEVICE="/dev/random" ;;
  *) DEVDEVICE="/dev/zero" ;;
esac
case "$OS" in
  Windows*)
    isWindows=true
    INTOVOID="NUL"
    DEVDEVICE="NUL"
    ;;
esac

case "$UNAME" in
  Darwin) MD5SUM="md5 -r" ;;
  NetBSD) MD5SUM="md5 -n" ;;
  OpenBSD) MD5SUM="md5" ;;
  *) MD5SUM="md5sum" ;;
esac

md5hash() {
  $MD5SUM | dd bs=1 count=32 status=none
  echo
}

DIFF="diff"
case "$UNAME" in
  SunOS) DIFF="gdiff" ;;
esac

if echo hello | zstd -v -T2 2>&1 > $INTOVOID | grep -q 'multi-threading is disabled'
then
    hasMT=""
else
    hasMT="true"
fi

if zstd -vv --version | grep -q 'non-deterministic'; then
  NON_DETERMINISTIC="true"
else
  NON_DETERMINISTIC=""
fi
