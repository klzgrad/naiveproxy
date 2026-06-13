--
-- Copyright 2019 The Android Open Source Project
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

CREATE PERFETTO VIEW _startup_async_events AS
SELECT
  ts,
  dur,
  cast_int!(SUBSTR(name, 19) ) AS startup_id
FROM slice
WHERE
  name GLOB 'launchingActivity#*' AND dur > 0 AND instr(name, ':') = 0;

CREATE PERFETTO VIEW _startup_complete_events AS
SELECT
  cast_int!(STR_SPLIT(completed, ':', 0)) AS startup_id,
  str_split(completed, ':', 2) AS package_name,
  CASE
    WHEN str_split(completed, ':', 1) = 'completed-hot'
    THEN 'hot'
    WHEN str_split(completed, ':', 1) = 'completed-warm'
    THEN 'warm'
    WHEN str_split(completed, ':', 1) = 'completed-cold'
    THEN 'cold'
    ELSE NULL
  END AS startup_type,
  min(ts)
FROM (
  SELECT
    ts,
    substr(name, 19) AS completed
  FROM slice
  WHERE
    dur = 0
    -- Originally completed was unqualified, but at some point we introduced
    -- the startup type as well
    AND name GLOB 'launchingActivity#*:completed*:*'
    AND NOT name GLOB '*:completed-same-process:*'
)
GROUP BY
  1,
  2,
  3;

CREATE PERFETTO TABLE _startups_minsdk33 AS
SELECT
  startup_id,
  ts,
  ts + dur AS ts_end,
  dur,
  package_name AS package,
  startup_type
FROM _startup_async_events
JOIN _startup_complete_events
  USING (startup_id);
