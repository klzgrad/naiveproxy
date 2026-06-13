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
--
-- Get all chrome processes and threads tables set up.
SELECT RUN_METRIC('chrome/chrome_processes.sql');

-- When working on InputLatency events we need to ensure we have all the events
-- from the browser, renderer, and GPU processes. This query isn't quite
-- perfect. In system tracing we could have 3 browser processes all in the
-- background and this would match, but for now its the best we can do (renderer
-- and GPU names on android are quite complicated, but this should filter 99%
-- (citation needed) of what we want.
--
-- See b/151077536 for historical context.
DROP VIEW IF EXISTS sufficient_chrome_processes;
CREATE PERFETTO VIEW sufficient_chrome_processes AS
SELECT
  CASE WHEN (
      SELECT COUNT(*) FROM chrome_process) = 0
    THEN
    FALSE
    ELSE (
      SELECT COUNT(*) >= 3 FROM (
        SELECT name FROM chrome_process
        WHERE
          name GLOB "Browser"
          OR name GLOB "Renderer"
          OR name GLOB "Gpu"
          OR name GLOB 'com.android.chrome*'
          OR name GLOB 'com.chrome.beta*'
          OR name GLOB 'com.chrome.dev*'
          OR name GLOB 'com.chrome.canary*'
          OR name GLOB 'com.google.android.apps.chrome*'
          OR name GLOB 'org.chromium.chrome*'
        GROUP BY name
      )) END AS have_enough_chrome_processes;
