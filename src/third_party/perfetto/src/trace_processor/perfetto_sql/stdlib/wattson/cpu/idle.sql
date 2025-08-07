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

INCLUDE PERFETTO MODULE counters.intervals;

INCLUDE PERFETTO MODULE wattson.device_infos;

-- Get the corresponding deep idle time offset based on device and CPU.
CREATE PERFETTO VIEW _filtered_deep_idle_offsets AS
SELECT
  cpu,
  offset_ns
FROM _device_cpu_deep_idle_offsets AS offsets
JOIN _wattson_device AS device
  ON offsets.device = device.name;

-- Adjust duration of active portion to be slightly longer to account for
-- overhead cost of transitioning out of deep idle. This is done because the
-- device is active and consumes power for longer than the logs actually report.
CREATE PERFETTO TABLE _adjusted_deep_idle AS
WITH
  idle_prev AS (
    SELECT
      ts,
      lag(ts, 1, trace_start()) OVER (PARTITION BY cpu ORDER BY ts) AS prev_ts,
      value AS idle,
      cli.value - cli.delta_value AS idle_prev,
      cct.cpu
    -- Same as cpu_idle_counters, but extracts some additional info that isn't
    -- nominally present in cpu_idle_counters, such that the already calculated
    -- lag values are reused instead of recomputed
    FROM counter_leading_intervals!((
      SELECT c.*
      FROM counter c
      JOIN cpu_counter_track cct ON cct.id = c.track_id AND cct.name = 'cpuidle'
    )) AS cli
    JOIN cpu_counter_track AS cct
      ON cli.track_id = cct.id
    WHERE
      dur > 0
  ),
  -- Adjusted ts if applicable, which makes the current active state longer if
  -- it is coming from an idle exit.
  idle_mod AS (
    SELECT
      iif(
        idle_prev = 1 AND idle = 4294967295,
        -- extend ts backwards by offset_ns at most up to prev_ts
        max(ts - offset_ns, prev_ts),
        ts
      ) AS ts,
      cpu,
      idle
    FROM idle_prev
    JOIN _filtered_deep_idle_offsets
      USING (cpu)
  ),
  -- Use EITHER idle states as is OR device specific override of idle states
  _cpu_idle AS (
    -- Idle state calculations as is
    SELECT
      ts,
      lead(ts, 1, trace_end()) OVER (PARTITION BY cpu ORDER BY ts) - ts AS dur,
      cpu,
      cast_int!(IIF(idle = 4294967295, -1, idle)) AS idle
    FROM idle_mod
    WHERE
      NOT EXISTS(
        SELECT
          1
        FROM _idle_state_map_override
      )
    UNION ALL
    -- Device specific override of idle states
    SELECT
      ts,
      lead(ts, 1, trace_end()) OVER (PARTITION BY cpu ORDER BY ts) - ts AS dur,
      cpu,
      override_idle AS idle
    FROM idle_mod
    JOIN _idle_state_map_override AS idle_map
      ON idle_mod.idle = idle_map.nominal_idle
    WHERE
      EXISTS(
        SELECT
          1
        FROM _idle_state_map_override
      )
  ),
  -- Get first idle transition per CPU
  first_cpu_idle_slices AS (
    SELECT
      ts,
      cpu
    FROM _cpu_idle
    GROUP BY
      cpu
    ORDER BY
      ts ASC
  )
-- Prepend NULL slices up to first idle events on a per CPU basis
SELECT
  -- Construct slices from first cpu ts up to first freq event for each cpu
  trace_start() AS ts,
  first_slices.ts - trace_start() AS dur,
  first_slices.cpu,
  NULL AS idle
FROM first_cpu_idle_slices AS first_slices
WHERE
  dur > 0
UNION ALL
SELECT
  ts,
  dur,
  cpu,
  idle
FROM _cpu_idle
-- Some durations are 0 post-adjustment and won't work with interval intersect
WHERE
  dur > 0
UNION ALL
-- Add empty cpu idle counters for CPUs that are physically present, but did not
-- have a single idle event register. The time region needs to be defined so
-- that interval_intersect doesn't remove the undefined time region.
SELECT
  trace_start() AS ts,
  trace_dur() AS dur,
  cpu,
  NULL AS idle
FROM _dev_cpu_policy_map
WHERE
  NOT cpu IN (
    SELECT
      cpu
    FROM first_cpu_idle_slices
  );
