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

INCLUDE PERFETTO MODULE wattson.cpu.pivot;

INCLUDE PERFETTO MODULE wattson.curves.utils;

-- Find the CPU states creating the max vote
CREATE PERFETTO TABLE _w_cpu_dependence AS
WITH
  dependency_calculate AS (
    SELECT
      *,
      max(
        iif(idle_2 = -1 AND 2 IN _cpus_with_no_dependency, coalesce(cpu2_curve, 0), 0),
        iif(idle_3 = -1 AND 3 IN _cpus_with_no_dependency, coalesce(cpu3_curve, 0), 0),
        iif(idle_4 = -1 AND 4 IN _cpus_with_no_dependency, coalesce(cpu4_curve, 0), 0),
        iif(idle_5 = -1 AND 5 IN _cpus_with_no_dependency, coalesce(cpu5_curve, 0), 0),
        iif(idle_6 = -1 AND 6 IN _cpus_with_no_dependency, coalesce(cpu6_curve, 0), 0),
        iif(idle_7 = -1 AND 7 IN _cpus_with_no_dependency, coalesce(cpu7_curve, 0), 0)
      ) AS dependency_max
    FROM _w_independent_cpus_calc, _skip_devfreq_for_calc
  )
SELECT
  d.ts,
  d.dur,
  d.freq_0,
  d.idle_0,
  d.freq_1,
  d.idle_1,
  d.freq_2,
  d.idle_2,
  d.freq_3,
  d.idle_3,
  d.cpu0_curve,
  d.cpu1_curve,
  d.cpu2_curve,
  d.cpu3_curve,
  d.cpu4_curve,
  d.cpu5_curve,
  d.cpu6_curve,
  d.cpu7_curve,
  d.l3_hit_count,
  d.l3_miss_count,
  d.no_static,
  d.all_cpu_deep_idle,
  -- If dependency_max is 0, dependent CPUs are not active, so use the minimum
  -- freq/voltage vote
  iif(d.dependency_max = 0, m.min_dependency, d.dependency_max) AS dependency
FROM dependency_calculate AS d, _min_active_curve_value_for_dependency AS m;
