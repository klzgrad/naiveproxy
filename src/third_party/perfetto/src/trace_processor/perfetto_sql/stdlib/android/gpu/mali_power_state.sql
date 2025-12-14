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
--

INCLUDE PERFETTO MODULE counters.intervals;

-- GPU power state which is analogous to CPU idle state
CREATE PERFETTO TABLE android_mali_gpu_power_state (
  -- Timestamp
  ts TIMESTAMP,
  -- Duration
  dur DURATION,
  -- GPU power state
  power_state LONG
) AS
SELECT
  ts,
  dur,
  cast_int!(value) AS power_state
FROM counter_leading_intervals!((
    SELECT c.*
    FROM counter c
    JOIN counter_track t ON t.id = c.track_id
      AND t.name = 'mali_gpu_power_state'
)) AS cli
JOIN counter_track AS t
  ON t.id = cli.track_id;
