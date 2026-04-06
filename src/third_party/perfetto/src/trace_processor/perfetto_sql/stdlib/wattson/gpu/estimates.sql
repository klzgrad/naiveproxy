--
-- Copyright 2025 The Android Open Source Project
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

INCLUDE PERFETTO MODULE wattson.gpu.freq_idle;

INCLUDE PERFETTO MODULE wattson.curves.utils;

-- GPU power estimates in mW
CREATE PERFETTO TABLE _gpu_estimates_mw AS
SELECT
  gfi.ts,
  gfi.dur,
  c.curve_value AS gpu_mw
FROM _gpu_freq_idle AS gfi
JOIN _gpu_filtered_curves AS c
  ON c.freq_khz = gfi.freq AND c.idle = gfi.power_state
ORDER BY
  ts ASC;
