--
-- Copyright 2021 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

DROP VIEW IF EXISTS rx_packets;
CREATE PERFETTO VIEW rx_packets AS
SELECT
  ts,
  REPLACE(name, " Received KB", "") AS dev,
  EXTRACT_ARG(arg_set_id, 'cpu') AS cpu,
  EXTRACT_ARG(arg_set_id, 'len') AS len
FROM counter c
LEFT JOIN counter_track t
  ON c.track_id = t.id
WHERE name GLOB "* Received KB"
ORDER BY ts DESC;

DROP VIEW IF EXISTS gro_rx_packet_count;
CREATE PERFETTO VIEW gro_rx_packet_count AS
SELECT
  s.name AS dev,
  COUNT(1) AS cnt
FROM slice s
LEFT JOIN track t
  ON s.track_id = t.id
WHERE t.name GLOB "Napi Gro Cpu *"
GROUP BY s.name;

DROP VIEW IF EXISTS tx_packets;
CREATE PERFETTO VIEW tx_packets AS
SELECT
  ts,
  REPLACE(name, " Transmitted KB", "") AS dev,
  EXTRACT_ARG(arg_set_id, 'cpu') AS cpu,
  EXTRACT_ARG(arg_set_id, 'len') AS len
FROM counter c
LEFT JOIN counter_track t
  ON c.track_id = t.id
WHERE name GLOB "* Transmitted KB"
ORDER BY ts DESC;

DROP VIEW IF EXISTS net_devices;
CREATE PERFETTO VIEW net_devices AS
SELECT DISTINCT dev
FROM tx_packets
UNION
SELECT DISTINCT dev
FROM rx_packets;

DROP VIEW IF EXISTS tcp_retransmitted_count;
CREATE PERFETTO VIEW tcp_retransmitted_count AS
SELECT
  COUNT(1) AS cnt
FROM slice s
LEFT JOIN track t
  ON s.track_id = t.id
WHERE
  t.name = "TCP Retransmit Skb";

DROP VIEW IF EXISTS kfree_skb_count;
CREATE PERFETTO VIEW kfree_skb_count AS
SELECT
  MAX(value) AS cnt
FROM counter c
LEFT JOIN track t
  ON c.track_id = t.id
WHERE
  t.name = "Kfree Skb IP Prot";

DROP VIEW IF EXISTS device_per_core_ingress_traffic;
CREATE PERFETTO VIEW device_per_core_ingress_traffic AS
SELECT
  dev,
  AndroidNetworkMetric_CorePacketStatistic(
    'id', cpu,
    'packet_statistic', AndroidNetworkMetric_PacketStatistic(
      'packets', COUNT(1),
      'bytes', SUM(len),
      'first_packet_timestamp_ns', MIN(ts),
      'last_packet_timestamp_ns', MAX(ts),
      'interval_ns', IIF((MAX(ts) - MIN(ts)) > 10000000, MAX(ts) - MIN(ts), 10000000),
      'data_rate_kbps', (SUM(len) * 8) / (IIF((MAX(ts) - MIN(ts)) > 10000000, MAX(ts) - MIN(ts), 10000000) / 1e9) / 1024
    )
  ) AS proto
FROM rx_packets
GROUP BY dev, cpu;

DROP VIEW IF EXISTS device_per_core_egress_traffic;
CREATE PERFETTO VIEW device_per_core_egress_traffic AS
SELECT
  dev,
  AndroidNetworkMetric_CorePacketStatistic(
    'id', cpu,
    'packet_statistic', AndroidNetworkMetric_PacketStatistic(
      'packets', COUNT(1),
      'bytes', SUM(len),
      'first_packet_timestamp_ns', MIN(ts),
      'last_packet_timestamp_ns', MAX(ts),
      'interval_ns', IIF((MAX(ts) - MIN(ts)) > 10000000, MAX(ts) - MIN(ts), 10000000),
      'data_rate_kbps', (SUM(len) * 8) / (IIF((MAX(ts) - MIN(ts)) > 10000000, MAX(ts) - MIN(ts), 10000000) / 1e9) / 1024
    )
  ) AS proto
FROM tx_packets
GROUP BY dev, cpu;

DROP VIEW IF EXISTS device_total_ingress_traffic;
CREATE PERFETTO VIEW device_total_ingress_traffic AS
SELECT
  dev,
  MIN(ts) AS start_ts,
  MAX(ts) AS end_ts,
  IIF((MAX(ts) - MIN(ts)) > 10000000, MAX(ts) - MIN(ts), 10000000) AS interval,
  COUNT(1) AS packets,
  SUM(len) AS bytes
FROM rx_packets
GROUP BY dev;

DROP VIEW IF EXISTS device_total_egress_traffic;
CREATE PERFETTO VIEW device_total_egress_traffic AS
SELECT
  dev,
  MIN(ts) AS start_ts,
  MAX(ts) AS end_ts,
  IIF((MAX(ts) - MIN(ts)) > 10000000, MAX(ts) - MIN(ts), 10000000) AS interval,
  COUNT(1) AS packets,
  SUM(len) AS bytes
FROM tx_packets
GROUP BY dev;

DROP VIEW IF EXISTS device_traffic_statistic;
CREATE PERFETTO VIEW device_traffic_statistic AS
SELECT
  AndroidNetworkMetric_NetDevice(
    'name', net_devices.dev,
    'rx', (
      SELECT
        AndroidNetworkMetric_Rx(
          'total', AndroidNetworkMetric_PacketStatistic(
            'packets', packets,
            'bytes', bytes,
            'first_packet_timestamp_ns', start_ts,
            'last_packet_timestamp_ns', end_ts,
            'interval_ns', interval,
            'data_rate_kbps', (bytes * 8) / (interval / 1e9) / 1024
          ),
          'core', (
            SELECT
              RepeatedField(proto)
            FROM device_per_core_ingress_traffic
            WHERE device_per_core_ingress_traffic.dev = device_total_ingress_traffic.dev
          ),
          'gro_aggregation_ratio', (
            SELECT
              CASE
                WHEN packets > 0 THEN '1:' || CAST((cnt * 1.0 / packets) AS text)
                ELSE '0:' || cnt
              END
            FROM gro_rx_packet_count
            WHERE gro_rx_packet_count.dev = net_devices.dev
          )
        )
      FROM device_total_ingress_traffic
      WHERE device_total_ingress_traffic.dev = net_devices.dev
    ),
    'tx', (
      SELECT
        AndroidNetworkMetric_Tx(
          'total', AndroidNetworkMetric_PacketStatistic(
            'packets', packets,
            'bytes', bytes,
            'first_packet_timestamp_ns', start_ts,
            'last_packet_timestamp_ns', end_ts,
            'interval_ns', interval,
            'data_rate_kbps', (bytes * 8) / (interval / 1e9) / 1024
          ),
          'core', (
            SELECT
              RepeatedField(proto)
            FROM device_per_core_egress_traffic
            WHERE device_per_core_egress_traffic.dev = device_total_egress_traffic.dev
          )
        )
      FROM device_total_egress_traffic
      WHERE device_total_egress_traffic.dev = net_devices.dev
    )
  ) AS proto
FROM net_devices
ORDER BY dev;

DROP VIEW IF EXISTS net_rx_actions;
CREATE PERFETTO VIEW net_rx_actions AS
SELECT
  s.ts,
  s.dur,
  CAST(SUBSTR(t.name, 13, 1) AS int) AS cpu
FROM slice s
LEFT JOIN track t
  ON s.track_id = t.id
WHERE s.name = "NET_RX";

DROP VIEW IF EXISTS net_tx_actions;
CREATE PERFETTO VIEW net_tx_actions AS
SELECT
  s.ts,
  s.dur,
  CAST(SUBSTR(t.name, 13, 1) AS int) AS cpu
FROM slice s
LEFT JOIN track t
  ON s.track_id = t.id
WHERE s.name = "NET_TX";

DROP VIEW IF EXISTS ipi_actions;
CREATE PERFETTO VIEW ipi_actions AS
SELECT
  s.ts,
  s.dur,
  CAST(SUBSTR(t.name, 13, 1) AS int) AS cpu
FROM slice s
LEFT JOIN track t
  ON s.track_id = t.id
WHERE s.name = "IRQ (IPI)";

DROP VIEW IF EXISTS cpu_freq_view;
CREATE PERFETTO VIEW cpu_freq_view AS
SELECT
  cpu,
  ts,
  LEAD(ts, 1, trace_end())
  OVER (PARTITION BY cpu ORDER BY ts) - ts AS dur,
  CAST(value AS INT) AS freq_khz
FROM counter
JOIN cpu_counter_track ON counter.track_id = cpu_counter_track.id
WHERE name = 'cpufreq';

DROP TABLE IF EXISTS cpu_freq_net_rx_action_per_core;
CREATE VIRTUAL TABLE cpu_freq_net_rx_action_per_core
USING SPAN_LEFT_JOIN(net_rx_actions PARTITIONED cpu, cpu_freq_view PARTITIONED cpu);

DROP TABLE IF EXISTS cpu_freq_net_tx_action_per_core;
CREATE VIRTUAL TABLE cpu_freq_net_tx_action_per_core
USING SPAN_LEFT_JOIN(net_tx_actions PARTITIONED cpu, cpu_freq_view PARTITIONED cpu);

DROP VIEW IF EXISTS total_net_rx_action_statistic;
CREATE PERFETTO VIEW total_net_rx_action_statistic AS
SELECT
  COUNT(1) AS times,
  SUM(dur) AS runtime,
  AVG(dur) AS avg_runtime,
  (SELECT COUNT(1) FROM rx_packets) AS total_packet
FROM net_rx_actions;

DROP VIEW IF EXISTS total_net_tx_action_statistic;
CREATE PERFETTO VIEW total_net_tx_action_statistic AS
SELECT
  COUNT(1) AS times,
  SUM(dur) AS runtime,
  AVG(dur) AS avg_runtime
FROM net_tx_actions;

DROP VIEW IF EXISTS total_ipi_action_statistic;
CREATE PERFETTO VIEW total_ipi_action_statistic AS
SELECT
  COUNT(1) AS times,
  SUM(dur) AS runtime,
  AVG(dur) AS avg_runtime
FROM ipi_actions;

DROP VIEW IF EXISTS activated_cores_net_rx;
CREATE PERFETTO VIEW activated_cores_net_rx AS
SELECT DISTINCT
  cpu
FROM net_rx_actions;

DROP VIEW IF EXISTS activated_cores_net_tx;
CREATE PERFETTO VIEW activated_cores_net_tx AS
SELECT DISTINCT
  cpu
FROM net_tx_actions;

DROP VIEW IF EXISTS per_core_net_rx_action_statistic;
CREATE PERFETTO VIEW per_core_net_rx_action_statistic AS
SELECT
  AndroidNetworkMetric_CoreNetRxActionStatistic(
    'id', cpu,
    'net_rx_action_statistic', AndroidNetworkMetric_NetRxActionStatistic(
      'count', (SELECT COUNT(1) FROM net_rx_actions AS na WHERE na.cpu = ac.cpu),
      'runtime_ms', (SELECT SUM(dur) / 1e6 FROM net_rx_actions AS na WHERE na.cpu = ac.cpu),
      'avg_runtime_ms', (SELECT AVG(dur) / 1e6 FROM net_rx_actions AS na WHERE na.cpu = ac.cpu),
      'avg_freq_khz', (SELECT SUM(dur * freq_khz) / SUM(dur) FROM cpu_freq_net_rx_action_per_core AS cc WHERE cc.cpu = ac.cpu),
      'mcycles', (SELECT CAST(SUM(dur * freq_khz / 1000) / 1e9 AS INT) FROM cpu_freq_net_rx_action_per_core AS cc WHERE cc.cpu = ac.cpu)
    )
  ) AS proto
FROM activated_cores_net_rx AS ac;

DROP VIEW IF EXISTS per_core_net_tx_action_statistic;
CREATE PERFETTO VIEW per_core_net_tx_action_statistic AS
SELECT
  AndroidNetworkMetric_CoreNetTxActionStatistic(
    'id', cpu,
    'net_tx_action_statistic', AndroidNetworkMetric_NetTxActionStatistic(
      'count', (SELECT COUNT(1) FROM net_tx_actions AS na WHERE na.cpu = ac.cpu),
      'runtime_ms', (SELECT SUM(dur) / 1e6 FROM net_tx_actions AS na WHERE na.cpu = ac.cpu),
      'avg_runtime_ms', (SELECT AVG(dur) / 1e6 FROM net_tx_actions AS na WHERE na.cpu = ac.cpu),
      'avg_freq_khz', (SELECT SUM(dur * freq_khz) / SUM(dur) FROM cpu_freq_net_tx_action_per_core AS cc WHERE cc.cpu = ac.cpu),
      'mcycles', (SELECT CAST(SUM(dur * freq_khz / 1000) / 1e9 AS INT) FROM cpu_freq_net_tx_action_per_core AS cc WHERE cc.cpu = ac.cpu)
    )
  ) AS proto
FROM activated_cores_net_tx AS ac;

DROP VIEW IF EXISTS android_netperf_output;
CREATE PERFETTO VIEW android_netperf_output AS
SELECT AndroidNetworkMetric(
    'net_devices', (
      SELECT
        RepeatedField(proto)
      FROM device_traffic_statistic
    ),
    'net_rx_action', AndroidNetworkMetric_NetRxAction(
      'total', AndroidNetworkMetric_NetRxActionStatistic(
        'count', (SELECT times FROM total_net_rx_action_statistic),
        'runtime_ms', (SELECT runtime / 1e6 FROM total_net_rx_action_statistic),
        'avg_runtime_ms', (SELECT avg_runtime / 1e6 FROM total_net_rx_action_statistic),
        'avg_freq_khz', (SELECT SUM(dur * freq_khz) / SUM(dur) FROM cpu_freq_net_rx_action_per_core),
        'mcycles', (SELECT CAST(SUM(dur * freq_khz / 1000) / 1e9 AS INT) FROM cpu_freq_net_rx_action_per_core)
      ),
      'core', (
        SELECT
          RepeatedField(proto)
        FROM per_core_net_rx_action_statistic
      ),
      'avg_interstack_latency_ms', (
        SELECT
          runtime / total_packet / 1e6
        FROM total_net_rx_action_statistic
      )
    ),
    'retransmission_rate', (
      SELECT
        (SELECT cnt FROM tcp_retransmitted_count) * 100.0 / COUNT(1)
      FROM tx_packets
    ),
    'kfree_skb_rate', (
      SELECT
        cnt * 100.0 / ((SELECT count(1) FROM rx_packets) + (SELECT count(1) FROM tx_packets))
      FROM kfree_skb_count
    ),
    'net_tx_action', AndroidNetworkMetric_NetTxAction(
      'total', AndroidNetworkMetric_NetTxActionStatistic(
        'count', (SELECT times FROM total_net_tx_action_statistic),
        'runtime_ms', (SELECT runtime / 1e6 FROM total_net_tx_action_statistic),
        'avg_runtime_ms', (SELECT avg_runtime / 1e6 FROM total_net_tx_action_statistic),
        'avg_freq_khz', (SELECT SUM(dur * freq_khz) / SUM(dur) FROM cpu_freq_net_tx_action_per_core),
        'mcycles', (SELECT CAST(SUM(dur * freq_khz / 1000) / 1e9 AS INT) FROM cpu_freq_net_tx_action_per_core)
      ),
      'core', (
        SELECT
          RepeatedField(proto)
        FROM per_core_net_tx_action_statistic
      )
    ),
    'ipi_action', AndroidNetworkMetric_IpiAction(
      'total', AndroidNetworkMetric_IpiActionStatistic(
        'count', (SELECT times FROM total_ipi_action_statistic),
        'runtime_ms', (SELECT runtime / 1e6 FROM total_ipi_action_statistic),
        'avg_runtime_ms', (SELECT avg_runtime / 1e6 FROM total_ipi_action_statistic)
      )
    )
  );
