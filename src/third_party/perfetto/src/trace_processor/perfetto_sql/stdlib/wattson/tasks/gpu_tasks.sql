--
-- Copyright 2026 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.gpu.work_period;

-- Extract GPU task slices from the work period track.
CREATE PERFETTO TABLE _gpu_tasks AS
SELECT
  s.ts,
  s.dur,
  t.uid,
  t.gpu_id
FROM slice AS s
JOIN android_gpu_work_period_track AS t
  ON s.track_id = t.id
WHERE
  s.dur > 0;

-- Calculate the number of active GPU tasks at any point in time.
CREATE PERFETTO TABLE _gpu_active_task_count AS
WITH
  events AS (
    SELECT
      ts,
      1 AS delta
    FROM _gpu_tasks
    UNION ALL
    SELECT
      ts + dur AS ts,
      -1 AS delta
    FROM _gpu_tasks
  ),
  running_tasks AS (
    SELECT
      ts,
      sum(delta) OVER (ORDER BY ts) AS active_tasks
    FROM events
  ),
  running_tasks_with_dur AS (
    SELECT
      ts,
      lead(ts) OVER (ORDER BY ts) - ts AS dur,
      active_tasks
    FROM running_tasks
  )
SELECT
  ts,
  dur,
  active_tasks
FROM running_tasks_with_dur
WHERE
  dur > 0;
