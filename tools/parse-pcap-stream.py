#!/usr/bin/env python3
import os
import sys
import subprocess
import yaml


class TlsStreamParser:
    STATE_CONTENT_TYPE = 0
    STATE_VERSION_BYTE0 = 1
    STATE_VERSION_BYTE1 = 2
    STATE_LENGTH_BYTE0 = 3
    STATE_LENGTH_BYTE1 = 4
    STATE_DATA = 5

    TLS_HEADER_SIZE = 5

    def __init__(self):
        self.state = self.STATE_CONTENT_TYPE
        self.current_length = None
        self.current_remaining = None

    def read(self, data):
        record_parts = []
        i = 0
        tls_consumed_bytes = 0
        while i < len(data):
            if self.state == self.STATE_CONTENT_TYPE:
                # TODO: add content type description
                content_type = data[i]
                self.state = self.STATE_VERSION_BYTE0
                i += 1
                tls_consumed_bytes += 1
            elif self.state == self.STATE_VERSION_BYTE0:
                self.state = self.STATE_VERSION_BYTE1
                i += 1
                tls_consumed_bytes += 1
            elif self.state == self.STATE_VERSION_BYTE1:
                self.state = self.STATE_LENGTH_BYTE0
                i += 1
                tls_consumed_bytes += 1
            elif self.state == self.STATE_LENGTH_BYTE0:
                self.current_length = data[i]
                self.state = self.STATE_LENGTH_BYTE1
                i += 1
                tls_consumed_bytes += 1
            elif self.state == self.STATE_LENGTH_BYTE1:
                self.current_length = self.current_length * 256 + data[i]
                self.current_remaining = self.current_length
                self.state = self.STATE_DATA
                i += 1
                tls_consumed_bytes += 1
            elif self.state == self.STATE_DATA:
                consume_data = min(self.current_remaining, len(data) - i)
                self.current_remaining -= consume_data
                i += consume_data
                tls_consumed_bytes += consume_data
                if self.current_remaining == 0:
                    record_parts.append(
                        (tls_consumed_bytes, self.TLS_HEADER_SIZE + self.current_length))
                    tls_consumed_bytes = 0
                    self.current_length = None
                    self.state = self.STATE_CONTENT_TYPE
        if tls_consumed_bytes:
            if self.current_length is None:
                record_parts.append((tls_consumed_bytes, '?'))
            else:
                record_parts.append(
                    (tls_consumed_bytes, self.TLS_HEADER_SIZE + self.current_length))
        return record_parts


if len(sys.argv) != 3:
    print(f'Usage: {sys.argv[0]} PCAP_FILE STREAM_ID')
    os.exit(1)

file = sys.argv[1]
stream_id = sys.argv[2]
result = subprocess.run(['tshark', '-2', '-r', file, '-q', '-z',
                        f'follow,tcp,yaml,{stream_id}'], capture_output=True, check=True, text=True)

follow_result = yaml.safe_load(result.stdout)
LOCAL_PEER = 0
REMOTE_PEER = 1
assert follow_result['peers'][REMOTE_PEER][
    'port'] == 443, f"assuming the remote peer is the TLS server: {follow_result['peers']}"
packets = follow_result['packets']

upload_stream = TlsStreamParser()
download_stream = TlsStreamParser()
rtt = packets[1]['timestamp'] - packets[0]['timestamp']
time_unit = rtt / 2
local_timestamp_first = packets[0]['timestamp']
mitm_timestamp_first = local_timestamp_first + rtt / 4
min_mitm_timestamp_up = packets[0]['timestamp']
min_mitm_timestamp_down = packets[0]['timestamp']
for packet in packets:
    local_timestamp = packet['timestamp']

    data = packet['data']
    if packet['peer'] == LOCAL_PEER:
        mitm_timestamp = local_timestamp + time_unit / 2
        mitm_timestamp = max(mitm_timestamp, min_mitm_timestamp_up)
        min_mitm_timestamp_up = mitm_timestamp

        timestamp = (mitm_timestamp - mitm_timestamp_first) / time_unit
        record_parts = upload_stream.read(data)
        print('%.3f' % timestamp, len(data), ','.join(
            f'{i}/{j}' for i, j in record_parts))
    elif packet['peer'] == REMOTE_PEER:
        mitm_timestamp = local_timestamp - time_unit / 2
        mitm_timestamp = max(mitm_timestamp, min_mitm_timestamp_down)
        min_mitm_timestamp_down = mitm_timestamp

        timestamp = (mitm_timestamp - mitm_timestamp_first) / time_unit
        record_parts = download_stream.read(data)
        print('%.3f' % timestamp, -len(data),
              ','.join(f'{i}/{j}' for i, j in record_parts))
