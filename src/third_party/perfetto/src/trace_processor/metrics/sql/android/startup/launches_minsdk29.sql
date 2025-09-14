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

-- Marks the beginning of the trace and is equivalent to when the statsd launch
-- logging begins.
DROP VIEW IF EXISTS activity_intent_received;
CREATE PERFETTO VIEW activity_intent_received AS
SELECT ts FROM slice
WHERE name = 'MetricsLogger:launchObserverNotifyIntentStarted';

-- We partition the trace into spans based on posted activity intents.
-- We will refine these progressively in the next steps to only encompass
-- activity starts.
DROP TABLE IF EXISTS activity_intent_recv_spans;
CREATE TABLE activity_intent_recv_spans(id INT, ts BIGINT, dur BIGINT);

INSERT INTO activity_intent_recv_spans
SELECT
  ROW_NUMBER()
  OVER(ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING) AS id,
  ts,
  LEAD(ts, 1, trace_end()) OVER(ORDER BY ts) - ts AS dur
FROM activity_intent_received
ORDER BY ts;

-- Filter activity_intent_recv_spans, keeping only the ones that triggered
-- a launch.
DROP VIEW IF EXISTS launch_partitions;
CREATE PERFETTO VIEW launch_partitions AS
SELECT * FROM activity_intent_recv_spans AS spans
WHERE 1 = (
  SELECT COUNT(1)
  FROM launching_events
  WHERE launching_events.ts BETWEEN spans.ts AND spans.ts + spans.dur);

-- Successful activity launch. The end of the 'launching' event is not related
-- to whether it actually succeeded or not.
DROP VIEW IF EXISTS activity_intent_launch_successful;
CREATE PERFETTO VIEW activity_intent_launch_successful AS
SELECT ts FROM slice
WHERE name = 'MetricsLogger:launchObserverNotifyActivityLaunchFinished';

-- Use the starting event package name. The finish event package name
-- is not reliable in the case of failed launches.
INSERT INTO launches(id, ts, ts_end, dur, package, launch_type)
SELECT
  lpart.id AS id,
  lpart.ts AS ts,
  launching_events.ts_end AS ts_end,
  launching_events.ts_end - lpart.ts AS dur,
  package_name AS package,
  NULL AS launch_type
FROM launch_partitions AS lpart
JOIN launching_events ON
  (launching_events.ts BETWEEN lpart.ts AND lpart.ts + lpart.dur)
  AND (launching_events.ts_end BETWEEN lpart.ts AND lpart.ts + lpart.dur)
WHERE (
  SELECT COUNT(1)
  FROM activity_intent_launch_successful AS successful
  WHERE successful.ts BETWEEN lpart.ts AND lpart.ts + lpart.dur
) > 0;
