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

INCLUDE PERFETTO MODULE android.frames.timeline;

INCLUDE PERFETTO MODULE android.startup.startup_events;

INCLUDE PERFETTO MODULE slices.with_context;

CREATE PERFETTO TABLE _startups_maxsdk28 AS
-- Warm and cold starts only are based on the launching slice
WITH
  warm_and_cold AS (
    SELECT
      le.ts,
      le.ts_end AS ts_end,
      package_name AS package,
      NULL AS startup_type
    FROM _startup_events AS le
  ),
  -- Hot starts don’t have a launching slice so we use activityResume as a
  -- proxy.
  --
  -- Note that this implementation will also count warm and cold starts but
  -- we will remove those below.
  maybe_hot AS (
    SELECT
      sl.ts,
      rs.ts + rs.dur AS ts_end,
      -- We use the process name as the package as we have no better option.
      coalesce(process_name, thread_name, 'unknown') AS package,
      "hot" AS startup_type
    FROM thread_slice AS sl, android_first_frame_after(sl.ts) AS rs
    WHERE
      name = 'activityResume'
      AND sl.is_main_thread
      -- Remove any launches here where the activityResume slices happens during
      -- a warm/cold startup.
      AND NOT EXISTS(
        SELECT
          1
        FROM warm_and_cold AS wac
        WHERE
          sl.ts BETWEEN wac.ts AND wac.ts_end
        LIMIT 1
      )
  ),
  cold_warm_hot AS (
    SELECT
      *
    FROM warm_and_cold
    UNION ALL
    SELECT
      *
    FROM maybe_hot
  )
SELECT
  ts,
  ts_end,
  ts_end - ts AS dur,
  package,
  startup_type
FROM cold_warm_hot
ORDER BY
  ts;
