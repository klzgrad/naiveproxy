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


SELECT RUN_METRIC(
  'android/g2d_duration.sql',
  'g2d_type', 'sw',
  'output_table', 'g2d_sw_duration_metric'
);

SELECT RUN_METRIC(
  'android/g2d_duration.sql',
  'g2d_type', 'hw',
  'output_table', 'g2d_hw_duration_metric'
);

DROP VIEW IF EXISTS g2d_output;
CREATE PERFETTO VIEW g2d_output AS
SELECT G2dMetrics(
  'g2d_hw', (SELECT metric FROM g2d_hw_duration_metric),
  'g2d_sw', (SELECT metric FROM g2d_sw_duration_metric)
);
