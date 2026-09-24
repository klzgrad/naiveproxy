--
-- Copyright 2024 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the 'License');
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an 'AS IS' BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

-- One row per (process, heap, dump): the dump as a slice over its profiling
-- interval. The end is heap_profile.ts_end (the dump timestamp); the start is
-- heap_profile.ts when present. For older producers without a start (ts ==
-- ts_end) we fall back to just after the previous dump of the same heap (or
-- the trace start for the first), so continuous dumps tile the timeline
-- without gaps.
--
-- Each row also carries three byte totals for the interval:
--   * allocated: bytes allocated during the interval.
--   * retained:  bytes allocated during the interval that were still live at
--                the dump (allocated but not freed within the interval).
--   * delta:     net change (allocations minus all frees in the interval,
--                including frees of memory allocated before the interval), so
--                it can differ from retained.
-- These are 1:1 with the flamegraph roots shown when a slice is selected (see
-- heap_profile_details_panel.ts): allocated == "Total" (self_alloc_size) and
-- retained == "Unreleased" (self_size).
CREATE PERFETTO TABLE _android_heap_profile_intervals AS
WITH
  -- The heap profiler data is delta-encoded: per callsite each dump records the
  -- allocations (positive size) and frees (negative size) since the previous
  -- dump. Aggregate per callsite first so retained can apply the flamegraph's
  -- "count the callsite only if its net is positive" rule.
  per_callsite AS (
    SELECT
      upid,
      heap_name,
      ts,
      min(id) AS min_id,
      sum(count) AS net_count,
      sum(size) AS net_size,
      sum(max(size, 0)) AS alloc_size
    FROM heap_profile_allocation
    GROUP BY
      upid,
      heap_name,
      ts,
      callsite_id
  ),
  dumps AS (
    SELECT
      min(min_id) AS id,
      p.upid AS upid,
      p.heap_name AS heap_name,
      p.ts AS ts_end,
      hp.ts AS start_ts,
      sum(iif(net_count > 0, net_size, 0)) AS retained,
      sum(alloc_size) AS allocated,
      sum(net_size) AS delta
    FROM per_callsite AS p
    JOIN heap_profile AS hp
      ON hp.upid = p.upid
      AND p.ts = hp.ts_end
    GROUP BY
      p.upid,
      p.heap_name,
      p.ts
  ),
  with_start AS (
    SELECT
      id,
      upid,
      heap_name,
      ts_end,
      retained,
      allocated,
      delta,
      iif(
        start_ts < ts_end,
        start_ts,
        -- Start just after the previous dump of this heap, but never past this
        -- dump: a dump at trace_start() has no room before it, so collapse to
        -- a zero-duration instant (ts = ts_end) instead of inverting.
        min(
          lag(ts_end, 1, trace_start()) OVER (
            PARTITION BY
              upid,
              heap_name
            ORDER BY ts_end
          )
          + 1,
          ts_end
        )
      ) AS ts
    FROM dumps
  )
SELECT id, upid, heap_name, ts, ts_end - ts AS dur, retained, allocated, delta
FROM with_start;
