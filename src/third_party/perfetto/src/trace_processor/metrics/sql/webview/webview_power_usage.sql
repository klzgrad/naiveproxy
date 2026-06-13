--
-- Copyright 2020 The Android Open Source Project
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

-- WebView is embedded in the hosting app's main process, which means it shares some threads
-- with the host app's work. We approximate WebView-related power usage
-- by selecting user slices that belong to WebView and estimating their power use
-- through the CPU time they consume at different core frequencies.
-- This file populates a summary table that can be used to produce metrics in different formats.

SELECT RUN_METRIC('android/android_proxy_power.sql');
SELECT RUN_METRIC('android/cpu_info.sql');

DROP TABLE IF EXISTS top_level_slice;

-- Select topmost slices from the 'toplevel' and 'Java' categories.
-- Filter out Looper events since they are likely to belong to the host app.
-- Slices are only used to calculate the contribution of the browser process,
-- renderer contribution will be calculated as the sum of all renderer processes' usage.
CREATE PERFETTO TABLE top_level_slice AS
SELECT *
FROM slice WHERE
  depth = 0
  AND ((category GLOB '*toplevel*' OR category = 'Java')
    AND name NOT GLOB '*looper*');

DROP TABLE IF EXISTS webview_browser_slices;

-- Match top-level slices to threads and hosting apps.
-- This excludes any renderer slices because renderer processes are counted
-- as a whole separately.
-- Slices from Chrome browser processes are also excluded.
CREATE PERFETTO TABLE webview_browser_slices AS
SELECT
  top_level_slice.ts,
  top_level_slice.dur,
  thread_track.utid,
  process.upid AS upid,
  extract_arg(process.arg_set_id, 'chrome.host_app_package_name') AS app_name
FROM top_level_slice
JOIN thread_track
  ON top_level_slice.track_id = thread_track.id
JOIN process
  ON thread.upid = process.upid
JOIN thread
  ON thread_track.utid = thread.utid
WHERE process.name NOT GLOB '*SandboxedProcessService*'
  AND process.name NOT GLOB '*chrome*'
  AND app_name IS NOT NULL;

DROP TABLE IF EXISTS webview_browser_slices_power;

-- Assign power usage to WebView browser slices.
CREATE VIRTUAL TABLE webview_browser_slices_power
USING SPAN_JOIN(power_per_thread PARTITIONED utid,
                webview_browser_slices PARTITIONED utid);

DROP TABLE IF EXISTS webview_browser_slices_power_summary;

-- Calculate the power usage of all WebView browser slices for each app
-- in milliampere-seconds.
CREATE PERFETTO TABLE webview_browser_slices_power_summary AS
SELECT
  app_name,
  SUM(dur * COALESCE(power_ma, 0) / 1e9) AS power_mas
FROM webview_browser_slices_power
GROUP BY app_name;

DROP TABLE IF EXISTS webview_renderer_threads;

-- All threads of all WebView renderer processes.
CREATE PERFETTO TABLE webview_renderer_threads AS
SELECT
  thread.utid AS utid,
  extract_arg(process.arg_set_id, 'chrome.host_app_package_name') AS app_name
FROM process
JOIN thread
  ON thread.upid = process.upid
WHERE process.name GLOB '*webview*SandboxedProcessService*'
  AND app_name IS NOT NULL;

DROP TABLE IF EXISTS webview_renderer_power_summary;

-- Calculate the power usage of all WebView renderer processes for each app
-- in milliampere-seconds.
CREATE PERFETTO TABLE webview_renderer_power_summary AS
SELECT
  app_name,
  SUM(dur * COALESCE(power_ma, 0) / 1e9) AS power_mas
FROM power_per_thread
JOIN webview_renderer_threads
  ON power_per_thread.utid = webview_renderer_threads.utid
GROUP BY app_name;

DROP TABLE IF EXISTS webview_renderer_power_per_core_type;

-- Calculate the power usage of all WebView renderer processes for each app
-- in milliampere-seconds grouped by core type.
CREATE PERFETTO TABLE webview_renderer_power_per_core_type AS
SELECT
  app_name,
  core_type_per_cpu.core_type AS core_type,
  SUM(dur * COALESCE(power_ma, 0) / 1e9) AS power_mas
FROM power_per_thread
JOIN webview_renderer_threads
  ON power_per_thread.utid = webview_renderer_threads.utid
JOIN core_type_per_cpu
  ON power_per_thread.cpu = core_type_per_cpu.cpu
GROUP BY app_name, core_type_per_cpu.core_type;

DROP TABLE IF EXISTS host_app_threads;

-- All threads of hosting apps (this is a superset of webview_browser_slices).
-- 1) select all threads that had any WebView browser slices associated with them;
-- 2) get all threads for processes matching threads from 1).
-- For example, only some of app's threads wrote any slices, but we are selecting
-- all threads for this app's process.
-- Excludes all renderer processes and Chrome browser processes.
CREATE PERFETTO TABLE host_app_threads AS
SELECT
  thread.utid AS utid,
  thread.name AS name,
  extract_arg(process.arg_set_id, 'chrome.host_app_package_name') AS app_name
FROM thread
JOIN process ON thread.upid = process.upid
WHERE thread.upid IN
  (SELECT DISTINCT(webview_browser_slices.upid) FROM webview_browser_slices)
  AND process.name NOT GLOB '*SandboxedProcessService*'
  AND process.name NOT GLOB '*chrome*'
  AND app_name IS NOT NULL;

DROP TABLE IF EXISTS host_app_power_summary;

-- Calculate the power usage of all WebView (host app+browser) processes for each app
-- in milliampere-seconds.
CREATE PERFETTO TABLE host_app_power_summary AS
SELECT
  app_name,
  SUM(dur * COALESCE(power_ma, 0) / 1e9) AS power_mas
FROM power_per_thread
JOIN host_app_threads
  ON power_per_thread.utid = host_app_threads.utid
GROUP BY app_name;

DROP TABLE IF EXISTS host_app_power_per_core_type;

-- Calculate the power usage of all WebView (host app+browser) processes for each app
-- in milliampere-seconds grouped by core type.
CREATE PERFETTO TABLE host_app_power_per_core_type AS
SELECT
  app_name,
  core_type_per_cpu.core_type AS core_type,
  SUM(dur * COALESCE(power_ma, 0) / 1e9) AS power_mas
FROM power_per_thread
JOIN host_app_threads
  ON power_per_thread.utid = host_app_threads.utid
JOIN core_type_per_cpu
  ON power_per_thread.cpu = core_type_per_cpu.cpu
GROUP BY app_name, core_type_per_cpu.core_type;

DROP TABLE IF EXISTS webview_only_threads;

-- A subset of the host app threads that are WebView-specific.
CREATE PERFETTO TABLE webview_only_threads AS
SELECT *
FROM host_app_threads
WHERE name GLOB 'Chrome*' OR name GLOB 'CookieMonster*'
  OR name GLOB 'CompositorTileWorker*' OR name GLOB 'ThreadPool*ground*'
  OR NAME GLOB 'ThreadPoolService*' OR name GLOB 'VizCompositorThread*'
  OR name IN ('AudioThread', 'DedicatedWorker thread', 'GpuMemoryThread',
    'JavaBridge', 'LevelDBEnv.IDB', 'MemoryInfra', 'NetworkService', 'VizWebView');

DROP TABLE IF EXISTS webview_only_power_summary;

-- Calculate the power usage of all WebView-specific host app threads
-- (browser + in-process renderers) for each app in milliampere-seconds.
CREATE PERFETTO TABLE webview_only_power_summary AS
SELECT
  app_name,
  SUM(dur * COALESCE(power_ma, 0) / 1e9) AS power_mas
FROM power_per_thread
JOIN webview_only_threads
  ON power_per_thread.utid = webview_only_threads.utid
GROUP BY app_name;

DROP TABLE IF EXISTS webview_only_power_per_core_type;

-- Calculate the power usage of all WebView-specific host app threads
-- for each app in milliampere-seconds grouped by core type.
CREATE PERFETTO TABLE webview_only_power_per_core_type AS
SELECT app_name,
  core_type_per_cpu.core_type AS core_type,
  SUM(dur * COALESCE(power_ma, 0) / 1e9) AS power_mas
FROM power_per_thread
JOIN webview_only_threads
  ON power_per_thread.utid = webview_only_threads.utid
JOIN core_type_per_cpu
  ON power_per_thread.cpu = core_type_per_cpu.cpu
GROUP BY app_name, core_type_per_cpu.core_type;

-- Create views for output.

DROP TABLE IF EXISTS total_app_power_output;

CREATE PERFETTO TABLE total_app_power_output AS
SELECT
  host_app_power_summary.app_name AS app_name,
  host_app_power_summary.power_mas AS total_mas,
  host_app_power_little_cores_mas.power_mas AS little_cores_mas,
  host_app_power_big_cores_mas.power_mas AS big_cores_mas,
  host_app_power_bigger_cores_mas.power_mas AS bigger_cores_mas
FROM host_app_power_summary LEFT JOIN host_app_power_per_core_type AS host_app_power_little_cores_mas
  ON host_app_power_summary.app_name = host_app_power_little_cores_mas.app_name
    AND host_app_power_little_cores_mas.core_type = 'little'
LEFT JOIN host_app_power_per_core_type AS host_app_power_big_cores_mas
  ON host_app_power_summary.app_name = host_app_power_big_cores_mas.app_name
    AND host_app_power_big_cores_mas.core_type = 'big'
LEFT JOIN host_app_power_per_core_type AS host_app_power_bigger_cores_mas
  ON host_app_power_summary.app_name = host_app_power_bigger_cores_mas.app_name
    AND host_app_power_bigger_cores_mas.core_type = 'bigger';

DROP TABLE IF EXISTS webview_renderer_power_output;

CREATE PERFETTO TABLE webview_renderer_power_output AS
SELECT
  webview_renderer_power_summary.app_name AS app_name,
  webview_renderer_power_summary.power_mas AS total_mas,
  webview_renderer_little_power.power_mas AS little_cores_mas,
  webview_renderer_big_power.power_mas AS big_cores_mas,
  webview_renderer_bigger_power.power_mas AS bigger_cores_mas
FROM webview_renderer_power_summary LEFT JOIN webview_renderer_power_per_core_type AS webview_renderer_little_power
  ON webview_renderer_power_summary.app_name = webview_renderer_little_power.app_name
    AND webview_renderer_little_power.core_type = 'little'
LEFT JOIN webview_renderer_power_per_core_type AS webview_renderer_big_power
  ON webview_renderer_power_summary.app_name = webview_renderer_big_power.app_name
    AND webview_renderer_big_power.core_type = 'big'
LEFT JOIN webview_renderer_power_per_core_type AS webview_renderer_bigger_power
  ON webview_renderer_power_summary.app_name = webview_renderer_bigger_power.app_name
    AND webview_renderer_bigger_power.core_type = 'bigger';

DROP TABLE IF EXISTS webview_only_power_output;

CREATE PERFETTO TABLE webview_only_power_output AS
SELECT
  webview_only_power_summary.app_name AS app_name,
  webview_only_power_summary.power_mas AS total_mas,
  webview_only_power_little_cores_mas.power_mas AS little_cores_mas,
  webview_only_power_big_cores_mas.power_mas AS big_cores_mas,
  webview_only_power_bigger_cores_mas.power_mas AS bigger_cores_mas
FROM webview_only_power_summary LEFT JOIN webview_only_power_per_core_type AS webview_only_power_little_cores_mas
  ON webview_only_power_summary.app_name = webview_only_power_little_cores_mas.app_name
    AND webview_only_power_little_cores_mas.core_type = 'little'
LEFT JOIN webview_only_power_per_core_type AS webview_only_power_big_cores_mas
  ON webview_only_power_summary.app_name = webview_only_power_big_cores_mas.app_name
    AND webview_only_power_big_cores_mas.core_type = 'big'
LEFT JOIN webview_only_power_per_core_type AS webview_only_power_bigger_cores_mas
  ON webview_only_power_summary.app_name = webview_only_power_bigger_cores_mas.app_name
    AND webview_only_power_bigger_cores_mas.core_type = 'bigger';

DROP TABLE IF EXISTS total_device_power;

-- Calculate the power usage of the device in milliampere-seconds.
CREATE PERFETTO TABLE total_device_power AS
SELECT SUM(dur * COALESCE(power_ma, 0) / 1e9) AS power_mas
FROM power_per_thread;
