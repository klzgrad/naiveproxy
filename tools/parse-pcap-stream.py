#!/usr/bin/env python3
import os
import sys
import subprocess
import json
import xml.etree.ElementTree as ET

if len(sys.argv) != 3:
    print(f'Usage: {sys.argv[0]} PCAP_FILE DOMAIN')
    os.exit(1)

file = sys.argv[1]
domain = sys.argv[2]
result = subprocess.run(['tshark', '-2', '-r', file, '-q', '-o','tls.keylog_file:/tmp/keys','-Y',
                        f'http2.header.value == "{domain}"', '-T', 'json'], capture_output=True, check=True, text=True)
json_result = json.loads(result.stdout)
target_tcp_stream = json_result[0]["_source"]["layers"]["tcp"]["tcp.stream"]
result = subprocess.run(['tshark', '-2', '-r', file, '-q', '-o','tls.keylog_file:/tmp/keys','-Y',
                        f'tcp.stream == {target_tcp_stream}', '-T', 'pdml'], capture_output=True, check=True, text=True)
pdml_result = ET.fromstring(result.stdout)

def children(e, cname):
    return [c for c in e if c.attrib['name'] == cname]

start_time = None

for packet in pdml_result:
    frame = children(packet, "frame")[0]
    frame_number = children(frame, "frame.number")[0].attrib['show']
    frame_time_relative = children(frame, "frame.time_relative")[0].attrib['show']
    tcp = children(packet, "tcp")[0]
    tcp_srcport = children(tcp, "tcp.srcport")[0].attrib['show']
    tcp_dstport = children(tcp, "tcp.dstport")[0].attrib['show']
    if tcp_dstport == "443":
        dir = '↑'
    else:
        dir = '↓'
    if start_time is None:
        start_time = float(frame_time_relative)
    frame_time_relative = float(frame_time_relative) - start_time
    http2s = children(packet, "http2")
    if len(http2s) == 0:
        continue
    http2s_desc = []
    for http2 in http2s:
        http2_stream = children(http2, "http2.stream")
        assert len(http2_stream) == 1
        http2_stream = http2_stream[0]
        http2_magic = children(http2_stream, "http2.magic")
        if http2_magic:
            http2s_desc.append('Magic')
            continue
        http2_type = children(http2_stream, "http2.type")[0].attrib['showname'].split(' ')[1]
        http2_length = children(http2_stream, "http2.length")[0].attrib['show']
        http2_streamid = children(http2_stream, "http2.streamid")[0].attrib['show']
        http2_stream_desc = f'{http2_length}:{http2_type}[{http2_streamid}]'
        if http2_type == 'HEADERS':
            http2_stream_desc += ": " + http2_stream.attrib['showname'].split(',')[-1].strip()
        http2s_desc.append(http2_stream_desc)
    if not http2s_desc:
        continue
    print(frame_number, f'{frame_time_relative:.4f}', dir, ', '.join(http2s_desc))
