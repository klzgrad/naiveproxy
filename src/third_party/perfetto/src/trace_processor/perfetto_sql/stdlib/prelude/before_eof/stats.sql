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

-- Diagnostic stats and parser errors collected at trace time and during
-- import. One row per (key, idx, machine_id, trace_id) combination.
CREATE PERFETTO VIEW stats(
  -- Unique identifier for this stats row.
  id ID,
  -- Stat name (e.g. ftrace_cpu_overrun_begin).
  name STRING,
  -- Numeric stat key (stats::KeyIDs enum value).
  key LONG,
  -- Per-stat index for kIndexed stats; NULL for kSingle.
  idx LONG,
  -- "info" | "notice" | "data_loss" | "error".
  severity STRING,
  -- "trace" (recorded on-device) or "analysis" (TP).
  source STRING,
  -- Stat value.
  value LONG,
  -- Human-readable description.
  description STRING,
  -- Machine identifier (NULL for kGlobal stats).
  machine_id JOINID(machine.id),
  -- Trace identifier (NULL for kGlobal stats).
  trace_id LONG
)
AS
SELECT
  id,
  name,
  key,
  idx,
  severity,
  source,
  value,
  description,
  machine_id,
  trace_id
FROM __intrinsic_stats;
