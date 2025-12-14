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

INCLUDE PERFETTO MODULE counters.intervals;

INCLUDE PERFETTO MODULE wattson.estimates;

INCLUDE PERFETTO MODULE wattson.utils;

-- After ii, a single column will have the same value split up into different
-- slices. This macro recombines all the slices such that adjacent slices will
-- always have different values. This means less slices to process, and from the
-- UI perspective, the counter track will be displayed cleaner.
CREATE PERFETTO MACRO _get_continuous_estimates(
    rail ColumnName
)
RETURNS TableOrSubquery AS
(
  SELECT
    ts,
    dur,
    value AS $rail
  FROM counter_leading_intervals!((
    SELECT
      ts,
      dur,
      $rail AS value,
      NULL AS id,
      NULL AS track_id
    FROM _system_state_mw
  ))
);

CREATE PERFETTO TABLE _system_state_cpu0_mw AS
SELECT
  *,
  0 AS cpu
FROM _get_continuous_estimates!(cpu0_mw);

CREATE PERFETTO TABLE _system_state_cpu1_mw AS
SELECT
  *,
  1 AS cpu
FROM _get_continuous_estimates!(cpu1_mw);

CREATE PERFETTO TABLE _system_state_cpu2_mw AS
SELECT
  *,
  2 AS cpu
FROM _get_continuous_estimates!(cpu2_mw);

CREATE PERFETTO TABLE _system_state_cpu3_mw AS
SELECT
  *,
  3 AS cpu
FROM _get_continuous_estimates!(cpu3_mw);

CREATE PERFETTO TABLE _system_state_cpu4_mw AS
SELECT
  *,
  4 AS cpu
FROM _get_continuous_estimates!(cpu4_mw);

CREATE PERFETTO TABLE _system_state_cpu5_mw AS
SELECT
  *,
  5 AS cpu
FROM _get_continuous_estimates!(cpu5_mw);

CREATE PERFETTO TABLE _system_state_cpu6_mw AS
SELECT
  *,
  6 AS cpu
FROM _get_continuous_estimates!(cpu6_mw);

CREATE PERFETTO TABLE _system_state_cpu7_mw AS
SELECT
  *,
  7 AS cpu
FROM _get_continuous_estimates!(cpu7_mw);

CREATE PERFETTO TABLE _system_state_dsu_scu_mw AS
SELECT
  *
FROM _get_continuous_estimates!(dsu_scu_mw);

CREATE PERFETTO TABLE _system_state_gpu_mw AS
SELECT
  *
FROM _get_continuous_estimates!(gpu_mw);
