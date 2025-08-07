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

INCLUDE PERFETTO MODULE android.anrs;

DROP VIEW IF EXISTS android_anr_output;
CREATE PERFETTO VIEW android_anr_output AS
SELECT
  AndroidAnrMetric(
    'anr', (
      SELECT RepeatedField(
        AndroidAnrMetric_Anr(
          'process_name', process_name,
          'pid', pid,
          'subject', subject,
          'error_id', error_id,
          'ts', ts))
      FROM android_anrs
    )
  );