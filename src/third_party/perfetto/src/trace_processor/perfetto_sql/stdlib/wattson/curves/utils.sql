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

INCLUDE PERFETTO MODULE wattson.curves.device_cpu_1d;

INCLUDE PERFETTO MODULE wattson.curves.device_cpu_2d;

INCLUDE PERFETTO MODULE wattson.curves.device_gpu;

INCLUDE PERFETTO MODULE wattson.curves.device_l3;

INCLUDE PERFETTO MODULE wattson.device_infos;

INCLUDE PERFETTO MODULE wattson.utils;

-- 1D LUT
CREATE PERFETTO TABLE _filtered_curves_1d_raw AS
SELECT
  cp.policy,
  freq_khz,
  active,
  idle0,
  idle1,
  static
FROM _device_curves_1d AS dc
JOIN _wattson_device AS device
  ON dc.device = device.name
JOIN _dev_cpu_policy_map AS cp
  ON dc.policy = cp.policy;

CREATE PERFETTO TABLE _filtered_curves_1d AS
SELECT
  policy,
  freq_khz,
  -1 AS idle,
  active AS curve_value,
  static
FROM _filtered_curves_1d_raw
UNION
SELECT
  policy,
  freq_khz,
  0,
  idle0,
  static
FROM _filtered_curves_1d_raw
UNION
SELECT
  policy,
  freq_khz,
  1,
  idle1,
  static
FROM _filtered_curves_1d_raw;

CREATE PERFETTO INDEX freq_1d ON _filtered_curves_1d(policy, freq_khz, idle);

-- 2D LUT; with dependency on another CPU
CREATE PERFETTO TABLE _filtered_curves_2d_raw AS
SELECT
  dc.policy,
  dc.freq_khz,
  dc.dep_policy,
  dc.dep_freq,
  dc.active,
  dc.idle0,
  dc.idle1,
  dc.static
FROM _device_curves_2d AS dc
JOIN _wattson_device AS device
  ON dc.device = device.name;

CREATE PERFETTO TABLE _filtered_curves_2d AS
SELECT
  freq_khz,
  dep_policy,
  dep_freq,
  -1 AS idle,
  static,
  active AS curve_value
FROM _filtered_curves_2d_raw
UNION
SELECT
  freq_khz,
  dep_policy,
  dep_freq,
  0,
  static,
  idle0
FROM _filtered_curves_2d_raw
UNION
SELECT
  freq_khz,
  dep_policy,
  dep_freq,
  1,
  static,
  idle1
FROM _filtered_curves_2d_raw;

CREATE PERFETTO INDEX freq_2d ON _filtered_curves_2d(freq_khz, dep_policy, dep_freq, idle);

-- L3 cache LUT
CREATE PERFETTO TABLE _filtered_curves_l3 AS
SELECT
  dc.freq_khz,
  dc.dep_policy,
  dc.dep_freq,
  dc.l3_hit,
  dc.l3_miss
FROM _device_curves_l3 AS dc
JOIN _wattson_device AS device
  ON dc.device = device.name;

CREATE PERFETTO INDEX freq_l3 ON _filtered_curves_l3(freq_khz, dep_policy, dep_freq);

-- Device specific GPU curves
CREATE PERFETTO TABLE _gpu_filtered_curves_raw AS
SELECT
  freq_khz,
  active,
  idle1,
  idle2
FROM _gpu_device_curves AS dc
JOIN _wattson_device AS device
  ON dc.device = device.name;

CREATE PERFETTO TABLE _gpu_filtered_curves AS
SELECT
  freq_khz,
  2 AS idle,
  active AS curve_value
FROM _gpu_filtered_curves_raw
UNION ALL
SELECT
  freq_khz,
  1 AS idle,
  idle1 AS curve_value
FROM _gpu_filtered_curves_raw
UNION ALL
SELECT
  freq_khz,
  0 AS idle,
  idle2 AS curve_value
FROM _gpu_filtered_curves_raw;

CREATE PERFETTO INDEX gpu_freq ON _gpu_filtered_curves(freq_khz, idle);

-- Constructs table specifying CPUs that are DSU dependent
CREATE PERFETTO TABLE _cpu_w_dsu_dependency AS
SELECT DISTINCT
  cpu
FROM _filtered_curves_2d_raw
JOIN _dev_cpu_policy_map
  USING (policy)
WHERE
  dep_policy = _dsu_dep!();

-- Chooses the minimum vote for CPUs with dependencies
CREATE PERFETTO TABLE _cpu_w_dependency_default_vote AS
WITH
  policy_vote AS (
    SELECT
      policy,
      dep_policy,
      min(dep_freq) AS dep_freq
    FROM _filtered_curves_2d_raw
    GROUP BY
      policy
  )
SELECT
  cpu,
  dep_policy,
  dep_freq
FROM policy_vote
JOIN _dev_cpu_policy_map
  USING (policy);

-- CPUs that need to be checked for static calculation
CREATE PERFETTO TABLE _cpus_for_static AS
SELECT DISTINCT
  m.cpu
FROM _filtered_curves_2d_raw AS c
JOIN _dev_cpu_policy_map AS m
  USING (policy)
WHERE
  static > 0
UNION
SELECT DISTINCT
  m.cpu
FROM _filtered_curves_1d AS c
JOIN _dev_cpu_policy_map AS m
  USING (policy)
WHERE
  static > 0;

-- Contructs table specifying CPU dependency of each CPU (if applicable)
CREATE PERFETTO TABLE _cpu_lut_dependencies AS
WITH
  base_cpus AS (
    SELECT DISTINCT
      m.cpu,
      m.policy
    FROM _filtered_curves_2d_raw AS c
    JOIN _dev_cpu_policy_map AS m
      USING (policy)
    WHERE
      dep_policy != _dsu_dep!()
  ),
  dep_cpus AS (
    SELECT DISTINCT
      m.cpu AS dep_cpu,
      m.policy AS dep_policy
    FROM _filtered_curves_2d_raw AS c
    JOIN _dev_cpu_policy_map AS m
      ON c.dep_policy = m.policy
  )
SELECT
  b.cpu,
  d.dep_cpu
FROM base_cpus AS b
CROSS JOIN dep_cpus AS d
WHERE
  b.policy != d.dep_policy;
