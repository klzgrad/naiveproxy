--
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

INCLUDE PERFETTO MODULE counters.intervals;

-- Table of tracks for CPU-per-UID data. Each row represents one UID / cluster
-- combination.
CREATE PERFETTO TABLE android_cpu_per_uid_track (
  -- ID of the track; can be joined with cpu_per_uid_counter.
  id LONG,
  -- UID doing the work.
  uid LONG,
  -- Cluster ID for the track, starting from 0, typically with larger numbers
  -- meaning larger cores.
  cluster LONG,
  -- A package name for the UID. If there are multiple for a UID, one is chosen
  -- arbitrarily. UIDs below 10000 always have null package name.
  package_name STRING
) AS
WITH
  track AS (
    SELECT
      id,
      extract_arg(dimension_arg_set_id, 'uid') AS uid,
      extract_arg(dimension_arg_set_id, 'cluster') AS cluster
    FROM counter_track
    WHERE
      type = 'android_cpu_per_uid'
  )
SELECT
  id,
  uid,
  cluster,
  (
    SELECT
      package_name
    FROM package_list
    WHERE
      uid = track.uid % 100000 AND uid >= 10000
    LIMIT 1
  ) AS package_name
FROM track;

-- Table of counters for CPU-per-UID data. Each row represents one instant in
-- time for one UID / cluster.
CREATE PERFETTO TABLE android_cpu_per_uid_counter (
  -- ID for the row.
  id LONG,
  -- Timestamp for the row.
  ts LONG,
  -- Time to the next measurement for the UID / cluster combination.
  dur LONG,
  -- Associated track.
  track_id LONG,
  -- CPU time measurement for this time period (milliseconds).
  diff_ms LONG,
  -- Inferred CPU use value for the period where 1.0 means a single core at
  -- 100% utilisation.
  cpu_ratio DOUBLE
) AS
WITH
  deltas AS (
    SELECT
      *
    FROM counter_leading_intervals!((
    select 
      c.id,
      c.ts,
      c.track_id,
      c.value
    from counter c join android_cpu_per_uid_track t on t.id = c.track_id
  ))
  )
SELECT
  id,
  ts,
  dur,
  track_id,
  next_value - value AS diff_ms,
  (
    next_value - value
  ) * 1e6 / dur AS cpu_ratio
FROM deltas
WHERE
  next_value IS NOT NULL;
