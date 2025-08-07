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

-- Uses cluster_id which has been calculated using the cpu_capacity in order
-- to determine the cluster type for cpus with 2, 3 or 4 clusters
-- indicating whether they are "little", "medium" or "big".

INCLUDE PERFETTO MODULE intervals.overlap;

CREATE PERFETTO TABLE _cores AS
WITH
  data(cluster_id, cluster_type, cluster_count) AS (
    SELECT
      *
    FROM (VALUES
      (0, 'little', 2),
      (1, 'big', 2),
      (0, 'little', 3),
      (1, 'medium', 3),
      (2, 'big', 3),
      (0, 'little', 4),
      (1, 'medium', 4),
      (2, 'medium', 4),
      (3, 'big', 4)) AS _values
  )
SELECT
  *
FROM data;

-- Stores the mapping of a cpu to its cluster type - e.g. little, medium, big.
-- This cluster type is determined by initially using cpu_capacity from sysfs
-- and grouping clusters with identical capacities, ordered by size.
-- In the case that capacities are not present, max frequency is used instead.
-- If nothing is avaiable, NULL is returned.
CREATE PERFETTO TABLE android_cpu_cluster_mapping (
  -- Alias of `cpu.ucpu`.
  ucpu LONG,
  -- Alias of `cpu.cpu`.
  cpu LONG,
  -- The cluster type of the CPU.
  cluster_type STRING
) AS
SELECT
  ucpu,
  cpu,
  _cores.cluster_type AS cluster_type
FROM cpu
LEFT JOIN _cores
  ON _cores.cluster_id = cpu.cluster_id
  AND _cores.cluster_count = (
    SELECT
      count(DISTINCT cluster_id)
    FROM cpu
  );

-- The count of active CPUs with a given cluster type over time.
CREATE PERFETTO FUNCTION _active_cpu_count_for_cluster_type(
    -- Type of the CPU cluster as reported by android_cpu_cluster_mapping. Usually 'little', 'medium' or 'big'.
    cluster_type STRING
)
RETURNS TABLE (
  -- Timestamp when the number of active CPU changed.
  ts TIMESTAMP,
  -- Number of active CPUs, covering the range from this timestamp to the next
  -- row's timestamp.
  active_cpu_count LONG
) AS
WITH
  -- Materialise the relevant clusters to avoid calling a function for each row of the sched table.
  cluster AS (
    SELECT
      ucpu
    FROM android_cpu_cluster_mapping
    WHERE
      cluster_type = $cluster_type
  ),
  -- Filter sched events corresponding to running tasks.
  -- thread(s) with is_idle = 1 are the swapper threads / idle tasks.
  tasks AS (
    SELECT
      ts,
      dur
    FROM sched
    WHERE
      ucpu IN (
        SELECT
          ucpu
        FROM cluster
      )
      AND NOT utid IN (
        SELECT
          utid
        FROM thread
        WHERE
          is_idle
      )
  )
SELECT
  ts,
  value AS active_cpu_count
FROM intervals_overlap_count!(tasks, ts, dur)
ORDER BY
  ts;
