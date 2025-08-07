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
--

DROP VIEW IF EXISTS launch_async_events;
CREATE PERFETTO VIEW launch_async_events AS
SELECT
  ts,
  dur,
  SUBSTR(name, 19) AS id
FROM slice
WHERE
  name GLOB 'launchingActivity#*'
  AND dur != 0
  AND INSTR(name, ':') = 0;

DROP VIEW IF EXISTS launch_complete_events;
CREATE PERFETTO VIEW launch_complete_events AS
SELECT
  STR_SPLIT(completed, ':', 0) AS id,
  STR_SPLIT(completed, ':', 2) AS package_name,
  CASE
    WHEN STR_SPLIT(completed, ':', 1) = 'completed-hot' THEN 'hot'
    WHEN STR_SPLIT(completed, ':', 1) = 'completed-warm' THEN 'warm'
    WHEN STR_SPLIT(completed, ':', 1) = 'completed-cold' THEN 'cold'
    ELSE NULL
  END AS launch_type,
  MIN(ts)
FROM (
  SELECT ts, SUBSTR(name, 19) AS completed
  FROM slice
  WHERE
    dur = 0
    -- Originally completed was unqualified, but at some point we introduced
    -- the startup type as well
    AND name GLOB 'launchingActivity#*:completed*:*'
)
GROUP BY 1, 2, 3;

INSERT INTO launches(id, ts, ts_end, dur, package, launch_type)
SELECT
  id,
  ts,
  ts + dur AS ts_end,
  dur,
  package_name,
  launch_type
FROM launch_async_events
JOIN launch_complete_events USING (id);
