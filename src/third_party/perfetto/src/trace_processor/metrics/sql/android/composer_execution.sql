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


-- The HWC execution time will be calculated based on the runtime of
-- HwcPresentOrValidateDisplay, HwcValidateDisplay, and/or HwcPresentDisplay
-- which are happened in one frame.
-- There are 3 possible combinations how those functions will be called, i.e.:
-- 1. HwcPresentOrValidateDisplay and then HwcPresentDisplay
-- 2. HwcPresentOrValidateDisplay
-- 3. HwcValidateDisplay and then HwcPresentDisplay
DROP VIEW IF EXISTS raw_hwc_function_spans;
CREATE PERFETTO VIEW raw_hwc_function_spans AS
SELECT
  id,
  display_id,
  name,
  dur,
  LEAD(name, 1, '') OVER (PARTITION BY display_id ORDER BY ts) AS next_name,
  LEAD(dur, 1, 0) OVER (PARTITION BY display_id ORDER BY ts) AS next_dur
FROM(
  SELECT
    id,
    ts,
    dur,
    track_id,
    CASE
      WHEN INSTR(name, ' ') = 0 THEN name
      ELSE SUBSTR(name, 1, INSTR(name, ' ')-1)
    END AS name,
    CASE
      WHEN INSTR(name, ' ') = 0 THEN 'unspecified'
      ELSE SUBSTR(name, INSTR(name, ' ')+1)
    END AS display_id
  FROM slice
  WHERE name GLOB 'HwcPresentOrValidateDisplay*' OR name GLOB 'HwcValidateDisplay*'
    OR name GLOB 'HwcPresentDisplay*'
)
ORDER BY ts;

DROP VIEW IF EXISTS {{output}};
CREATE PERFETTO VIEW {{output}} AS
SELECT
  id,
  display_id,
  CASE
    WHEN name = 'HwcValidateDisplay' AND next_name = 'HwcPresentDisplay' THEN dur + next_dur
    WHEN name = 'HwcPresentOrValidateDisplay' AND next_name = 'HwcPresentDisplay' THEN dur + next_dur
    WHEN name = 'HwcPresentOrValidateDisplay' AND next_name != 'HwcPresentDisplay' THEN dur
    ELSE 0
  END AS execution_time_ns,
  CASE
    WHEN name = 'HwcValidateDisplay' AND next_name = 'HwcPresentDisplay' THEN 'separated_validation'
    WHEN name = 'HwcPresentOrValidateDisplay' AND next_name = 'HwcPresentDisplay' THEN 'unskipped_validation'
    WHEN name = 'HwcPresentOrValidateDisplay' AND next_name != 'HwcPresentDisplay' THEN 'skipped_validation'
    ELSE 'unknown'
  END AS validation_type
FROM raw_hwc_function_spans
WHERE (name = 'HwcValidateDisplay' OR name = 'HwcPresentOrValidateDisplay');
