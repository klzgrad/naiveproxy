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

-- Tracks for GPU work period events originating from the
-- `power/gpu_work_period` Linux ftrace tracepoint.
--
-- This tracepoint is usually only available on selected Android devices.
CREATE PERFETTO TABLE android_gpu_work_period_track (
  -- Unique identifier for this track. Joinable with track.id.
  id LONG,
  -- Machine identifier
  machine_id JOINID(machine.id),
  -- The UID of the package for which the GPU work period events were emitted.
  uid LONG,
  -- The unique GPU identifier (ugpu) from the gpu table.
  ugpu LONG,
  -- The raw GPU number.
  gpu_id LONG
) AS
SELECT
  t.id,
  t.machine_id,
  extract_arg(t.dimension_arg_set_id, 'uid') AS uid,
  extract_arg(t.dimension_arg_set_id, 'ugpu') AS ugpu,
  extract_arg(t.dimension_arg_set_id, 'gpu') AS gpu_id
FROM track AS t
WHERE
  t.type = 'android_gpu_work_period';
