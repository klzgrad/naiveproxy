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

DROP VIEW IF EXISTS android_trace_quality_failures;
CREATE PERFETTO VIEW android_trace_quality_failures AS
-- Check that all the sched slices are less than 1 week long.
SELECT
  'sched_slice_too_long' AS name,
  MAX(dur) > 1 * 7 * 24 * 60 * 60 * 1000 * 1000 * 1000 AS failed
FROM sched;

DROP VIEW IF EXISTS android_trace_quality_output;
CREATE PERFETTO VIEW android_trace_quality_output AS
SELECT AndroidTraceQualityMetric(
  'failures', (
    SELECT RepeatedField(AndroidTraceQualityMetric_Failure(
      'name', name
      ))
    FROM android_trace_quality_failures
    WHERE failed
  )
);
