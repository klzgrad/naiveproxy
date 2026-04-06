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
--

DROP VIEW IF EXISTS gc_slices;
CREATE PERFETTO VIEW gc_slices AS
SELECT slice_ts AS ts, slice_dur AS dur, utid, startup_id AS launch_id
FROM thread_slices_for_all_launches
WHERE
  slice_name GLOB '*mark sweep GC'
  OR slice_name GLOB '*concurrent copying GC'
  OR slice_name GLOB '*semispace GC';

DROP TABLE IF EXISTS gc_slices_by_state;
CREATE VIRTUAL TABLE gc_slices_by_state
USING SPAN_JOIN(gc_slices PARTITIONED utid, thread_state_extended PARTITIONED utid);

DROP TABLE IF EXISTS running_gc_slices_materialized;
CREATE PERFETTO TABLE running_gc_slices_materialized AS
SELECT launch_id, SUM(dur) AS sum_dur
FROM gc_slices_by_state
WHERE state = 'Running'
GROUP BY launch_id;
