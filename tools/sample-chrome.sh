#!/bin/sh
if [ ! "$1" ]; then
  echo "Usage: $0 DOMAIN"
  exit 1
fi

domain="$1"
sudo echo

tempdir=$(mktemp -d)
rm -f /tmp/keys
sudo rm -f /tmp/direct.pcapng

sudo tshark -Q -a duration:10 -w /tmp/direct.pcapng &
tsharkpid=$!
sleep 1

google-chrome --user-data-dir="$tempdir" --ssl-key-log-file=/tmp/keys --no-default-browser-check --no-first-run --disable-quic "https://$domain/" &
chromepid=$!

sleep 10
kill $chromepid
rm -rf "$tempir"

wait $tsharkpid
sudo chmod +r /tmp/direct.pcapng

./parse-pcap-stream.py /tmp/direct.pcapng "$domain"
