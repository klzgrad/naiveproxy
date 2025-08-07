--
-- Copyright 2024 The Android Open Source Project
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
--

INCLUDE PERFETTO MODULE slices.with_context;

INCLUDE PERFETTO MODULE counters.intervals;

-- Converts an oom_adj score Integer to String sample name.
-- One of: cached, background, job, foreground_service, bfgs, foreground and
-- system.
CREATE PERFETTO FUNCTION android_oom_adj_score_to_bucket_name(
    -- `oom_score` value
    oom_score LONG
)
-- Returns the sample bucket based on the oom score.
RETURNS STRING AS
SELECT
  CASE
    WHEN $oom_score >= 900
    THEN 'cached'
    WHEN $oom_score BETWEEN 250 AND 900
    THEN 'background'
    WHEN $oom_score BETWEEN 201 AND 250
    THEN 'job'
    WHEN $oom_score = 200
    THEN 'foreground_service'
    WHEN $oom_score BETWEEN 100 AND 200
    THEN 'bfgs'
    WHEN $oom_score BETWEEN 0 AND 100
    THEN 'foreground'
    WHEN $oom_score < 0
    THEN 'system'
  END;

-- Converts an oom_adj score Integer to String bucket name.
-- Deprecated: use `android_oom_adj_score_to_bucket_name` instead.
CREATE PERFETTO FUNCTION android_oom_adj_score_to_detailed_bucket_name(
    -- oom_adj score.
    value LONG,
    -- android_app id of the process.
    android_appid LONG
)
-- Returns the oom_adj bucket.
RETURNS STRING AS
SELECT
  CASE
    WHEN $value = -1000
    THEN 'native'
    WHEN $value = -900
    THEN 'system'
    WHEN $value = -800
    THEN 'persistent_proc'
    WHEN $value = -700
    THEN 'persistent_service'
    WHEN $value = -600
    THEN 'logcat'
    WHEN $value = 0
    THEN 'foreground_app'
    WHEN $value = 50
    THEN 'perceptible_foreground_app'
    WHEN $value BETWEEN 100 AND 199
    THEN 'visible_app'
    WHEN $value BETWEEN 200 AND 224
    THEN 'perceptible_app'
    WHEN $value BETWEEN 225 AND 249
    THEN 'perceptible_medium_app'
    WHEN $value BETWEEN 250 AND 299
    THEN 'perceptible_low_app'
    WHEN $value BETWEEN 300 AND 399
    THEN 'backup'
    WHEN $value BETWEEN 400 AND 499
    THEN 'heavy_weight_app'
    WHEN $value BETWEEN 500 AND 599
    THEN 'service'
    WHEN $value BETWEEN 600 AND 699
    THEN 'home_app'
    WHEN $value BETWEEN 700 AND 799
    THEN 'previous_app'
    WHEN $value BETWEEN 800 AND 899
    THEN 'service_b'
    WHEN $value BETWEEN 900 AND 949
    THEN 'cached_app'
    WHEN $value >= 950
    THEN 'cached_app_lmk_first'
    WHEN $android_appid IS NULL
    THEN 'unknown'
    WHEN $android_appid < 10000
    THEN 'unknown_native'
    ELSE 'unknown_app'
  END;

CREATE PERFETTO TABLE _oom_adjuster_intervals AS
WITH
  reason AS (
    SELECT
      thread_slice.id AS oom_adj_id,
      thread_slice.ts AS oom_adj_ts,
      thread_slice.dur AS oom_adj_dur,
      thread_slice.track_id AS oom_adj_track_id,
      utid AS oom_adj_utid,
      thread_name AS oom_adj_thread_name,
      str_split(thread_slice.name, '_', 1) AS oom_adj_reason,
      slice.name AS oom_adj_trigger,
      lead(thread_slice.ts) OVER (ORDER BY thread_slice.ts) AS oom_adj_next_ts
    FROM thread_slice
    LEFT JOIN slice
      ON slice.id = thread_slice.parent_id AND slice.dur != -1
    WHERE
      thread_slice.name GLOB 'updateOomAdj_*' AND process_name = 'system_server'
  )
SELECT
  ts,
  dur,
  cast_int!(value) AS score,
  process.upid,
  process.name AS process_name,
  reason.oom_adj_id,
  reason.oom_adj_ts,
  reason.oom_adj_dur,
  reason.oom_adj_track_id,
  reason.oom_adj_thread_name,
  reason.oom_adj_utid,
  reason.oom_adj_reason,
  reason.oom_adj_trigger,
  android_appid
FROM counter_leading_intervals
    !(
      (
        SELECT counter.*
        FROM counter
        JOIN counter_track track
          ON track.id = counter.track_id AND track.name = 'oom_score_adj'
      )) AS counter
JOIN process_counter_track AS track
  ON counter.track_id = track.id
JOIN process
  USING (upid)
LEFT JOIN reason
  ON counter.ts BETWEEN oom_adj_ts AND coalesce(oom_adj_next_ts, trace_end())
WHERE
  track.name = 'oom_score_adj';

-- All oom adj state intervals across all processes along with the reason for the state update.
CREATE PERFETTO VIEW android_oom_adj_intervals (
  -- Timestamp the oom_adj score of the process changed
  ts TIMESTAMP,
  -- Duration until the next oom_adj score change of the process.
  dur DURATION,
  -- oom_adj score of the process.
  score LONG,
  -- oom_adj bucket of the process.
  bucket STRING,
  -- Upid of the process having an oom_adj update.
  upid JOINID(process.id),
  -- Name of the process having an oom_adj update.
  process_name STRING,
  -- Slice id of the latest oom_adj update in the system_server.
  oom_adj_id LONG,
  -- Timestamp of the latest oom_adj update in the system_server.
  oom_adj_ts TIMESTAMP,
  -- Duration of the latest oom_adj update in the system_server.
  oom_adj_dur DURATION,
  -- Track id of the latest oom_adj update in the system_server
  oom_adj_track_id JOINID(track.id),
  -- Thread name of the latest oom_adj update in the system_server.
  oom_adj_thread_name STRING,
  -- Reason for the latest oom_adj update in the system_server.
  oom_adj_reason STRING,
  -- Trigger for the latest oom_adj update in the system_server.
  oom_adj_trigger STRING
) AS
SELECT
  ts,
  dur,
  score,
  android_oom_adj_score_to_bucket_name(score) AS bucket,
  upid,
  process_name,
  oom_adj_id,
  oom_adj_ts,
  oom_adj_dur,
  oom_adj_track_id,
  oom_adj_thread_name,
  oom_adj_reason,
  oom_adj_trigger
FROM _oom_adjuster_intervals;
