--
-- Copyright 2026 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.profiling_manager.util;

-- Provides cold startups for redacted traces
-- Note: Redacted traces contain a subset of slices compare to a regular perfetto trace as they contain information only about the process
-- starting its own profiling, thus, queries in this section may omit process information as its assumed that slices belong
-- to the single profiled process.
CREATE PERFETTO TABLE android_profiling_manager_cold_startup (
  -- Initial timestamp for startup from apps perspective
  ts TIMESTAMP,
  -- slice name to identify the startup
  name STRING,
  -- duration in nanoseconds
  dur DURATION,
  -- Checkpoint where startup is marked as finished. Two cases:
  --   TTFF (Time to first frame): Startup marked as finished after first frame done
  --   TTI (Time to interactive): Startup marked as finished after app calls Activity#reportFullyDrawn()
  startup_checkpoint STRING
) AS
WITH
  startups AS (
    SELECT
      ts,
      'app_startup_ttff' AS name,
      dur,
      'TTFF' AS startup_checkpoint
    FROM _android_generate_start_to_end_slices('bindApplication', 'Choreographer#doFrame [0-9]*', TRUE)
    UNION ALL
    SELECT
      ts,
      'app_startup_tti' AS name,
      dur,
      'TTI' AS startup_checkpoint
    FROM _android_generate_start_to_end_slices('bindApplication', 'reportFullyDrawn*', TRUE)
  )
SELECT
  *
FROM startups;
