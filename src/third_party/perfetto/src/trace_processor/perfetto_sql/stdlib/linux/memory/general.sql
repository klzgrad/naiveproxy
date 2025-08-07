--
-- Copyright 2024 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the 'License');
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an 'AS IS' BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

CREATE PERFETTO VIEW _all_counters_per_process AS
SELECT
  ts,
  lead(
    ts,
    1,
    (
      SELECT
        coalesce(end_ts, trace_end())
      FROM process AS p
      WHERE
        p.upid = t.upid
    ) + 1
  ) OVER (PARTITION BY track_id ORDER BY ts) - ts AS dur,
  upid,
  value,
  track_id,
  name
FROM counter AS c
JOIN process_counter_track AS t
  ON t.id = c.track_id
WHERE
  upid IS NOT NULL;
