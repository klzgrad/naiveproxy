-- Copyright 2025 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.suspend;

CREATE PERFETTO TABLE _android_kernel_wakelocks_base AS
WITH
  raw AS (
    SELECT
      -- Move back by one period here since our ts represents wakelock counts up
      -- to that point, but counters project forward from their ts.
      lag(ts) OVER (PARTITION BY track_id ORDER BY ts) AS ts,
      ts AS next_ts,
      name,
      extract_arg(dimension_arg_set_id, 'wakelock_type') AS type,
      value,
      lag(value) OVER (PARTITION BY track_id ORDER BY ts) AS lag_value
    FROM track AS t
    JOIN counter AS c
      ON t.id = c.track_id
    WHERE
      t.type = 'android_kernel_wakelock'
  )
SELECT
  ts,
  ts AS original_ts,
  next_ts - ts AS dur,
  name,
  hash(name) AS name_int,
  type,
  value - lag_value AS held_dur
FROM raw;

CREATE VIRTUAL TABLE _android_kernel_wakelocks_joined USING span_join (_android_kernel_wakelocks_base partitioned name_int, android_suspend_state);

-- Table of kernel (or native) wakelocks with held duration.
--
-- Subtracts suspended time from each period to calculate the
-- fraction of awake time for which the wakelock was held.
CREATE PERFETTO TABLE android_kernel_wakelocks (
  -- Timestamp.
  ts TIMESTAMP,
  -- Duration.
  dur DURATION,
  -- Duration spent awake.
  awake_dur DURATION,
  -- Kernel or native wakelock name.
  name STRING,
  -- 'kernel' or 'native'.
  type STRING,
  -- Time the wakelock was held.
  held_dur DURATION,
  -- Fraction of awake (not suspended) time the wakelock was held.
  held_ratio DOUBLE
) AS
WITH
  base AS (
    SELECT
      original_ts AS ts,
      name,
      type,
      held_dur,
      sum(dur) AS dur,
      sum(iif(power_state = 'awake', dur, 0)) AS awake_dur
    FROM _android_kernel_wakelocks_joined
    GROUP BY
      original_ts,
      name,
      type,
      held_dur
  )
SELECT
  ts,
  dur,
  awake_dur,
  name,
  type,
  cast_int!(held_dur) AS held_dur,
  max(min(held_dur / awake_dur, 1.0), 0.0) AS held_ratio
FROM base;
