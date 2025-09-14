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

-- Functions useful for filling the SystemState proto which gives
-- context to what was happening on the system during a startup.

INCLUDE PERFETTO MODULE android.startup.startups;

-- Given a launch id and process name glob, returns the sched.dur if a process with
-- that name was running on a CPU concurrent to that launch.
CREATE OR REPLACE PERFETTO FUNCTION dur_of_process_running_concurrent_to_launch(
  startup_id INT,
  process_glob STRING
)
RETURNS INT AS
SELECT IFNULL(SUM(sched.dur), 0)
FROM sched
JOIN thread USING (utid)
JOIN process USING (upid)
JOIN (
  SELECT ts, ts_end
  FROM android_startups
  WHERE startup_id = $startup_id
) launch
WHERE
  process.name GLOB $process_glob AND
  sched.ts BETWEEN launch.ts AND launch.ts_end;

-- Given a launch id and slice name glob, returns the number of slices with that
-- name which start concurrent to that launch.
CREATE OR REPLACE PERFETTO FUNCTION count_slices_concurrent_to_launch(startup_id INT, slice_glob STRING)
RETURNS INT AS
SELECT COUNT(1)
FROM slice
JOIN (
  SELECT ts, ts_end
  FROM android_startups
  WHERE startup_id = $startup_id
) launch
WHERE
  slice.name GLOB $slice_glob AND
  slice.ts BETWEEN launch.ts AND launch.ts_end;
