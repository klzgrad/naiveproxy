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

-- Counter information for each frequency change for each CPU. Finds each time
-- region where a CPU frequency is constant.
CREATE PERFETTO TABLE cpu_frequency_counters (
  -- Counter id.
  id LONG,
  -- Joinable with 'counter_track.id'.
  track_id JOINID(track.id),
  -- Starting timestamp of the counter
  ts TIMESTAMP,
  -- Duration in which counter is constant and frequency doesn't change.
  dur DURATION,
  -- Frequency in kHz of the CPU that corresponds to this counter. NULL if not
  -- found or undefined.
  freq LONG,
  -- Unique CPU id.
  ucpu LONG,
  -- CPU that corresponds to this counter.
  cpu LONG
) AS
SELECT
  count_w_dur.id,
  count_w_dur.track_id,
  count_w_dur.ts,
  count_w_dur.dur,
  cast_int!(count_w_dur.value) AS freq,
  cpu.ucpu,
  cct.cpu
FROM counter_leading_intervals!((
  SELECT c.*
  FROM counter c
  JOIN cpu_counter_track cct
  ON cct.id = c.track_id AND cct.name = 'cpufreq'
)) AS count_w_dur
JOIN cpu_counter_track AS cct
  ON track_id = cct.id
JOIN cpu
  ON cct.machine_id IS cpu.machine_id AND cct.cpu = cpu.cpu;
