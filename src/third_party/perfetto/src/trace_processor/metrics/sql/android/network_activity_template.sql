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

INCLUDE PERFETTO MODULE android.network_packets;

-- Creates a view of aggregated network activity. It is common among networking
-- to have the interface active for some time after network use. For example, in
-- mobile networking, it is common to have the cellular interface active for 10
-- or more seconds after the last packet was sent or received. This view takes
-- raw packet timing and aggregates it into something that approximates the
-- activity of the underlying interface.
--
-- @arg view_name        The name of the output view.
-- @arg group_by         Expression to group by (set to 'null' for no grouping).
-- @arg filter           Expression on `android_network_packets` to filter by.
-- @arg idle_ns          The amount of time before considering the network idle.
-- @arg quant_ns         Quantization value, to group rows before the heavy
--                       part of the query. This should be smaller than idle_ns.
--
-- @column group_by      The group_by columns are all present in the output.
-- @column ts            The timestamp indicating the start of the segment.
-- @column dur           The duration of the current segment.
-- @column packet_count  The total number of packets in this segment.
-- @column packet_length The total number of bytes for packets in this segment.
DROP VIEW IF EXISTS {{view_name}};
CREATE PERFETTO VIEW {{view_name}} AS
WITH quantized AS (
  SELECT
    {{group_by}},
    MIN(ts) AS ts,
    MAX(ts+dur)-MIN(ts) AS dur,
    SUM(packet_count) AS packet_count,
    SUM(packet_length) AS packet_length
  FROM android_network_packets
  WHERE {{filter}}
  GROUP BY CAST(ts / {{quant_ns}} AS INT64), {{group_by}}
),
with_last AS (
  SELECT
    *,
    MAX(ts+dur) OVER (
      PARTITION BY {{group_by}}
      ORDER BY ts
      ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING
    ) AS max_end_so_far
  FROM quantized
),
with_group AS (
  SELECT
    *,
    COUNT(IIF(ts-max_end_so_far>{{idle_ns}}, 1, null)) OVER (
      PARTITION BY {{group_by}}
      ORDER BY ts
    ) AS group_id
  FROM with_last
)
SELECT
  {{group_by}},
  MIN(ts) AS ts,
  MAX(ts+dur)-MIN(ts)+{{idle_ns}} AS dur,
  SUM(packet_count) AS packet_count,
  SUM(packet_length) AS packet_length
FROM with_group
GROUP BY group_id, {{group_by}}
