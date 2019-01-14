#!/bin/sh
if [ ! "$1" ]; then
  echo "Usage: $0 IFACE"
  exit 1
fi

iface="$1"
sudo echo

sudo tcpdump -i "$1" -s0 -w microsoft-direct.pcap &
sleep 1
curl https://www.example.com/

sleep 1
sudo pkill tcpdump

sudo tcpdump -i "$1" -s0 -w microsoft-proxy.pcap &
sleep 1
curl --proxy socks5h://127.0.0.1:1080 https://www.example.com/

sleep 1
sudo pkill tcpdump &
