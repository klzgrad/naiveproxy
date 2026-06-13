--
-- Copyright 2025 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the 'License');
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an 'AS IS' BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

INCLUDE PERFETTO MODULE android.oom_adjuster;

CREATE PERFETTO FUNCTION _android_lmk_kill_reason_string(
    -- kill reason enum value
    kill_reason LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $kill_reason = 0
    THEN 'PRESSURE_AFTER_KILL'
    WHEN $kill_reason = 1
    THEN 'NOT_RESPONDING'
    WHEN $kill_reason = 2
    THEN 'LOW_SWAP_AND_THRASHING'
    WHEN $kill_reason = 3
    THEN 'LOW_MEM_AND_SWAP'
    WHEN $kill_reason = 4
    THEN 'LOW_MEM_AND_THRASHING'
    WHEN $kill_reason = 5
    THEN 'DIRECT_RECL_AND_THRASHING'
    WHEN $kill_reason = 6
    THEN 'LOW_MEM_AND_SWAP_UTIL'
    WHEN $kill_reason = 7
    THEN 'LOW_FILECACHE_AFTER_THRASHING'
    WHEN $kill_reason = 8
    THEN 'LOW_MEM'
    WHEN $kill_reason = 9
    THEN 'DIRECT_RECL_STUCK'
    WHEN $kill_reason >= 1000 AND $kill_reason < 2000
    THEN 'VENDOR_REASON'
    ELSE 'UNKNOWN'
  END;

-- Kills recorded via the instant lowmemorykiller track
-- Introduced with ag/32702401 (Mar 2025)
CREATE PERFETTO TABLE _lmk_instant_events AS
SELECT
  ts,
  CAST(str_split(slice.name, ',', 1) AS LONG) AS pid,
  CAST(str_split(slice.name, ',', 2) AS LONG) AS kill_reason_raw,
  CAST(str_split(slice.name, ',', 3) AS LONG) AS oom_score_adj
FROM slice
JOIN process_track AS pt
  ON slice.track_id = pt.id
WHERE
  pt.name = 'lowmemorykiller' AND slice.name GLOB 'lmk,*' AND dur = 0;

-- LMK events based on the slice atrace event (legacy)
-- Introduced with aosp/1782391 (Aug 2021)
CREATE PERFETTO TABLE _legacy_lmk_events AS
SELECT
  ts,
  CAST(str_split(slice.name, ',', 1) AS LONG) AS pid,
  CAST(str_split(slice.name, ',', 2) AS LONG) AS kill_reason_raw,
  CAST(str_split(slice.name, ',', 3) AS LONG) AS oom_score_adj
FROM slice
WHERE
  slice.name GLOB 'lmk,*' AND dur > 0;

-- The original lmkd trace events, deprecated in 2022
CREATE PERFETTO TABLE _kill_one_process_events AS
WITH
  kills AS (
    SELECT
      c.ts,
      c.value AS pid
    FROM counter AS c
    JOIN counter_track AS ct
      ON c.track_id = ct.id
    WHERE
      ct.name = 'kill_one_process' AND c.value > 0
  ),
  si AS (
    SELECT
      si.ts,
      si.dur,
      si.score,
      process.pid
    FROM android_oom_adj_intervals AS si
    JOIN process
      USING (upid)
  )
SELECT
  kills.ts,
  kills.pid,
  NULL AS kill_reason_raw,
  si.score AS oom_score_adj
FROM kills
LEFT JOIN si
  ON kills.pid = si.pid AND kills.ts >= si.ts AND kills.ts < si.ts + si.dur;

CREATE PERFETTO VIEW _android_lmk_events AS
WITH
  selector AS (
    SELECT
      CASE
        WHEN (
          SELECT
            count(1)
          FROM _lmk_instant_events
        ) > 0
        THEN '_lmk_instant_events'
        WHEN (
          SELECT
            count(1)
          FROM _legacy_lmk_events
        ) > 0
        THEN '_legacy_lmk_events'
        ELSE '_kill_one_process_events'
      END AS s
  )
SELECT
  *
FROM _lmk_instant_events
WHERE
  (
    SELECT
      s
    FROM selector
  ) = '_lmk_instant_events'
UNION ALL
SELECT
  *
FROM _legacy_lmk_events
WHERE
  (
    SELECT
      s
    FROM selector
  ) = '_legacy_lmk_events'
UNION ALL
SELECT
  *
FROM _kill_one_process_events
WHERE
  (
    SELECT
      s
    FROM selector
  ) = '_kill_one_process_events';

-- Android Low-Memory Kill (LMK) events
CREATE PERFETTO TABLE android_lmk_events (
  -- timestamp of the kill being requested by lmkd
  ts TIMESTAMP,
  -- upid of the process being killed
  upid JOINID(process.id),
  -- pid of the process being killed
  pid LONG,
  -- process name of the process being killed
  process_name STRING,
  -- oom_score_adj of the process being killed
  oom_score_adj LONG,
  -- lmkd kill_reason (matches lmkd/statslog.h kill_reasons enum)
  kill_reason STRING,
  -- lmkd kill_reason enum value
  kill_reason_raw LONG
) AS
SELECT
  ts,
  process.pid,
  process.upid,
  process.name AS process_name,
  oom_score_adj,
  _android_lmk_kill_reason_string(kill_reason_raw) AS kill_reason,
  kill_reason_raw
FROM _android_lmk_events AS evt
LEFT JOIN process
  ON (
    evt.pid = process.pid
    AND evt.ts >= coalesce(process.start_ts, trace_start())
    AND evt.ts <= coalesce(process.end_ts, trace_end())
  );
