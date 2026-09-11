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
--

SELECT RUN_METRIC('chrome/chrome_processes.sql');

-- Computes the duration of the browser main thread duration in ms.
DROP VIEW IF EXISTS chrome_browser_main_thread_duration;
CREATE PERFETTO VIEW chrome_browser_main_thread_duration AS
SELECT MAX(len) AS duration_ms
FROM (
  SELECT (MAX(ts) - MIN(ts)) / 1000 / 1000 AS len
  FROM slice s
  JOIN thread_track tt ON s.track_id = tt.id
  JOIN chrome_thread t ON t.utid = tt.utid
  JOIN chrome_process p ON t.upid = p.upid
  JOIN track tr ON tt.id = tr.id
  JOIN args a ON tr.source_arg_set_id = a.arg_set_id
  WHERE (t.canonical_name = "CrProcessMain" OR t.canonical_name = "CrBrowserMain")
    AND p.process_type = "Browser"
    AND a.key = "is_root_in_scope"
    AND a.int_value = 1
  GROUP BY s.track_id
);
