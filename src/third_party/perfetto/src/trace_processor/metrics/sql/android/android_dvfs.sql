--
-- Copyright 2021 The Android Open Source Project
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

DROP VIEW IF EXISTS freq_slice;

CREATE PERFETTO VIEW freq_slice AS
SELECT
  counter.track_id AS track_id,
  track.name AS freq_name,
  ts,
  value AS freq_value,
  LEAD(ts, 1, trace_end()) OVER (PARTITION BY track.id ORDER BY ts) - ts AS duration
FROM counter
LEFT JOIN track ON counter.track_id = track.id
WHERE track.name GLOB "* Frequency"
ORDER BY ts;

DROP VIEW IF EXISTS freq_total_duration;

CREATE PERFETTO VIEW freq_total_duration AS
SELECT
  track_id,
  freq_name,
  SUM(duration) AS total_duration
FROM freq_slice
WHERE duration > 0
GROUP BY track_id, freq_name;

DROP VIEW IF EXISTS dvfs_per_band_view;

CREATE PERFETTO VIEW dvfs_per_band_view AS
WITH
freq_duration AS (
  SELECT
    track_id,
    freq_name,
    CAST(freq_value AS int) AS freq_value,
    SUM(duration) AS duration_ns
  FROM freq_slice
  WHERE duration > 0
  GROUP BY track_id, freq_name, freq_value
)
SELECT
  freq_duration.track_id,
  freq_duration.freq_name,
  AndroidDvfsMetric_BandStat(
    'freq_value', freq_value,
    'percentage', duration_ns / (total_duration / 1e2),
    'duration_ns', duration_ns
  ) AS proto
FROM freq_duration
LEFT JOIN freq_total_duration
  USING(track_id)
ORDER BY freq_duration.freq_name, freq_duration.freq_value;

DROP VIEW IF EXISTS dvfs_per_freq_view;
CREATE PERFETTO VIEW dvfs_per_freq_view AS
SELECT
  AndroidDvfsMetric_FrequencyResidency(
    'freq_name', freq_total_duration.freq_name,
    'band_stat', (
      SELECT
        RepeatedField(proto)
      FROM dvfs_per_band_view
      WHERE dvfs_per_band_view.track_id = freq_total_duration.track_id
    )
  ) AS proto
FROM freq_total_duration
GROUP BY track_id, freq_name
ORDER BY freq_name;

DROP VIEW IF EXISTS android_dvfs_output;
CREATE PERFETTO VIEW android_dvfs_output AS
SELECT AndroidDVFSMetric(
    'freq_residencies', (
      SELECT
        RepeatedField(proto)
      FROM dvfs_per_freq_view
    )
  );
