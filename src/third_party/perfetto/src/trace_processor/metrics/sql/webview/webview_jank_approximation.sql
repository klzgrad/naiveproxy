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

-- We approximate WebView-related app janks by selecting WebView renderer
-- slices and overlapping them with app jank slices for known apps.

-- Select all WebView processes
DROP VIEW IF EXISTS webview_processes;
CREATE PERFETTO VIEW webview_processes AS
SELECT * FROM process
WHERE name IN ('com.google.android.gm',
  'com.google.android.googlequicksearchbox',
  'com.google.android.apps.searchlite',
  'com.google.android.apps.magazines');

-- Select all system processes
DROP VIEW IF EXISTS system_processes;
CREATE PERFETTO VIEW system_processes AS
SELECT * FROM process
WHERE name IN ('com.android.systemui',
  '/system/bin/surfaceflinger',
  'system_server');

-- Select all slices related to startup
DROP TABLE IF EXISTS webview_browser_startup_slices;
CREATE PERFETTO TABLE webview_browser_startup_slices AS
SELECT slice.id AS browser_startup_id, slice.ts, slice.dur
FROM slice
WHERE name = 'WebViewChromium.init';

-- Select all scheduler slices from WebView renderer processes
DROP TABLE IF EXISTS webview_renderer_slices;
CREATE PERFETTO TABLE webview_renderer_slices AS
SELECT sched_slice.id as renderer_id, sched_slice.ts, sched_slice.dur
FROM sched_slice
JOIN thread
USING(utid)
JOIN process
USING (upid)
WHERE process.name GLOB '*webview*Sand*';

-- Select all jank slices
DROP TABLE IF EXISTS all_self_jank_slices;
CREATE PERFETTO TABLE all_self_jank_slices AS
SELECT *
FROM actual_frame_timeline_slice
WHERE jank_type NOT IN ('None', 'Buffer Stuffing')
  AND jank_tag = 'Self Jank';

-- Select all jank slices from WebView processes
-- @column id Id of jank slice from a WebView process
-- @column ts Timestamp of the start of jank slice in a WebView process (in nanoseconds)
-- @column dur Duration of jank slice in a WebView process (in nanoseconds)
DROP VIEW IF EXISTS webview_app_jank_slices;
CREATE PERFETTO VIEW webview_app_jank_slices AS
SELECT * FROM all_self_jank_slices
WHERE upid IN (SELECT upid FROM webview_processes);

-- Select all jank slices from all processes except system processes
-- @column id Id of jank slice from all processes except system processes
-- @column ts Timestamp of the start of jank slice from all processes except system processes (in nanoseconds)
-- @column dur Duration of the jank slice from all processes except system processes (in nanoseconds)
DROP VIEW IF EXISTS webview_all_app_jank_slices;
CREATE PERFETTO VIEW webview_all_app_jank_slices AS
SELECT * FROM all_self_jank_slices
WHERE upid NOT IN (SELECT upid FROM system_processes);

-- Select jank slices from WebView processes overlapping WebView renderer
-- scheduler slices
-- @column id Id of jank slice from WebView processes overlapping WebView renderer
-- @column ts Timestamp of the start of jank slice from WebView processes overlapping WebView renderer (in nanoseconds)
-- @column dur Duration of jank slice from WebView processes overlapping WebView renderer (in nanoseconds)
DROP TABLE IF EXISTS webview_jank_slices;
CREATE VIRTUAL TABLE webview_jank_slices
USING SPAN_JOIN(webview_renderer_slices,
                webview_app_jank_slices);

-- Select jank slices overlapping WebView startup slices
-- @column id Id of jank slice overlapping WebView startup slices
-- @column ts Timestamp of the start of jank slice overlapping WebView startup slices (in nanoseconds)
-- @column dur Duration of jank slice overlapping WebView startup slices (in nanoseconds)
DROP TABLE IF EXISTS webview_browser_startup_jank_slices;
CREATE VIRTUAL TABLE webview_browser_startup_jank_slices
USING SPAN_JOIN(webview_browser_startup_slices,
               webview_app_jank_slices);

-- Select jank slices from all processes except system processes overlapping
-- WebView renderer scheduler slices
-- @column id Id of jank slice from all processes except system processes overlapping WebView renderer scheduler slices
-- @column ts Timestamp of the start of jank slice from all processes except system processes overlapping WebView renderer scheduler slices (in nanoseconds)
-- @column dur Duration of jank slice from all processes except system processes overlapping WebView renderer scheduler slices (in nanoseconds)
DROP TABLE IF EXISTS webview_total_jank_slices;
CREATE VIRTUAL TABLE webview_total_jank_slices
USING SPAN_JOIN(webview_renderer_slices,
                webview_all_app_jank_slices);

-- Select jank slices from WebView processes overlapping WebView renderer
-- scheduler slices excluding WebView startup slices
-- @column id Id of jank slice from WebView processes overlapping WebView renderer scheduler slices excluding WebView startup slices
-- @column ts Timestamp of the start of jank slice from WebView processes overlapping WebView renderer scheduler slices excluding WebView startup slices (in nanoseconds)
-- @column dur Duration of jank slice from WebView processes overlapping WebView renderer scheduler slices excluding WebView startup slices (in nanoseconds)
DROP VIEW IF EXISTS webview_janks_slices_without_startup;
CREATE PERFETTO VIEW webview_janks_slices_without_startup AS
SELECT * FROM webview_jank_slices
WHERE id NOT IN (SELECT id FROM webview_browser_startup_jank_slices);

-- Summary for all types of janks
-- @column webview_janks janks in WebView apps that overlap with WebView renderer
-- @column webview_janks_without_startup same as above but excluding startup
-- @column webview_app_janks janks in WebView apps
-- @column webview_total_janks janks in all apps (except system) that overlap with WebView renderer
-- @column total_janks janks in all apps (except system)
DROP VIEW IF EXISTS webview_jank_approximation_summary;
CREATE PERFETTO VIEW webview_jank_approximation_summary AS
WITH wvj AS (SELECT COUNT(DISTINCT(id)) AS webview_janks
  FROM webview_jank_slices),
wvjwos AS (SELECT COUNT(DISTINCT(id))
  AS webview_janks_without_startup FROM webview_janks_slices_without_startup),
wvaj AS (SELECT COUNT(DISTINCT(id))
  AS webview_app_janks FROM webview_app_jank_slices),
wvtj AS (SELECT COUNT(DISTINCT(id)) AS webview_total_janks
  FROM webview_total_jank_slices),
tj AS (SELECT COUNT(DISTINCT(id))
  AS total_janks FROM webview_all_app_jank_slices)
SELECT *
from wvj, wvjwos, wvaj, wvtj, tj;

DROP VIEW IF EXISTS webview_jank_approximation_output;
CREATE PERFETTO VIEW webview_jank_approximation_output AS
SELECT WebViewJankApproximation(
  'webview_janks', (SELECT webview_janks FROM webview_jank_approximation_summary),
  'webview_janks_without_startup', (SELECT webview_janks_without_startup FROM webview_jank_approximation_summary),
  'webview_app_janks', (SELECT webview_app_janks FROM webview_jank_approximation_summary),
  'webview_total_janks', (SELECT webview_total_janks FROM webview_jank_approximation_summary),
  'total_janks', (SELECT total_janks FROM webview_jank_approximation_summary)
);