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

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE wattson.estimates;

INCLUDE PERFETTO MODULE wattson.tasks.task_slices;

INCLUDE PERFETTO MODULE wattson.utils;

-- Gets the slices where the CPU transitions from deep idle to active, and the
-- associated task that causes the idle exit
CREATE PERFETTO TABLE _idle_w_tasks AS
WITH
  _ii_idle_tasks AS (
    SELECT
      ii.ts,
      ii.dur,
      ii.cpu,
      tasks.utid,
      tasks.upid,
      id_1 AS idle_group
    FROM _interval_intersect!(
    (
      _ii_subquery!(_wattson_task_slices),
      _ii_subquery!(_idle_exits)
    ),
    (cpu)
  ) AS ii
    JOIN _wattson_task_slices AS tasks
      ON tasks._auto_id = id_0
    ORDER BY
      ii.ts ASC
  ),
  -- Since sorted by time, MIN() is fast aggregate function that will return the
  -- first time slice, which will be the utid = 0 slice immediately succeeding the
  -- idle to active transition, and immediately preceding the active task
  first_swapper_slice AS (
    SELECT
      ts,
      dur,
      cpu,
      idle_group,
      min(ts) AS min
    FROM _ii_idle_tasks
    WHERE
      utid IN (
        SELECT
          utid
        FROM thread
        WHERE
          is_idle
      )
    GROUP BY
      idle_group
  ),
  -- MIN() here will give the first active task immediately succeeding the idle
  -- to active transition slice, which means this the the task that causes the
  -- idle exit
  first_non_swapper_slice AS (
    SELECT
      idle_group,
      utid,
      upid,
      min(ts) AS min,
      min(ts) + dur AS next_ts
    FROM _ii_idle_tasks
    WHERE
      NOT utid IN (
        SELECT
          utid
        FROM thread
        WHERE
          is_idle
      )
    GROUP BY
      idle_group
  ),
  -- MAX() here will give the last time slice in the group. This will be the
  -- utid = 0 slice immediately preceding the active to idle transition.
  last_swapper_slice AS (
    SELECT
      ts,
      dur,
      cpu,
      idle_group,
      max(ts) AS min
    FROM _ii_idle_tasks
    WHERE
      utid IN (
        SELECT
          utid
        FROM thread
        WHERE
          is_idle
      )
    GROUP BY
      idle_group
  )
SELECT
  swapper_info.ts,
  swapper_info.dur,
  swapper_info.cpu,
  task_info.utid,
  task_info.upid
FROM first_non_swapper_slice AS task_info
JOIN first_swapper_slice AS swapper_info
  USING (idle_group)
UNION ALL
-- Adds the last slice to idle transition attribution IF this is a singleton
-- task wakeup. This is true if there is only one task between swapper idle
-- exits/wakeups. For example, groups with order of swapper, task X, swapper
-- will be included. Entries that have multiple task between swappers, such as
-- swapper, task X, task Y, swapper will not be included.
SELECT
  swapper_info.ts,
  swapper_info.dur,
  swapper_info.cpu,
  task_info.utid,
  task_info.upid
FROM first_non_swapper_slice AS task_info
JOIN last_swapper_slice AS swapper_info
  USING (idle_group)
WHERE
  ts = next_ts;

-- Interval intersect with the estimate power track, so that each slice can be
-- attributed to the power of the CPU in that time duration
CREATE PERFETTO TABLE _idle_transition_cost AS
SELECT
  ii.ts,
  ii.dur,
  tasks.cpu,
  tasks.utid,
  tasks.upid,
  CASE tasks.cpu
    WHEN 0
    THEN power.cpu0_mw
    WHEN 1
    THEN power.cpu1_mw
    WHEN 2
    THEN power.cpu2_mw
    WHEN 3
    THEN power.cpu3_mw
    WHEN 4
    THEN power.cpu4_mw
    WHEN 5
    THEN power.cpu5_mw
    WHEN 6
    THEN power.cpu6_mw
    WHEN 7
    THEN power.cpu7_mw
    ELSE 0
  END AS estimated_mw
FROM _interval_intersect!(
  (
    _ii_subquery!(_idle_w_tasks),
    _ii_subquery!(_system_state_mw)
  ),
  ()
) AS ii
JOIN _idle_w_tasks AS tasks
  ON tasks._auto_id = id_0
JOIN _system_state_mw AS power
  ON power._auto_id = id_1;

-- Macro for easily filtering idle attribution to a specified time window. This
-- information can then further be filtered by specific CPU and GROUP BY on
-- either utid or upid
CREATE PERFETTO FUNCTION _filter_idle_attribution(
    ts TIMESTAMP,
    dur LONG
)
RETURNS TABLE (
  idle_cost_mws LONG,
  utid JOINID(task.id),
  upid JOINID(process.id),
  cpu JOINID(cpu.id)
) AS
-- Give the negative sum of idle costs to the swapper thread, which by
-- definition has a utid = 0, upid = 0, and by definition will not be defined,
-- so need to UNION to manually add swapper thread
WITH
  base AS (
    SELECT
      cost.estimated_mw * cost.dur / 1e9 AS idle_cost_mws,
      cost.utid,
      cost.upid,
      cost.cpu
    FROM _interval_intersect_single!(
    $ts, $dur, _ii_subquery!(_idle_transition_cost)
  ) AS ii
    JOIN _idle_transition_cost AS cost
      ON cost._auto_id = id
  )
SELECT
  idle_cost_mws,
  utid,
  upid,
  cpu
FROM base
UNION ALL
SELECT
  -1 * sum(idle_cost_mws) AS idle_cost_mws,
  0 AS utid,
  0 AS upid,
  cpu
FROM base
GROUP BY
  cpu;
