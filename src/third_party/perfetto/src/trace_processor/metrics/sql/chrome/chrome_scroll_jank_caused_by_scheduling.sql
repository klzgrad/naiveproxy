--
-- Copyright 2022 The Android Open Source Project
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

SELECT RUN_METRIC('chrome/chrome_input_to_browser_intervals.sql');

-- Script params:
-- {{dur_causes_jank_ms}} : The duration of a task barrage on the Chrome
-- main thread that will delay input causing jank.

-- Filter intervals to only durations longer than {{dur_causes_jank_ms}}.
DROP VIEW IF EXISTS chrome_input_to_browser_longer_intervals;
CREATE PERFETTO VIEW chrome_input_to_browser_longer_intervals AS
SELECT
  *
FROM chrome_input_to_browser_intervals
WHERE
  (window_end_ts - window_start_ts) >= {{dur_causes_jank_ms}} * 1e6;

-- Assign tasks to each delay interval that we could have started
-- processing input but didn't on the main thread, and sum those
-- tasks.
-- We filter java out here as we're interested in tasks that delayed
-- yielding to java native work, and we filter tasks that are more
-- than 8ms here as those are handled separately and are not regarded
-- as scheduling issues.
DROP VIEW IF EXISTS chrome_task_barrages_per_interval;
CREATE PERFETTO VIEW chrome_task_barrages_per_interval AS
SELECT
  GROUP_CONCAT(DISTINCT full_name) AS full_name,
  SUM(dur / 1e6) AS total_duration_ms,
  SUM(thread_dur / 1e6) AS total_thread_duration_ms,
  MIN(id) AS first_id_per_task_barrage,
  MAX(id) AS last_id_per_task_barrage,
  COUNT(*) AS count,
  window_start_id,
  window_start_ts,
  window_end_id,
  window_end_ts,
  scroll_type
FROM
  (
    SELECT * FROM (
      SELECT
        chrome_tasks.full_name AS full_name,
        chrome_tasks.dur AS dur,
        chrome_tasks.thread_dur AS thread_dur,
        chrome_tasks.ts AS ts,
        chrome_tasks.id,
        chrome_tasks.upid
      FROM
        chrome_tasks
      WHERE
        chrome_tasks.thread_name = "CrBrowserMain"
        AND task_type != "java"
        AND task_type != "choreographer"
      ORDER BY chrome_tasks.ts
    ) tasks
    JOIN chrome_input_to_browser_longer_intervals
      ON (tasks.ts + tasks.dur)
        > chrome_input_to_browser_longer_intervals.window_start_ts
        AND (tasks.ts + tasks.dur)
        < chrome_input_to_browser_longer_intervals.window_end_ts
        AND tasks.ts > chrome_input_to_browser_longer_intervals.window_start_ts
        AND tasks.ts < chrome_input_to_browser_longer_intervals.window_end_ts
        -- For cases when there are multiple chrome instances.
        AND tasks.upid = chrome_input_to_browser_longer_intervals.upid
    ORDER BY window_start_ts, window_end_ts
  )
GROUP BY window_start_ts, window_end_ts;

-- Filter to task barrages that took more than 8ms, as barrages
-- that lasted less than that are unlikely to have caused jank.
DROP VIEW IF EXISTS chrome_scroll_jank_caused_by_scheduling;
CREATE PERFETTO VIEW chrome_scroll_jank_caused_by_scheduling AS
SELECT *
FROM chrome_task_barrages_per_interval
WHERE total_duration_ms > {{dur_causes_jank_ms}} AND count > 1
ORDER BY total_duration_ms DESC;
