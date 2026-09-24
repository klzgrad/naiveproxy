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

-- Each thread joined with its (optional) process, materialized into a table so
-- that callers can attach thread+process context with a single INNER JOIN on
-- `utid` instead of writing `thread LEFT JOIN process`.
--
-- Why this exists: SQLite will not reorder a virtual table across a LEFT JOIN,
-- so a context view that embeds `thread ... LEFT JOIN process` directly cannot
-- be driven from a point-lookup on the fact table's id when it is joined into
-- from another table (the id constraint is never offered to xBestIndex, and the
-- join collapses into a full scan). Pre-computing the thread/process LEFT JOIN
-- here lets those views use only INNER joins, which the planner can reorder
-- freely. Internal only for now.
CREATE PERFETTO TABLE _thread_with_process AS
SELECT
  thread.utid,
  thread.tid,
  thread.name AS thread_name,
  thread.is_main_thread,
  process.upid,
  process.pid,
  process.name AS process_name
FROM thread
LEFT JOIN process USING (upid);
