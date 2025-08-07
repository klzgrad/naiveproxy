--
-- Copyright 2024 The Android Open Source Project
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

-- Device specific device curves with 1D dependency (i.e. curve characteristics
-- are dependent only on one CPU policy). See go/wattson for more info.
CREATE PERFETTO TABLE _gpu_device_curves AS
WITH
  data(device, freq_khz, active, idle1, idle2) AS (
    SELECT
      *
    FROM (VALUES
      ("Tensor", 0, 0, 0, 0),
      ("Tensor", 151000, 88.44, 15.68, 0),
      ("Tensor", 202000, 100.09, 15.97, 0),
      ("Tensor", 251000, 136.38, 23.34, 0),
      ("Tensor", 302000, 146.19, 23.62, 0),
      ("Tensor", 351000, 210.35, 34.16, 0),
      ("Tensor", 400000, 221.89, 34.51, 0),
      ("Tensor", 471000, 233.40, 35.10, 0),
      ("Tensor", 510000, 259.44, 41.19, 0),
      ("Tensor", 572000, 268.65, 41.59, 0),
      ("Tensor", 701000, 332.84, 54.65, 0),
      ("Tensor", 762000, 352.48, 59.98, 0),
      ("Tensor", 848000, 358.19, 60.72, 0)) AS _values
  )
SELECT
  *
FROM data;
