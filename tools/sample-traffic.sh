#!/bin/sh
if [ ! "$1" ]; then
  echo "Usage: $0 IFACE"
  exit 1
fi

iface="$1"
sudo echo

sudo tcpdump -i "$1" -s0 -w microsoft-direct.pcap &
chromium --user-data-dir=$(mktemp -d) https://www.microsoft.com/en-us/
sudo pkill tcpdump

sudo tcpdump -i "$1" -s0 -w microsoft-proxy.pcap &
chromium --proxy-server=socks5://127.0.0.1:1080 --user-data-dir=$(mktemp -d) https://www.microsoft.com/en-us/
sudo pkill tcpdump &
