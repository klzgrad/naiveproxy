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

INCLUDE PERFETTO MODULE android.process_metadata;

-- Establish relationships between thread/process/package
CREATE PERFETTO TABLE _sched_w_thread_process_package_summary AS
SELECT
  sched.ts,
  sched.dur,
  sched.cpu,
  thread.utid,
  thread.upid,
  thread.tid,
  process.pid,
  package.uid,
  thread.name AS thread_name,
  process.name AS process_name,
  package.package_name
FROM thread
JOIN sched
  USING (utid)
LEFT JOIN process
  USING (upid)
LEFT JOIN android_process_metadata AS package
  USING (upid)
WHERE
  dur > 0;
