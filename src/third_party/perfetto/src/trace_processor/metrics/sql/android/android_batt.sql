--
-- Copyright 2019 The Android Open Source Project
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
INCLUDE PERFETTO MODULE android.battery;
INCLUDE PERFETTO MODULE android.battery_stats;
INCLUDE PERFETTO MODULE android.suspend;
INCLUDE PERFETTO MODULE counters.intervals;

DROP VIEW IF EXISTS battery_view;
CREATE PERFETTO VIEW battery_view AS
SELECT * FROM android_battery_charge;

DROP TABLE IF EXISTS android_batt_wakelocks_merged;
CREATE PERFETTO TABLE android_batt_wakelocks_merged AS
SELECT
  MIN(ts) AS ts,
  MAX(ts_end) AS ts_end
FROM (
    SELECT
      *,
      SUM(new_group) OVER (ORDER BY ts) AS group_id
    FROM (
        SELECT
          ts,
          ts + dur AS ts_end,
          -- There is a new group if there was a gap before this wakelock.
          -- i.e. the max end timestamp of all preceding wakelocks is before
          -- the start timestamp of this one.
          -- The null check is for the first row which is always a new group.
          IFNULL(
            MAX(ts + dur) OVER (
              ORDER BY ts
              ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING
            ) < ts,
            TRUE
          ) AS new_group
        FROM slice
        WHERE slice.name GLOB 'WakeLock *' AND dur != -1
    )
)
GROUP BY group_id;

-- TODO(simonmacm) remove this shim once no longer used internally
DROP TABLE IF EXISTS suspend_slice_;
CREATE PERFETTO TABLE suspend_slice_ AS
SELECT ts, dur FROM android_suspend_state where power_state = 'suspended';

DROP TABLE IF EXISTS screen_state_span;
CREATE PERFETTO TABLE screen_state_span AS
WITH screen_state AS (
  SELECT counter.id, ts, 0 AS track_id, value
  FROM counter
  JOIN counter_track ON counter_track.id = counter.track_id
  WHERE name = 'ScreenState'
)
SELECT * FROM counter_leading_intervals!(screen_state);

DROP TABLE IF EXISTS screen_state_span_with_suspend;
CREATE VIRTUAL TABLE screen_state_span_with_suspend
USING span_join(screen_state_span, suspend_slice_);

DROP TABLE IF EXISTS power_mw_intervals;
CREATE PERFETTO TABLE power_mw_intervals AS
WITH power_mw_counter AS (
  SELECT counter.id, ts, track_id, value
  FROM counter
  JOIN counter_track ON counter_track.id = counter.track_id
  WHERE name = 'batt.power_mw'
)
SELECT * FROM counter_leading_intervals!(power_mw_counter);

DROP TABLE IF EXISTS charge_diff_mw;
CREATE PERFETTO TABLE charge_diff_mw AS
with energy_counters as (
select
  ts,
  CASE
    WHEN energy_counter_uwh IS NOT NULL THEN energy_counter_uwh
    ELSE charge_uah *  voltage_uv / 1e12 END as energy
 from android_battery_charge
), start_energy as (
  select
  min(ts) as ts,
  energy
  from energy_counters
), end_energy as (
  select
  max(ts) as ts,
  energy
  from energy_counters
)
select
  -- If the battery is discharging, the start energy value will be greater than
  -- the end and the estimate will report a positive value.
  -- Battery energy is in watt hours, so multiply by 3600 to convert to joules.
  -- Convert perfetto timestamp from nanoseconds to seconds.
  -- Divide energy by seconds and convert to milliwatts.
  (s.energy - e.energy) * 3600 * 1e3 / ((e.ts - s.ts) / 1e9) as estimate
from start_energy s, end_energy e;

DROP VIEW IF EXISTS android_batt_output;
CREATE PERFETTO VIEW android_batt_output AS
SELECT AndroidBatteryMetric(
  'battery_counters', (
    SELECT RepeatedField(
      AndroidBatteryMetric_BatteryCounters(
        'timestamp_ns', ts,
        'charge_counter_uah', charge_uah,
        'capacity_percent', capacity_percent,
        'current_ua', current_ua,
        'current_avg_ua', current_avg_ua,
        'voltage_uv', voltage_uv
      )
    )
    FROM android_battery_charge
  ),
  'battery_aggregates', (
    SELECT NULL_IF_EMPTY(AndroidBatteryMetric_BatteryAggregates(
      'total_screen_off_ns',
      SUM(CASE WHEN state = 1.0 AND tbl = 'total' THEN dur ELSE 0 END),
      'total_screen_on_ns',
      SUM(CASE WHEN state = 2.0 AND tbl = 'total' THEN dur ELSE 0 END),
      'total_screen_doze_ns',
      SUM(CASE WHEN state = 3.0 AND tbl = 'total' THEN dur ELSE 0 END),
      'sleep_ns',
      (SELECT SUM(dur) FROM suspend_slice_),
      'sleep_screen_off_ns',
      SUM(CASE WHEN state = 1.0 AND tbl = 'sleep' THEN dur ELSE 0 END),
      'sleep_screen_on_ns',
      SUM(CASE WHEN state = 2.0 AND tbl = 'sleep' THEN dur ELSE 0 END),
      'sleep_screen_doze_ns',
      SUM(CASE WHEN state = 3.0 AND tbl = 'sleep' THEN dur ELSE 0 END),
      'total_wakelock_ns',
      (SELECT SUM(ts_end - ts) FROM android_batt_wakelocks_merged),
      'avg_power_mw',
      (SELECT SUM(value * dur) / SUM(dur) FROM power_mw_intervals),
      'avg_power_from_charge_diff_mw',
      (select estimate FROM charge_diff_mw)
      ))
    FROM (
      SELECT dur, value AS state, 'total' AS tbl
      FROM screen_state_span
      UNION ALL
      SELECT dur, value AS state, 'sleep' AS tbl
      FROM screen_state_span_with_suspend
    )
  ),
  'suspend_period', (
    SELECT RepeatedField(
      AndroidBatteryMetric_SuspendPeriod(
        'timestamp_ns', ts,
        'duration_ns', dur
      )
    )
    FROM suspend_slice_
  )
);
