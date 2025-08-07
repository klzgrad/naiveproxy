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

--------------------------------------------------------------------------------
-- Collect page-reported events for each renderer. Note that we don't need to
-- match up process ids, because the unique nav_id ensures we're only comparing
-- corresponding events.

DROP VIEW IF EXISTS page_reported_events;
CREATE PERFETTO VIEW page_reported_events AS
SELECT ts, name, EXTRACT_ARG(arg_set_id, "debug.data.navigationId") AS nav_id
FROM slice
WHERE category = 'blink.user_timing'
  AND (name = 'navigationStart' OR name GLOB 'telemetry:reported_by_page:*')
ORDER BY nav_id, ts ASC;

--------------------------------------------------------------------------------
-- Compute the duration from the corresponding navigation start for each
-- reported event.

DROP VIEW IF EXISTS page_reported_durations;
CREATE PERFETTO VIEW page_reported_durations AS
SELECT p.name, (p.ts - (
    SELECT MAX(ts) FROM page_reported_events
    WHERE
      nav_id = p.nav_id
      AND ts < p.ts AND (
        -- Viewable/interactive markers measure time from nav start.
        (p.name GLOB 'telemetry:reported_by_page:*'
         AND p.name NOT GLOB 'telemetry:reported_by_page:benchmark*'
         AND name = 'navigationStart')
        -- Benchmark end markers measure time from the most recent begin marker.
        OR (p.name = 'telemetry:reported_by_page:benchmark_end'
            AND name = 'telemetry:reported_by_page:benchmark_begin')
      ))
) / 1e6 AS dur_ms
FROM page_reported_events p;

--------------------------------------------------------------------------------
-- Combine results into the output table.

DROP VIEW IF EXISTS reported_by_page_output;
CREATE PERFETTO VIEW reported_by_page_output AS
SELECT ReportedByPage(
  'time_to_viewable', (
    SELECT RepeatedField(dur_ms) FROM page_reported_durations
    WHERE name = 'telemetry:reported_by_page:viewable'),
  'time_to_interactive', (
    SELECT RepeatedField(dur_ms) FROM page_reported_durations
    WHERE name = 'telemetry:reported_by_page:interactive'),
  'benchmark_time', (
    SELECT RepeatedField(dur_ms) FROM page_reported_durations
    WHERE name = 'telemetry:reported_by_page:benchmark_end')
);
