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

INCLUDE PERFETTO MODULE linux.cpu.frequency;

INCLUDE PERFETTO MODULE wattson.device_infos;

CREATE PERFETTO TABLE _adjusted_cpu_freq AS
WITH
  _cpu_freq AS (
    SELECT
      ts,
      dur,
      freq,
      cf.ucpu AS cpu,
      d_map.policy
    FROM cpu_frequency_counters AS cf
    JOIN _dev_cpu_policy_map AS d_map
      ON cf.ucpu = d_map.cpu
  ),
  -- Get first freq transition per CPU
  first_cpu_freq_slices AS (
    SELECT
      min(ts) AS ts,
      cpu
    FROM _cpu_freq
    GROUP BY
      cpu
  )
-- Prepend NULL slices up to first freq events on a per CPU basis
SELECT
  -- Construct slices from first cpu ts up to first freq event for each cpu
  trace_start() AS ts,
  first_slices.ts - trace_start() AS dur,
  NULL AS freq,
  first_slices.cpu,
  d_map.policy
FROM first_cpu_freq_slices AS first_slices
JOIN _dev_cpu_policy_map AS d_map
  ON first_slices.cpu = d_map.cpu
UNION ALL
SELECT
  ts,
  dur,
  freq,
  cpu,
  policy
FROM _cpu_freq
UNION ALL
-- Add empty cpu freq counters for CPUs that are physically present, but did not
-- have a single freq event register. The time region needs to be defined so
-- that interval_intersect doesn't remove the undefined time region.
SELECT
  trace_start() AS ts,
  trace_dur() AS dur,
  NULL AS freq,
  cpu,
  NULL AS policy
FROM _dev_cpu_policy_map
WHERE
  NOT cpu IN (
    SELECT
      cpu
    FROM first_cpu_freq_slices
  );
