--
-- Copyright 2026 The Android Open Source Project
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

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE wattson.gpu.estimates;

INCLUDE PERFETTO MODULE wattson.gpu.freq_idle;

INCLUDE PERFETTO MODULE wattson.tasks.gpu_tasks;

INCLUDE PERFETTO MODULE wattson.utils;

-- Step 1: Find active GPU regions (contiguous freq > 0)
CREATE PERFETTO TABLE _gpu_active_regions AS
WITH
  state_transitions AS (
    SELECT
      ts,
      dur,
      freq > 0 AS is_active,
      lag(freq > 0) OVER (ORDER BY ts) AS prev_active
    FROM _gpu_freq_idle
  ),
  region_tags AS (
    SELECT
      ts,
      dur,
      is_active,
      -- Sum transitions to create group IDs
      sum(CASE WHEN is_active != coalesce(prev_active, 0) THEN 1 ELSE 0 END) OVER (
        ORDER BY ts
      ) AS group_id
    FROM state_transitions
  )
SELECT min(ts) AS ts, max(ts + dur) - min(ts) AS dur, group_id
FROM region_tags
WHERE
  is_active = 1
GROUP BY
  group_id;

-- Step 2: Find tasks within active regions
CREATE PERFETTO TABLE _gpu_active_region_tasks AS
SELECT ii.ts, ii.dur, ta.uid, id_0 AS region_id
FROM _interval_intersect!(
  (
    _ii_subquery!(_gpu_active_regions),
    _ii_subquery!(_gpu_tasks)
  ),
  ()
) AS ii
JOIN _gpu_active_regions AS p
  ON p._auto_id = id_0
JOIN _gpu_tasks AS ta
  ON ta._auto_id = id_1;

-- Step 3: Find active region task boundaries
CREATE PERFETTO VIEW _gpu_active_region_boundaries AS
WITH
  ranked AS (
    SELECT
      region_id,
      ts,
      dur,
      uid,
      row_number() OVER (PARTITION BY region_id ORDER BY ts) AS rnk_asc,
      row_number() OVER (PARTITION BY region_id ORDER BY ts DESC) AS rnk_desc
    FROM _gpu_active_region_tasks
  )
SELECT
  region_id,
  min(ts) AS min_ts,
  max(ts + dur) AS max_end_ts,
  min(CASE WHEN rnk_asc = 1 THEN uid END) AS first_uid,
  min(CASE WHEN rnk_desc = 1 THEN uid END) AS last_uid
FROM ranked
GROUP BY
  region_id;

-- Step 4: Classify gaps within active regions
CREATE PERFETTO TABLE _all_gaps AS
SELECT ts, dur FROM _gpu_active_task_count WHERE active_tasks = 0;

CREATE PERFETTO TABLE _gaps_in_active_regions AS
SELECT ii.ts, ii.dur, id_0 AS region_id
FROM _interval_intersect!(
  (
    (SELECT _auto_id AS id, ts, dur FROM _gpu_active_regions),
    (SELECT _auto_id AS id, ts, dur FROM _all_gaps)
  ),
  ()
) AS ii
JOIN _gpu_active_regions AS p
  ON p._auto_id = id_0
JOIN _all_gaps AS g
  ON g._auto_id = id_1;

CREATE PERFETTO TABLE _gpu_active_region_gaps AS
SELECT
  g.ts,
  g.dur,
  CASE
    WHEN b.min_ts IS NULL THEN -1
    WHEN g.ts + g.dur <= b.min_ts THEN b.first_uid
    ELSE -1
  END AS uid
FROM _gaps_in_active_regions AS g
JOIN _gpu_active_region_boundaries AS b ON g.region_id = b.region_id;

-- Step 5: Final Gap Attribution with Power
CREATE PERFETTO TABLE _gpu_gap_attribution AS
SELECT ii.ts, ii.dur, coalesce(ta.uid, -1) AS uid, p.gpu_mw AS estimated_mw
FROM _interval_intersect!(
  (
    _ii_subquery!(_gpu_active_region_gaps),
    _ii_subquery!(_gpu_estimates_mw)
  ),
  ()
) AS ii
JOIN _gpu_active_region_gaps AS ta
  ON ta._auto_id = id_0
JOIN _gpu_estimates_mw AS p
  ON p._auto_id = id_1;
