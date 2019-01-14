#!/bin/sh
if [ ! "$1" ]; then
  echo "Usage: $0 PCAP_FILE"
  exit 1
fi

file="$1"
# Remember to disable segmentation offload so pcap files won't capture packets larger than MTU:
# sudo ethtool --offload eth0 gso off gro off
tshark -2 -r "$file" -R tls.handshake.extensions_server_name -T fields -e tls.handshake.extensions_server_name -e tcp.stream
