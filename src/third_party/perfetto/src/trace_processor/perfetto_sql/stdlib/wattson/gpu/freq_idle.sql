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

INCLUDE PERFETTO MODULE android.gpu.frequency;

INCLUDE PERFETTO MODULE android.gpu.mali_power_state;

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE wattson.device_infos;

INCLUDE PERFETTO MODULE wattson.utils;

-- GPU power state which is analogous to CPU idle state
CREATE PERFETTO TABLE _pvr_gpu_power_state (
  -- Timestamp
  ts TIMESTAMP,
  -- Duration
  dur DURATION,
  -- GPU power state
  power_state LONG
) AS
SELECT
  s.ts,
  iif(s.dur = -1, trace_end() - s.ts, s.dur) AS dur,
  -- Map slice names to integer states
  CASE s.name WHEN 'OFF' THEN 0 WHEN 'PG' THEN 1 WHEN 'ON' THEN 2 ELSE -1 END AS power_state
FROM slice AS s
JOIN track AS t
  ON s.track_id = t.id
WHERE
  t.name = 'powervr_gpu_power_state';

-- Gapless time slices of GPU freq from trace_start() to trace_end()
CREATE PERFETTO TABLE _gapless_gpu_freq AS
WITH
  nominal_freqs AS (
    -- Prepend NULL slices up to first freq events
    SELECT
      trace_start() AS ts,
      min(ts) - trace_start() AS dur,
      NULL AS freq,
      NULL AS prev_freq,
      next_gpu_freq AS next_freq,
      gpu_id
    FROM android_gpu_frequency
    WHERE
      gpu_id = (
        SELECT
          gpu_id
        FROM _gpuid_map
        JOIN _wattson_device
          ON _gpuid_map.device = _wattson_device.name
      )
    UNION ALL
    SELECT
      ts,
      dur,
      gpu_freq AS freq,
      prev_gpu_freq AS prev_freq,
      next_gpu_freq AS next_freq,
      gpu_id
    FROM android_gpu_frequency
    WHERE
      gpu_id = (
        SELECT
          gpu_id
        FROM _gpuid_map
        JOIN _wattson_device
          ON _gpuid_map.device = _wattson_device.name
      )
  )
SELECT
  *
FROM nominal_freqs;

-- A single source for GPU power state information
-- from either mali or pvr which are mutually exclusive
CREATE PERFETTO VIEW _gpu_power_state AS
SELECT
  ts,
  dur,
  power_state
FROM android_mali_gpu_power_state
UNION ALL
SELECT
  ts,
  dur,
  power_state
FROM _pvr_gpu_power_state;

-- Gapless time slices of GPU idle from trace_start() to trace_end()
CREATE PERFETTO TABLE _gapless_gpu_power_state AS
-- Prepend NULL slices up to first idle events
WITH
  nominal_power_states AS (
    SELECT
      trace_start() AS ts,
      min(ts) - trace_start() AS dur,
      NULL AS power_state
    FROM _gpu_power_state
    UNION ALL
    SELECT
      ts,
      dur,
      power_state
    FROM _gpu_power_state
  )
SELECT
  *
FROM nominal_power_states;

CREATE PERFETTO TABLE _gpu_freq_idle AS
WITH
  base AS (
    SELECT
      ii.ts,
      ii.dur,
      freq.freq,
      freq.prev_freq,
      freq.next_freq,
      idle.power_state
    FROM _interval_intersect!(
    (
      _ii_subquery!(_gapless_gpu_freq),
      _ii_subquery!(_gapless_gpu_power_state)
    ),
    ()
  ) AS ii
    JOIN _gapless_gpu_freq AS freq
      ON freq._auto_id = id_0
    JOIN _gapless_gpu_power_state AS idle
      ON idle._auto_id = id_1
  )
SELECT
  ts,
  dur,
  -- From power perspective, even though driver is reporting freq=0, it actually
  -- is still at the previous frequency but in a shallower idle state.
  --
  -- This logic accounts for the inverse idle state relative to CPU idle states,
  -- and converts the GPU power state to be same scale as CPU idle state for
  -- consistency. (smaller numbers correspond to deeper idle states on Mali, and
  -- larger numbers correspond to deeper idle state on CPUs).
  iif(power_state = 2 AND freq = 0, iif(prev_freq = 0, next_freq, prev_freq), freq) AS freq,
  iif(power_state = 2 AND freq = 0, 1, power_state) AS power_state
FROM base;
