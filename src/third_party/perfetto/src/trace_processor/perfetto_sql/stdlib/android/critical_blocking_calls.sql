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
--
INCLUDE PERFETTO MODULE android.slices;

INCLUDE PERFETTO MODULE android.binder;

INCLUDE PERFETTO MODULE slices.with_context;

-- Internal implementation detail. Expands to a boolean expression that is true
-- when `slice_name` refers to a relevant blocking call.
--
-- This is a macro (rather than a `PERFETTO FUNCTION`) purely for performance:
-- trace_processor does not inline SQL functions, so calling one per row across
-- the whole `thread_slice` table (millions of rows) adds several seconds of
-- per-call dispatch overhead. A macro is expanded textually into the query, so
-- the predicate is evaluated directly with no call overhead.
CREATE PERFETTO MACRO _is_relevant_blocking_call_expr(
  -- Expression evaluating to the slice name to test.
  slice_name Expr
)
-- Boolean expression: true if the slice is a relevant blocking call.
RETURNS Expr
AS (
  $slice_name IN (
    'measure',
    'layout',
    'configChanged',
    'animation',
    'input',
    'traversal',
    'Contending for pthread mutex',
    'postAndWait',
    'CreateGraphicsPipeline',
    'flush layers',
    'flush commands',
    'queueBuffer'
  )
  OR $slice_name GLOB 'monitor contention with*'
  OR $slice_name GLOB 'SuspendThreadByThreadId*'
  OR $slice_name GLOB 'LoadApkAssetsFd*'
  OR $slice_name GLOB '*binder transaction*'
  OR $slice_name GLOB 'inflate*'
  OR $slice_name GLOB 'Lock contention on*'
  OR $slice_name GLOB 'android.os.Handler: kotlinx.coroutines*'
  OR $slice_name GLOB 'relayoutWindow*'
  OR $slice_name GLOB 'ImageDecoder#decode*'
  OR $slice_name GLOB 'NotificationStackScrollLayout#onMeasure'
  OR $slice_name GLOB 'ExpNotRow#*'
  OR $slice_name GLOB 'GC: Wait For*'
  OR $slice_name GLOB 'Recomposer:*'
  OR $slice_name GLOB 'Compose:*'
  OR $slice_name GLOB 'draw-VRI*'
  OR $slice_name GLOB 'drawLayer *'
  OR $slice_name GLOB 'DrawFrames*'
  OR $slice_name GLOB 'Texture upload*'
  OR (
    NOT ($slice_name GLOB '*Choreographer*')
    AND NOT ($slice_name GLOB '*Input*')
    AND NOT ($slice_name GLOB '*input*')
    AND NOT ($slice_name GLOB 'android.os.Handler: #*')
    AND (
      -- Handler pattern heuristics
      $slice_name GLOB '*Handler: *$*'
      OR $slice_name GLOB '*.*.*: *$*'
      OR $slice_name GLOB '*.*$*: #*'
    )
  )
);

-- Retained for backwards compatibility with any external callers. The `depth`
-- argument is unused.
CREATE PERFETTO FUNCTION _is_relevant_blocking_call(name STRING, depth LONG)
RETURNS BOOL
AS
SELECT _is_relevant_blocking_call_expr!($name);

CREATE PERFETTO TABLE _android_critical_blocking_calls_draw_frames AS
SELECT utid, id, ts, dur
FROM thread_slice
WHERE
  name GLOB 'DrawFrames*'
ORDER BY
  ts;

CREATE PERFETTO TABLE _android_critical_blocking_calls_wait_for_buffer_release
AS
SELECT utid, ts, dur
FROM thread_slice
WHERE
  name = 'waitForBufferRelease'
ORDER BY
  ts;

--Extract critical blocking calls from all processes.
CREATE PERFETTO TABLE _android_critical_blocking_calls AS
WITH
  _wait_for_buffer_release_dur AS (
    SELECT s.id, SUM(child.dur) AS dur
    FROM _android_critical_blocking_calls_wait_for_buffer_release AS child
    JOIN _android_critical_blocking_calls_draw_frames AS s
      ON child.utid = s.utid
      AND child.ts >= s.ts
      AND child.ts < s.ts + s.dur
    GROUP BY
      s.id
  )
SELECT
  android_standardize_slice_name(s.name) AS name,
  s.ts,
  iif(
    wait_for_buffer_release.dur IS NULL
    OR s.dur = -1,
    s.dur,
    MAX(s.dur - wait_for_buffer_release.dur, 0)
  ) AS dur,
  s.id,
  s.process_name,
  thread.utid,
  s.upid,
  s.ts
  + iif(
    wait_for_buffer_release.dur IS NULL
    OR s.dur = -1,
    s.dur,
    MAX(s.dur - wait_for_buffer_release.dur, 0)
  ) AS ts_end
FROM thread_slice AS s
JOIN thread USING (utid)
LEFT JOIN _wait_for_buffer_release_dur AS wait_for_buffer_release USING (id)
WHERE
  _is_relevant_blocking_call_expr!(s.name)
UNION ALL
-- Add a summation of all drawLayer slices without the individual layer name
SELECT
  'drawLayer' AS name,
  s.ts,
  s.dur,
  s.id,
  s.process_name,
  thread.utid,
  s.upid,
  s.ts + s.dur AS ts_end
FROM thread_slice AS s
JOIN thread USING (utid)
WHERE
  s.name GLOB 'drawLayer *'
UNION ALL
-- As binder names are not included in slice table, extract these directly from the
-- android_binder_txns table.
SELECT
  tx.aidl_name AS name,
  tx.client_ts AS ts,
  tx.client_dur AS dur,
  tx.binder_txn_id AS id,
  tx.client_process AS process_name,
  tx.client_utid AS utid,
  tx.client_upid AS upid,
  tx.client_ts + tx.client_dur AS ts_end
FROM android_binder_txns AS tx
WHERE
  NOT (aidl_name IS NULL)
  AND is_sync = 1;

CREATE PERFETTO FUNCTION _is_relevant_notifications_blocking_call(
  name STRING,
  dur LONG
)
RETURNS BOOL
AS
SELECT
  $name = 'NotificationStackScrollLayout#onMeasure'
  AND $dur > 0
  AND ($name GLOB 'NotificationStackScrollLayout#onMeasure'
  OR $name GLOB 'NotificationToplineView#onMeasure'
  OR $name GLOB 'ExpNotRow#*'
  OR $name GLOB 'NotificationShadeWindowView#onMeasure'
  OR $name GLOB 'ImageFloatingTextView#onMeasure');
