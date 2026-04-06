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

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE wattson.curves.utils;

INCLUDE PERFETTO MODULE wattson.tpu.freq_idle;

INCLUDE PERFETTO MODULE wattson.utils;

-- TPU frequency and parallel requests combined states
CREATE PERFETTO TABLE _tpu_combined_state AS
SELECT
  ii.ts,
  ii.dur,
  tf.freq,
  tf.cluster,
  tr.requests
FROM _interval_intersect!(
  (
    _ii_subquery!(_tpu_freq),
    _ii_subquery!(_tpu_requests_count)
  ),
  ()
) AS ii
JOIN _tpu_freq AS tf
  ON tf._auto_id = id_0
JOIN _tpu_requests_count AS tr
  ON tr._auto_id = id_1;

-- TPU power estimates in mW
CREATE PERFETTO TABLE _tpu_estimates_mw AS
SELECT
  t.ts,
  t.dur,
  coalesce(c.active, 0) AS tpu_mw
FROM _tpu_combined_state AS t
JOIN _tpu_filtered_curves AS c
  ON c.freq = t.freq AND c.cluster = t.cluster AND c.requests = min(t.requests, 8)
ORDER BY
  ts ASC;
