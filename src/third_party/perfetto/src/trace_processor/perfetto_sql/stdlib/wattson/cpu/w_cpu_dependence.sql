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

INCLUDE PERFETTO MODULE wattson.cpu.split;

-- Find the CPU states creating the max vote
CREATE PERFETTO TABLE _w_cpu_dependence AS
WITH
  max_power_tbl AS (
    SELECT
      *,
      -- Indicates if all CPUs are in deep idle
      min(
        no_static,
        coalesce(idle_4, 1),
        coalesce(idle_5, 1),
        coalesce(idle_6, 1),
        coalesce(idle_7, 1)
      ) AS all_cpu_deep_idle,
      -- Determines which CPU has highest vote
      max(static_4, static_5, static_6, static_7) AS max_static_vote
    FROM _w_independent_cpus_calc, _skip_devfreq_for_calc
  )
SELECT
  ts,
  dur,
  freq_0,
  idle_0,
  freq_1,
  idle_1,
  freq_2,
  idle_2,
  freq_3,
  idle_3,
  cpu0_curve,
  cpu1_curve,
  cpu2_curve,
  cpu3_curve,
  cpu4_curve,
  cpu5_curve,
  cpu6_curve,
  cpu7_curve,
  l3_hit_count,
  l3_miss_count,
  no_static,
  all_cpu_deep_idle,
  CASE max_static_vote
    WHEN -1
    THEN _get_min_freq_vote()
    WHEN static_4
    THEN freq_4
    WHEN static_5
    THEN freq_5
    WHEN static_6
    THEN freq_6
    WHEN static_7
    THEN freq_7
    ELSE 400000
  END AS dependent_freq,
  CASE max_static_vote
    WHEN -1
    THEN _get_min_policy_vote()
    WHEN static_4
    THEN policy_4
    WHEN static_5
    THEN policy_5
    WHEN static_6
    THEN policy_6
    WHEN static_7
    THEN policy_7
    ELSE 4
  END AS dependent_policy
FROM max_power_tbl;
