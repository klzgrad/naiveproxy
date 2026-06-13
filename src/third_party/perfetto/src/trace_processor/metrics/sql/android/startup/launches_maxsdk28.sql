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

-- Cold/warm starts emitted launching slices on API level 28-.
INSERT INTO launches(id, ts, ts_end, dur, package, launch_type)
SELECT
  ROW_NUMBER() OVER(ORDER BY ts) AS id,
  launching_events.ts AS ts,
  launching_events.ts_end AS ts_end,
  launching_events.ts_end - launching_events.ts AS dur,
  package_name AS package,
  NULL AS launch_type
FROM launching_events
ORDER BY ts;

-- TODO(lalitm): add handling of hot starts using frame timings.
