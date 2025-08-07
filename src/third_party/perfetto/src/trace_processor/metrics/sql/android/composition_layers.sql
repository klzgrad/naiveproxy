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

DROP VIEW IF EXISTS {{output}};
CREATE PERFETTO VIEW {{output}} AS
WITH composition_layer_counts AS (
  SELECT
    LAG(ts) OVER (ORDER BY ts) AS ts,
    name,
    INSTR(name, '(') AS separator_pos,
    value
  FROM counter c
  JOIN process_counter_track t ON c.track_id = t.id
  WHERE t.name GLOB '{{track_name}}*'
)
SELECT
  ts,
  CASE
    WHEN separator_pos = 0 THEN 'unspecified'
    ELSE SUBSTR(name, separator_pos + 1, INSTR(name, ')') - separator_pos - 1)
  END AS display_id,
  value
FROM composition_layer_counts
WHERE value >= 0 AND ts IS NOT NULL;
