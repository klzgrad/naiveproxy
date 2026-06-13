--
-- Copyright 2023 The Android Open Source Project
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

-- Android network packet events (from android.network_packets data source).
CREATE PERFETTO VIEW android_network_packets (
  -- Id of the slice.
  id ID,
  -- Timestamp.
  ts TIMESTAMP,
  -- Duration (non-zero only in aggregate events)
  dur DURATION,
  -- The track name (interface and direction)
  track_name STRING,
  -- Traffic package source (or uid=$X if not found)
  package_name STRING,
  -- Traffic interface name (linux interface name)
  iface STRING,
  -- Traffic direction ('Transmitted' or 'Received')
  direction STRING,
  -- Number of packets in this event
  packet_count LONG,
  -- Number of bytes in this event (wire size)
  packet_length LONG,
  -- Transport used for traffic in this event
  packet_transport STRING,
  -- TCP flags used by tcp frames in this event
  packet_tcp_flags LONG,
  -- The Android traffic tag of the network socket
  socket_tag STRING,
  -- The Linux user id of the network socket
  socket_uid LONG,
  -- The local port number (for udp or tcp only)
  local_port LONG,
  -- The remote port number (for udp or tcp only)
  remote_port LONG,
  -- 1-byte ICMP type identifier.
  packet_icmp_type LONG,
  -- 1-byte ICMP code identifier.
  packet_icmp_code LONG,
  -- Packet's tcp flags bitmask (e.g. FIN=0x1, SYN=0x2).
  packet_tcp_flags_int LONG,
  -- Packet's socket tag as an integer.
  socket_tag_int LONG
) AS
SELECT
  id,
  ts,
  dur,
  category AS track_name,
  name AS package_name,
  iface,
  direction,
  packet_count,
  packet_length,
  packet_transport,
  -- For backwards compatibility, the _str suffixed flags (which the ui shows)
  -- are exposed without suffix, and the integer fields get suffix instead.
  packet_tcp_flags_str AS packet_tcp_flags,
  packet_tcp_flags AS packet_tcp_flags_int,
  socket_tag_str AS socket_tag,
  socket_tag AS socket_tag_int,
  socket_uid,
  local_port,
  remote_port,
  packet_icmp_type,
  packet_icmp_code
FROM __intrinsic_android_network_packets
JOIN slice
  USING (id);

-- This helper is used to unparenthesize a column list expression. Currently,
-- the the pre-processor is unable to do both steps in one macro, so this macro
-- must be passed to __intrinsic_token_apply at the callsite.
CREATE PERFETTO MACRO _np_identity(
    x Expr
)
RETURNS Expr AS
$x;

-- Finds groups of overlapping slices and assigns them group ids.
--
-- An overlap group is a set of slices (or instants) that contiguously have >=1
-- slice present. For example, the following is one group:
--
--   ###   ##
--     #####
--
-- Overlap detection is partitioned by the partition columns, where overlaps are
-- only considered within the same partition.
--
-- Returns $src with two additional columns:
-- * group_id: the group the row belongs to (per partition_columns)
-- * max_end_so_far: the maximum end timestamp observed so far, useful for
--   determining the time since the last group or event (per partition_columns)
CREATE PERFETTO MACRO _add_overlap_group_id(
    src TableOrSubquery,
    partition_columns ColumnNameList
)
RETURNS TableOrSubquery AS
(
  WITH
    _max_endpoint AS (
      SELECT
        *,
        max(ts + dur) OVER (PARTITION BY __intrinsic_token_apply!(_np_identity, $partition_columns) ORDER BY ts ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) AS max_end_so_far
      FROM $src
    )
  SELECT
    *,
    sum(coalesce(ts > max_end_so_far, TRUE)) OVER (PARTITION BY __intrinsic_token_apply!(_np_identity, $partition_columns) ORDER BY ts) AS group_id
  FROM _max_endpoint
);

-- Computes network uptime spans based on an idle timeout model.
--
-- It is common in networking to have an interface active for some time after
-- use. For example, mobile networks are often connected for 10 or more seconds
-- after the last packet is sent or received. This macro simulates this timeout
-- and returns spans that approximate the underlying connected regions.
CREATE PERFETTO MACRO android_network_uptime_spans(
    -- A table/view/subquery containing the network events to apply the idle
    -- timeout model to. The table must contain all partition_columns, ts, dur,
    -- packet_count, and packet_length.
    src TableOrSubquery,
    -- A parenthesized set of columns to partition the analysis by.
    partition_columns ColumnNameList,
    -- The idle timeout, expressed in nanoseconds.
    timeout Expr
)
RETURNS TableOrSubquery AS
(
  -- This query applies the timeout as additional duration per item and performs
  -- pre-aggregation to speed up the overlap group detection below.
  WITH
    _quantized AS (
      SELECT
        __intrinsic_token_apply!(_np_identity, $partition_columns),
        min(ts) AS ts,
        max(ts + dur + $timeout) - min(ts) AS dur,
        sum(packet_count) AS packet_count,
        sum(packet_length) AS packet_length
      FROM $src
      GROUP BY
        __intrinsic_token_apply!(_np_identity, $partition_columns),
        CAST(ts / $timeout AS LONG)
    )
  SELECT
    __intrinsic_token_apply!(_np_identity, $partition_columns),
    min(ts) AS ts,
    max(ts + dur) - min(ts) AS dur,
    sum(packet_count) AS packet_count,
    sum(packet_length) AS packet_length
  FROM _add_overlap_group_id!(_quantized, $partition_columns)
  GROUP BY
    __intrinsic_token_apply!(_np_identity, $partition_columns),
    group_id
);

-- Compute the per-row uptime cost of network activity.
--
-- It is common in networking to have an interface active for some time after
-- use. For example, mobile networks are often connected for 10 or more seconds
-- after the last packet is sent or received. This macro computes a cost factor
-- indicating how much each row impacts the idle timer.
--
-- For example, assuming a 10s timeout, the first packet will extend the timeout
-- 10s in the future, and be assigned 10s of cost. If a packet arrives 4s later,
-- it pushes the timer an additional 4s, receiving 4s of cost. In this simple
-- case, cost is MIN(ts-last_packet_ts, timeout).
--
-- The complication is that network events can be aggregates, with more than one
-- packet. In such cases, we end up with a span with non-zero duration, rather
-- than an instant, and no easy way to compute time since the last packet.
--
-- The solution is to detect overlap regions and compute cost for the region as
-- a whole. The first event in each group receives the standard uptime cost as
-- described above. Each group has an additional cost equal to the duration of
-- the group which is distributed using packet count as weight.
--
-- For example (times in seconds, no partition, and 10 second timeout):
-- ```
-- ts=5,  dur=0, packet_count=1  -> group=1, uptime_cost=10
-- ts=7,  dur=0, packet_count=1  -> group=2, uptime_cost=2
-- ts=20, dur=5, packet_count=9  -> group=3, uptime_cost=14.5
-- ts=22, dur=0, packet_count=1  -> group=3, uptime_cost=0.5
-- ```
-- The third group spans ts=20 to ts=25, with a timeout at ts=35. This gives the
-- group a total cost of 15 which is distributed between the two rows. The 3rd
-- row receives 10s for being first, and 9/10 the duration cost (5*9/10=4.5).
--
-- The returned table schema is (id ID, uptime_cost INT64) where uptime cost is
-- in nanoseconds.
CREATE PERFETTO MACRO android_network_uptime_cost(
    -- A table/view/subquery containing the network events to apply the idle
    -- timeout model to. The table must contain all partition_columns, id, ts,
    -- dur, and packet_count.
    src TableOrSubquery,
    -- A parenthesized set of columns to partition the analysis by.
    partition_columns ColumnNameList,
    -- The idle timeout, expressed in nanoseconds.
    timeout Expr
)
RETURNS TableOrSubquery AS
(
  WITH
    _group_metrics AS (
      SELECT
        *,
        sum(packet_count) OVER group_window AS group_packets,
        max(ts + dur) OVER group_window - min(ts) OVER group_window AS group_dur
      FROM _add_overlap_group_id!($src, $partition_columns)
      WINDOW group_window AS (PARTITION BY __intrinsic_token_apply!(_np_identity, $partition_columns), group_id)
    ),
    _cost_parts AS (
      SELECT
        id,
        -- The first part is the standard time since last packet. For rows in
        -- the middle of a group, max_end_so_far>ts, so this is clamped to 0.
        coalesce(max(0, min($timeout, ts - max_end_so_far)), $timeout) AS initial_cost,
        -- The second part is the amortized duration cost, which is scaled by
        -- the packet count for this row, relative to the whole group.
        group_dur * packet_count / group_packets AS amortized_cost
      FROM _group_metrics
    )
  SELECT
    id,
    initial_cost + amortized_cost AS uptime_cost
  FROM _cost_parts
);
