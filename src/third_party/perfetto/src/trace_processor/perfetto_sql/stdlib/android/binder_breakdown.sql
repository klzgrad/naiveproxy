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

INCLUDE PERFETTO MODULE android.binder;

INCLUDE PERFETTO MODULE intervals.overlap;

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE slices.with_context;

-- Client side of all binder txns, sorted by client_ts.
-- Suitable for interval_intersect.
CREATE PERFETTO VIEW _binder_client_view AS
SELECT
  binder_txn_id AS id,
  client_upid AS upid,
  client_ts AS ts,
  client_dur AS dur
FROM android_binder_txns
WHERE
  client_dur > 0
ORDER BY
  ts;

-- Server side of all binder txns, sorted by server_ts.
-- Suitable for interval_intersect.
CREATE PERFETTO VIEW _binder_server_view AS
SELECT
  binder_txn_id AS id,
  server_upid AS upid,
  server_ts AS ts,
  server_dur AS dur
FROM android_binder_txns
WHERE
  server_dur > 0
ORDER BY
  ts;

-- Thread state view suitable for interval intersect.
CREATE PERFETTO VIEW _thread_state_view AS
SELECT
  id,
  ts,
  dur,
  utid,
  cpu,
  state,
  io_wait
FROM thread_state
WHERE
  dur > 0
ORDER BY
  ts;

-- Partitions and flattens slices underneath the server or client side of binder
-- txns. The |name| column in the output is the lowest depth slice name in a
-- given partition.
CREATE PERFETTO MACRO _binder_flatten_descendants(
    id ColumnName,
    ts ColumnName,
    dur ColumnName,
    slice_name ColumnName
)
RETURNS TableOrSubQuery AS
(
  WITH
    root_slices AS (
      SELECT
        $id AS id,
        $ts AS ts,
        $dur AS dur
      FROM android_binder_txns
    ),
    child_slices AS (
      SELECT
        slice.id AS root_id,
        dec.*
      FROM slice, descendant_slice(slice.id) AS dec
      WHERE
        slice.name = $slice_name
    ),
    flat_slices AS (
      SELECT
        root_id,
        id,
        ts,
        dur
      FROM _intervals_flatten !(_intervals_merge_root_and_children !(root_slices,
                                                                child_slices))
    )
  SELECT
    row_number() OVER () AS id,
    root_slice.binder_txn_id,
    root_slice.binder_reply_id,
    flat_slices.id AS flat_id,
    flat_slices.ts,
    flat_slices.dur,
    thread_slice.name,
    thread_slice.utid
  FROM flat_slices
  JOIN thread_slice
    USING (id)
  JOIN android_binder_txns AS root_slice
    ON root_slice.$id = root_id
  WHERE
    flat_slices.dur > 0
);

-- Server side flattened descendant slices.
CREATE PERFETTO TABLE _binder_server_flat_descendants AS
SELECT
  *
FROM _binder_flatten_descendants!(binder_reply_id, server_ts,
                                             server_dur, 'binder reply')
ORDER BY
  id;

-- Client side flattened descendant slices.
CREATE PERFETTO TABLE _binder_client_flat_descendants AS
SELECT
  *
FROM _binder_flatten_descendants!(binder_txn_id, client_ts, client_dur,
                                           'binder transaction')
ORDER BY
  id;

-- Server side flattened descendants intersected with their thread_states.
CREATE PERFETTO TABLE _binder_server_flat_descendants_with_thread_state AS
SELECT
  ii.ts,
  ii.dur,
  _server_flat.binder_txn_id,
  _server_flat.binder_reply_id,
  _server_flat.name,
  _server_flat.flat_id,
  thread_state.state,
  thread_state.io_wait
FROM _interval_intersect !((_binder_server_flat_descendants,
                            _thread_state_view), (utid)) AS ii
JOIN _binder_server_flat_descendants AS _server_flat
  ON id_0 = _server_flat.id
JOIN thread_state
  ON id_1 = thread_state.id;

-- Client side flattened descendants intersected with their thread_states.
CREATE PERFETTO TABLE _binder_client_flat_descendants_with_thread_state AS
SELECT
  ii.ts,
  ii.dur,
  _client_flat.binder_txn_id,
  _client_flat.binder_reply_id,
  _client_flat.name,
  _client_flat.flat_id,
  _thread_state_view.state,
  _thread_state_view.io_wait
FROM _interval_intersect !((_binder_client_flat_descendants,
                            _thread_state_view), (utid)) AS ii
JOIN _binder_client_flat_descendants AS _client_flat
  ON id_0 = _client_flat.id
JOIN _thread_state_view
  ON id_1 = _thread_state_view.id;

-- Returns the 'reason' for a binder txn delay based on the descendant slice
-- name and thread_state information. It follows the following priority:
-- 1. direct_reclaim.
-- 2. GC blocking stall.
-- 3. Sleeping in monitor contention.
-- 4. Sleeping in ART lock contention.
-- 5. Sleeping in binder txn or reply.
-- 6. Sleeping in Mutex contention.
-- 7. IO.
-- 8. State itself.
CREATE PERFETTO FUNCTION _binder_reason(
    name STRING,
    state STRING,
    io_wait LONG,
    binder_txn_id LONG,
    binder_reply_id LONG,
    flat_id LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $name = 'mm_vmscan_direct_reclaim'
    THEN 'kernel_memory_reclaim'
    WHEN $name GLOB 'GC: Wait For*'
    THEN 'userspace_memory_reclaim'
    WHEN $state = 'S'
    AND (
      $name GLOB 'monitor contention*'
      OR $name GLOB 'Lock contention on a monitor lock*'
    )
    THEN 'monitor_contention'
    WHEN $state = 'S' AND (
      $name GLOB 'Lock contention*'
    )
    THEN 'art_lock_contention'
    WHEN $state = 'S'
    AND $binder_reply_id != $flat_id
    AND $binder_txn_id != $flat_id
    AND (
      $name = 'binder transaction' OR $name = 'binder reply'
    )
    THEN 'binder'
    WHEN $state = 'S' AND (
      $name = 'Contending for pthread mutex'
    )
    THEN 'mutex_contention'
    WHEN $state != 'S' AND $io_wait = 1
    THEN 'io'
    ELSE $state
  END AS name;

-- Server side binder breakdowns per transactions per txn.
CREATE PERFETTO TABLE android_binder_server_breakdown (
  -- Client side id of the binder txn.
  binder_txn_id JOINID(slice.id),
  -- Server side id of the binder txn.
  binder_reply_id JOINID(slice.id),
  -- Timestamp of an exclusive interval during the binder reply with a single
  -- reason.
  ts TIMESTAMP,
  -- Duration of an exclusive interval during the binder reply with a single
  -- reason.
  dur DURATION,
  -- Cause of delay during an exclusive interval of the binder reply.
  reason STRING
) AS
SELECT
  binder_txn_id,
  binder_reply_id,
  ts,
  dur,
  _binder_reason(name, state, io_wait, binder_txn_id, binder_reply_id, flat_id) AS reason
FROM _binder_server_flat_descendants_with_thread_state;

-- Client side binder breakdowns per transactions per txn.
CREATE PERFETTO TABLE android_binder_client_breakdown (
  -- Client side id of the binder txn.
  binder_txn_id JOINID(slice.id),
  -- Server side id of the binder txn.
  binder_reply_id JOINID(slice.id),
  -- Timestamp of an exclusive interval during the binder txn with a single
  -- latency reason.
  ts TIMESTAMP,
  -- Duration of an exclusive interval during the binder txn with a single
  -- latency reason.
  dur DURATION,
  -- Cause of delay during an exclusive interval of the binder txn.
  reason STRING
) AS
SELECT
  binder_txn_id,
  binder_reply_id,
  ts,
  dur,
  _binder_reason(name, state, io_wait, binder_txn_id, binder_reply_id, flat_id) AS reason
FROM _binder_client_flat_descendants_with_thread_state;

CREATE PERFETTO VIEW _binder_client_breakdown_view AS
SELECT
  binder_txn_id,
  binder_reply_id,
  ts,
  dur,
  reason AS client_reason
FROM android_binder_client_breakdown;

CREATE PERFETTO VIEW _binder_server_breakdown_view AS
SELECT
  binder_txn_id,
  ts,
  dur,
  reason AS server_reason
FROM android_binder_server_breakdown;

CREATE VIRTUAL TABLE _binder_client_server_breakdown_sp USING SPAN_LEFT_JOIN (
    _binder_client_breakdown_view PARTITIONED binder_txn_id,
    _binder_server_breakdown_view PARTITIONED binder_txn_id);

-- Combined client and server side binder breakdowns per transaction.
CREATE PERFETTO TABLE android_binder_client_server_breakdown (
  -- Client side id of the binder txn.
  binder_txn_id JOINID(slice.id),
  -- Server side id of the binder txn.
  binder_reply_id JOINID(slice.id),
  -- Timestamp of an exclusive interval during the binder txn with a single
  -- latency reason.
  ts TIMESTAMP,
  -- Duration of an exclusive interval during the binder txn with a single
  -- latency reason.
  dur DURATION,
  -- The server side component of this interval's binder latency reason, if any.
  server_reason STRING,
  -- The client side component of this interval's binder latency reason.
  client_reason STRING,
  -- Combined reason indicating whether latency came from client or server side.
  reason STRING,
  -- Whether the latency is due to the client or server.
  reason_type STRING
) AS
SELECT
  *,
  iif(server_reason IS NOT NULL, server_reason, client_reason) AS reason,
  iif(server_reason IS NOT NULL, 'server', 'client') AS reason_type
FROM _binder_client_server_breakdown_sp;
