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

INCLUDE PERFETTO MODULE wattson.cpu.estimates;

INCLUDE PERFETTO MODULE wattson.gpu.estimates;

INCLUDE PERFETTO MODULE wattson.utils;

-- Need to use SPAN_OUTER_JOIN because depending on the trace points enabled,
-- it's possible one of the tables is empty
CREATE VIRTUAL TABLE _virtual_system_state_mw USING SPAN_OUTER_JOIN (_cpu_estimates_mw, _gpu_estimates_mw);

-- The most basic components of Wattson, all normalized to be in mW on a per
-- system state basis
CREATE PERFETTO TABLE _system_state_mw AS
SELECT
  *
FROM _virtual_system_state_mw;

-- API to get power from each system state in an arbitrary time window
CREATE PERFETTO FUNCTION _windowed_system_state_mw(
    ts TIMESTAMP,
    dur LONG
)
RETURNS TABLE (
  cpu0_mw DOUBLE,
  cpu1_mw DOUBLE,
  cpu2_mw DOUBLE,
  cpu3_mw DOUBLE,
  cpu4_mw DOUBLE,
  cpu5_mw DOUBLE,
  cpu6_mw DOUBLE,
  cpu7_mw DOUBLE,
  dsu_scu_mw DOUBLE,
  gpu_mw DOUBLE
) AS
SELECT
  sum(ss.cpu0_mw * ss.dur) / sum(ss.dur) AS cpu0_mw,
  sum(ss.cpu1_mw * ss.dur) / sum(ss.dur) AS cpu1_mw,
  sum(ss.cpu2_mw * ss.dur) / sum(ss.dur) AS cpu2_mw,
  sum(ss.cpu3_mw * ss.dur) / sum(ss.dur) AS cpu3_mw,
  sum(ss.cpu4_mw * ss.dur) / sum(ss.dur) AS cpu4_mw,
  sum(ss.cpu5_mw * ss.dur) / sum(ss.dur) AS cpu5_mw,
  sum(ss.cpu6_mw * ss.dur) / sum(ss.dur) AS cpu6_mw,
  sum(ss.cpu7_mw * ss.dur) / sum(ss.dur) AS cpu7_mw,
  sum(ss.dsu_scu_mw * ss.dur) / sum(ss.dur) AS dsu_scu_mw,
  sum(ss.gpu_mw * ss.dur) / sum(ss.dur) AS gpu_mw
FROM _interval_intersect_single!($ts, $dur, _ii_subquery!(_system_state_mw)) AS ii
JOIN _system_state_mw AS ss
  ON ss._auto_id = id;
