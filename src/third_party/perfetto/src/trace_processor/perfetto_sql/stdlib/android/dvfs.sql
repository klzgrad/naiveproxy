--
-- Copyright 2023 The Android Open Source Project
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

-- Dvfs counter with duration.
CREATE PERFETTO VIEW android_dvfs_counters (
  -- Counter name.
  name STRING,
  -- Timestamp when counter value changed.
  ts TIMESTAMP,
  -- Counter value.
  value DOUBLE,
  -- Counter duration.
  dur DURATION
) AS
SELECT
  counter_track.name,
  counter.ts,
  counter.value,
  lead(counter.ts, 1, trace_end()) OVER (PARTITION BY counter_track.id ORDER BY counter.ts) - counter.ts AS dur
FROM counter
JOIN counter_track
  ON counter.track_id = counter_track.id
WHERE
  counter_track.name IN ('domain@0 Frequency', 'domain@1 Frequency', 'domain@2 Frequency', '17000010.devfreq_mif Frequency', '17000020.devfreq_int Frequency', '17000090.devfreq_dsu Frequency', '170000a0.devfreq_bci Frequency', 'dsu_throughput Frequency', 'bus_throughput Frequency', 'cpu0dsu Frequency', 'cpu1dsu Frequency', 'cpu2dsu Frequency', 'cpu3dsu Frequency', 'cpu4dsu Frequency', 'cpu5dsu Frequency', 'cpu6dsu Frequency', 'cpu7dsu Frequency', 'cpu8dsu Frequency', 'gs_memlat_devfreq:devfreq_mif_cpu0_memlat@17000010 Frequency', 'gs_memlat_devfreq:devfreq_mif_cpu1_memlat@17000010 Frequency', 'gs_memlat_devfreq:devfreq_mif_cpu2_memlat@17000010 Frequency', 'gs_memlat_devfreq:devfreq_mif_cpu3_memlat@17000010 Frequency', 'gs_memlat_devfreq:devfreq_mif_cpu4_memlat@17000010 Frequency', 'gs_memlat_devfreq:devfreq_mif_cpu5_memlat@17000010 Frequency', 'gs_memlat_devfreq:devfreq_mif_cpu6_memlat@17000010 Frequency', 'gs_memlat_devfreq:devfreq_mif_cpu7_memlat@17000010 Frequency', 'gs_memlat_devfreq:devfreq_mif_cpu8_memlat@17000010 Frequency')
ORDER BY
  ts;

-- Aggregates dvfs counter slice for statistic.
CREATE PERFETTO TABLE android_dvfs_counter_stats (
  -- Counter name on which all the other values are aggregated on.
  name STRING,
  -- Max of all counter values for the counter name.
  max DOUBLE,
  -- Min of all counter values for the counter name.
  min DOUBLE,
  -- Duration between the first and last counter value for the counter name.
  dur DURATION,
  -- Weighted avergate of all the counter values for the counter name.
  wgt_avg DOUBLE
) AS
SELECT
  name,
  max(value) AS max,
  min(value) AS min,
  (
    max(ts) - min(ts)
  ) AS dur,
  (
    sum(dur * value) / sum(dur)
  ) AS wgt_avg
FROM android_dvfs_counters
WHERE
  android_dvfs_counters.dur > 0
GROUP BY
  name;

-- Aggregates dvfs counter slice for residency
CREATE PERFETTO VIEW android_dvfs_counter_residency (
  -- Counter name.
  name STRING,
  -- Counter value.
  value DOUBLE,
  -- Counter duration.
  dur DURATION,
  -- Counter duration as a percentage of total duration.
  pct DOUBLE
) AS
WITH
  total AS (
    SELECT
      name,
      sum(dur) AS dur
    FROM android_dvfs_counters
    WHERE
      dur > 0
    GROUP BY
      name
  )
SELECT
  android_dvfs_counters.name,
  android_dvfs_counters.value,
  sum(android_dvfs_counters.dur) AS dur,
  (
    sum(android_dvfs_counters.dur) * 100.0 / total.dur
  ) AS pct
FROM android_dvfs_counters
JOIN total
  USING (name)
WHERE
  android_dvfs_counters.dur > 0
GROUP BY
  1,
  2;
