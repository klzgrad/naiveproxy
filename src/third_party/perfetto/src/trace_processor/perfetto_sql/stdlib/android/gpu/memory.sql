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

INCLUDE PERFETTO MODULE linux.memory.general;

-- Counter for GPU memory per process with duration.
CREATE PERFETTO TABLE android_gpu_memory_per_process (
  -- Timestamp
  ts TIMESTAMP,
  -- Duration
  dur DURATION,
  -- Upid of the process
  upid JOINID(process.id),
  -- GPU memory
  gpu_memory LONG
) AS
SELECT
  ts,
  dur,
  upid,
  cast_int!(value) AS gpu_memory
FROM _all_counters_per_process
WHERE
  name = 'GPU Memory';
