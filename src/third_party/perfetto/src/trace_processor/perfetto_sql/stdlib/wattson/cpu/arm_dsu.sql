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

-- Converts event counter from count to rate (num of accesses per ns).
CREATE PERFETTO FUNCTION _get_rate(
    event STRING
)
RETURNS TABLE (
  ts TIMESTAMP,
  dur DURATION,
  access_rate LONG
) AS
SELECT
  ts,
  lead(ts) OVER (PARTITION BY track_id ORDER BY ts) - ts AS dur,
  -- Rate of event accesses in a section (i.e. count / dur).
  value / (
    lead(ts) OVER (PARTITION BY track_id ORDER BY ts) - ts
  ) AS access_rate
FROM counter AS c
JOIN counter_track AS t
  ON c.track_id = t.id
WHERE
  t.name = $event;

-- The rate of L3 misses for each time slice based on the ARM DSU PMU counter's
-- bus_access event. Units will be in number of L3 misses per ns. The number of
-- accesses in a given duration can be calculated by multiplying the appropriate
-- rate with the time in the window of interest.
CREATE PERFETTO TABLE _arm_l3_miss_rate AS
SELECT
  ts,
  dur,
  access_rate AS l3_miss_rate
FROM _get_rate("arm_dsu_0/bus_access/_cpu0");

-- The rate of L3 accesses for each time slice based on the ARM DSU PMU
-- counter's l3d_cache event. Units will be in number of DDR accesses per ns.
-- The number of accesses in a given duration can be calculated by multiplying
-- the appropriate rate with the time in the window of interest.
CREATE PERFETTO TABLE _arm_l3_hit_rate AS
SELECT
  ts,
  dur,
  access_rate AS l3_hit_rate
FROM _get_rate("arm_dsu_0/l3d_cache/_cpu0");
