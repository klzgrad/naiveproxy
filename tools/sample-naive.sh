#!/bin/sh
if [ ! "$1" ]; then
  echo "Usage: $0 USERPASS DOMAIN"
  exit 1
fi

userpass="$1"
domain="$2"
sudo echo

rm -f /tmp/keys

sudo tshark -Q -a duration:10 -w /tmp/direct.pcapng &
tsharkpid=$!
sleep 1

../src/out/Release/naive --listen=socks://127.0.0.1:1081 --proxy=https://$userpass@$domain --ssl-key-log-file=/tmp/keys --log &
naivepid=$!

sleep 1
curl -s --proxy socks5h://127.0.0.1:1081 https://www.google.com/ -o/dev/null

sleep 3
kill -INT $naivepid
kill -TERM $naivepid

sleep 1
kill -9 $naivepid

wait $tsharkpid
sudo chmod +r /tmp/direct.pcapng

./parse-pcap-stream.py /tmp/direct.pcapng "$domain"
