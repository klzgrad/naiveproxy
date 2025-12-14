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

INCLUDE PERFETTO MODULE android.process_metadata;

INCLUDE PERFETTO MODULE android.suspend;

INCLUDE PERFETTO MODULE slices.flow;

-- Count Binder transactions per process.
CREATE PERFETTO VIEW android_binder_metrics_by_process (
  -- Name of the process that started the binder transaction.
  process_name STRING,
  -- PID of the process that started the binder transaction.
  pid LONG,
  -- Name of the slice with binder transaction.
  slice_name STRING,
  -- Number of binder transactions in process in slice.
  event_count LONG
) AS
SELECT
  process.name AS process_name,
  process.pid AS pid,
  slice.name AS slice_name,
  count(*) AS event_count
FROM slice
JOIN thread_track
  ON slice.track_id = thread_track.id
JOIN thread
  ON thread.utid = thread_track.utid
JOIN process
  ON thread.upid = process.upid
WHERE
  slice.name GLOB 'binder*'
GROUP BY
  process_name,
  slice_name;

CREATE PERFETTO TABLE _binder_txn_merged AS
WITH
  maybe_broken_binder_txn AS (
    -- Fetch the broken binder txns first, i.e, the txns that have children slices
    -- They may be broken because synchronous txns are typically blocked sleeping while
    -- waiting for a response.
    -- These broken txns will be excluded below in the binder_txn CTE
    SELECT
      ancestor.id
    FROM slice
    JOIN slice AS ancestor
      ON ancestor.id = slice.parent_id
    WHERE
      ancestor.name = 'binder transaction'
    GROUP BY
      ancestor.id
  ),
  nested_binder_txn AS (
    -- Detect the non-broken cases which are just nested binder txns
    SELECT DISTINCT
      root_node_id AS id
    FROM _slice_following_flow!(maybe_broken_binder_txn)
  ),
  broken_binder_txn AS (
    -- Exclude the nested txns from the 'maybe broken' set
    SELECT
      *
    FROM maybe_broken_binder_txn
    EXCEPT
    SELECT
      *
    FROM nested_binder_txn
  ),
  -- Adding MATERIALIZED here matters in cases where there are few/no binder
  -- transactions in the trace. Our cost estimation is not good enough to allow
  -- the query planner to see through to this fact. Instead, our cost estimation
  -- causes repeated queries on this table which is slow because it's an O(n)
  -- query.
  --
  -- We should fix this by doing some (ideally all) of the following:
  --  1) Add support for columnar tables in SQL which will allow for
  --     "subsetting" the slice table to only contain binder transactions.
  --  2) Make this query faster by adding improving string filtering.
  --  3) Add caching so that even if these queries happen many times, they are
  --     fast.
  --  4) Improve cost estimation algorithm to allow the joins to happen the
  --     right way around.
  binder_txn AS MATERIALIZED (
    SELECT
      slice.id AS binder_txn_id,
      process.name AS process_name,
      thread.name AS thread_name,
      thread.utid AS utid,
      thread.tid AS tid,
      process.pid AS pid,
      process.upid AS upid,
      slice.ts,
      slice.dur,
      thread.is_main_thread
    FROM slice
    JOIN thread_track
      ON slice.track_id = thread_track.id
    JOIN thread
      USING (utid)
    JOIN process
      USING (upid)
    LEFT JOIN broken_binder_txn
      ON broken_binder_txn.id = slice.id
    WHERE
      slice.name = 'binder transaction' AND broken_binder_txn.id IS NULL
  ),
  binder_reply AS (
    SELECT
      binder_txn.*,
      binder_reply.ts AS server_ts,
      binder_reply.dur AS server_dur,
      binder_reply.id AS binder_reply_id,
      reply_thread.name AS server_thread,
      reply_process.name AS server_process,
      reply_thread.utid AS server_utid,
      reply_thread.tid AS server_tid,
      reply_process.pid AS server_pid,
      reply_process.upid AS server_upid,
      aidl.name AS aidl_name,
      aidl.ts AS aidl_ts,
      aidl.dur AS aidl_dur
    FROM binder_txn
    JOIN flow AS binder_flow
      ON binder_txn.binder_txn_id = binder_flow.slice_out
    JOIN slice AS binder_reply
      ON binder_flow.slice_in = binder_reply.id
    JOIN thread_track AS reply_thread_track
      ON binder_reply.track_id = reply_thread_track.id
    JOIN thread AS reply_thread
      ON reply_thread.utid = reply_thread_track.utid
    JOIN process AS reply_process
      ON reply_process.upid = reply_thread.upid
    LEFT JOIN slice AS aidl
      ON aidl.parent_id = binder_reply.id
      AND (
        -- Filter for only server side AIDL slices as there are some client side ones for cpp
        aidl.name GLOB 'AIDL::*Server'
        OR aidl.name GLOB 'AIDL::*server'
        OR aidl.name GLOB 'HIDL::*server'
      )
  )
SELECT
  min(aidl_name) AS aidl_name,
  aidl_ts,
  aidl_dur,
  binder_txn_id,
  process_name AS client_process,
  thread_name AS client_thread,
  upid AS client_upid,
  utid AS client_utid,
  tid AS client_tid,
  pid AS client_pid,
  is_main_thread,
  ts AS client_ts,
  dur AS client_dur,
  binder_reply_id,
  server_process,
  server_thread,
  server_upid,
  server_utid,
  server_tid,
  server_pid,
  server_ts,
  server_dur
FROM binder_reply
WHERE
  client_dur != -1 AND server_dur != -1 AND client_dur >= server_dur
GROUP BY
  process_name,
  thread_name,
  binder_txn_id,
  binder_reply_id;

CREATE PERFETTO TABLE _oom_score AS
SELECT
  process.upid,
  cast_int!(c.value) AS value,
  c.ts,
  coalesce(lead(ts) OVER (PARTITION BY upid ORDER BY ts), trace_end()) AS end_ts
FROM counter AS c
JOIN process_counter_track AS t
  ON c.track_id = t.id
JOIN process
  USING (upid)
WHERE
  t.name = 'oom_score_adj';

CREATE PERFETTO INDEX _oom_score_idx ON _oom_score(upid, ts);

-- Breakdown synchronous binder transactions per txn.
-- It returns data about the client and server ends of every binder transaction.
CREATE PERFETTO VIEW _sync_binder_metrics_by_txn AS
SELECT
  binder.*,
  client_oom.value AS client_oom_score,
  server_oom.value AS server_oom_score
FROM _binder_txn_merged AS binder
LEFT JOIN _oom_score AS client_oom
  ON binder.client_upid = client_oom.upid
  AND binder.client_ts BETWEEN client_oom.ts AND client_oom.end_ts
LEFT JOIN _oom_score AS server_oom
  ON binder.server_upid = server_oom.upid
  AND binder.server_ts BETWEEN server_oom.ts AND server_oom.end_ts;

CREATE PERFETTO VIEW _binder_txn AS
SELECT
  client_ts AS ts,
  client_dur AS dur,
  client_utid AS utid,
  *
FROM _sync_binder_metrics_by_txn;

CREATE PERFETTO VIEW _binder_reply AS
SELECT
  server_ts AS ts,
  server_dur AS dur,
  server_utid AS utid,
  *
FROM _sync_binder_metrics_by_txn;

CREATE VIRTUAL TABLE _sp_binder_txn_thread_state USING SPAN_JOIN (_binder_txn PARTITIONED utid, thread_state PARTITIONED utid);

CREATE VIRTUAL TABLE _sp_binder_reply_thread_state USING SPAN_JOIN (_binder_reply PARTITIONED utid, thread_state PARTITIONED utid);

-- Aggregated thread_states on the client and server side per binder txn
-- This builds on the data from |_sync_binder_metrics_by_txn| and
-- for each end (client and server) of the transaction, it returns
-- the aggregated sum of all the thread state durations.
-- The |thread_state_type| column represents whether a given 'aggregated thread_state'
-- row is on the client or server side. 'binder_txn' is client side and 'binder_reply'
-- is server side.
CREATE PERFETTO VIEW android_sync_binder_thread_state_by_txn (
  -- slice id of the binder txn
  binder_txn_id LONG,
  -- Client timestamp
  client_ts TIMESTAMP,
  -- Client tid
  client_tid LONG,
  -- slice id of the binder reply
  binder_reply_id LONG,
  -- Server timestamp
  server_ts TIMESTAMP,
  -- Server tid
  server_tid LONG,
  -- whether thread state is on the txn or reply side
  thread_state_type STRING,
  -- a thread_state that occurred in the txn
  thread_state STRING,
  -- aggregated dur of the |thread_state| in the txn
  thread_state_dur DURATION,
  -- aggregated count of the |thread_state| in the txn
  thread_state_count LONG
) AS
SELECT
  binder_txn_id,
  client_ts,
  client_tid,
  binder_reply_id,
  server_ts,
  server_tid,
  'binder_txn' AS thread_state_type,
  state AS thread_state,
  sum(dur) AS thread_state_dur,
  count(dur) AS thread_state_count
FROM _sp_binder_txn_thread_state
GROUP BY
  binder_txn_id,
  binder_reply_id,
  thread_state_type,
  thread_state
UNION ALL
SELECT
  binder_txn_id,
  client_ts,
  client_tid,
  binder_reply_id,
  server_ts,
  server_tid,
  'binder_reply' AS thread_state_type,
  state AS thread_state,
  sum(dur) AS thread_state_dur,
  count(dur) AS thread_state_count
FROM _sp_binder_reply_thread_state
GROUP BY
  binder_txn_id,
  binder_reply_id,
  thread_state_type,
  thread_state;

-- Aggregated blocked_functions on the client and server side per binder txn
-- This builds on the data from |_sync_binder_metrics_by_txn| and
-- for each end (client and server) of the transaction, it returns
-- the aggregated sum of all the kernel blocked function durations.
-- The |thread_state_type| column represents whether a given 'aggregated blocked_function'
-- row is on the client or server side. 'binder_txn' is client side and 'binder_reply'
-- is server side.
CREATE PERFETTO VIEW android_sync_binder_blocked_functions_by_txn (
  -- slice id of the binder txn
  binder_txn_id LONG,
  -- Client ts
  client_ts TIMESTAMP,
  -- Client tid
  client_tid LONG,
  -- slice id of the binder reply
  binder_reply_id LONG,
  -- Server ts
  server_ts TIMESTAMP,
  -- Server tid
  server_tid LONG,
  -- whether thread state is on the txn or reply side
  thread_state_type STRING,
  -- blocked kernel function in a thread state
  blocked_function STRING,
  -- aggregated dur of the |blocked_function| in the txn
  blocked_function_dur DURATION,
  -- aggregated count of the |blocked_function| in the txn
  blocked_function_count LONG
) AS
SELECT
  binder_txn_id,
  client_ts,
  client_tid,
  binder_reply_id,
  server_ts,
  server_tid,
  'binder_txn' AS thread_state_type,
  blocked_function,
  sum(dur) AS blocked_function_dur,
  count(dur) AS blocked_function_count
FROM _sp_binder_txn_thread_state
WHERE
  blocked_function IS NOT NULL
GROUP BY
  binder_txn_id,
  binder_reply_id,
  blocked_function
UNION ALL
SELECT
  binder_txn_id,
  client_ts,
  client_tid,
  binder_reply_id,
  server_ts,
  server_tid,
  'binder_reply' AS thread_state_type,
  blocked_function,
  sum(dur) AS blocked_function_dur,
  count(dur) AS blocked_function_count
FROM _sp_binder_reply_thread_state
WHERE
  blocked_function IS NOT NULL
GROUP BY
  binder_txn_id,
  binder_reply_id,
  blocked_function;

CREATE PERFETTO TABLE _async_binder_reply AS
WITH
  async_reply AS MATERIALIZED (
    SELECT
      id,
      ts,
      dur,
      track_id,
      name
    FROM slice
    WHERE
      -- Filter for only server side AIDL slices as there are some client side ones for cpp
      name GLOB 'AIDL::*Server'
      OR name GLOB 'AIDL::*server'
      OR name GLOB 'HIDL::*server'
      OR name = 'binder async rcv'
  )
SELECT
  *,
  lead(name) OVER (PARTITION BY track_id ORDER BY ts) AS next_name,
  lead(ts) OVER (PARTITION BY track_id ORDER BY ts) AS next_ts,
  lead(dur) OVER (PARTITION BY track_id ORDER BY ts) AS next_dur
FROM async_reply
ORDER BY
  id;

CREATE PERFETTO TABLE _binder_async_txn_raw AS
SELECT
  slice.id AS binder_txn_id,
  process.name AS client_process,
  thread.name AS client_thread,
  process.upid AS client_upid,
  thread.utid AS client_utid,
  thread.tid AS client_tid,
  process.pid AS client_pid,
  thread.is_main_thread,
  slice.ts AS client_ts,
  slice.dur AS client_dur
FROM slice
JOIN thread_track
  ON slice.track_id = thread_track.id
JOIN thread
  USING (utid)
JOIN process
  USING (upid)
WHERE
  slice.name = 'binder transaction async'
ORDER BY
  binder_txn_id;

CREATE PERFETTO TABLE _binder_async_txn AS
SELECT
  iif(binder_reply.next_name = 'binder async rcv', NULL, binder_reply.next_name) AS aidl_name,
  iif(binder_reply.next_name = 'binder async rcv', NULL, binder_reply.next_ts) AS aidl_ts,
  iif(binder_reply.next_name = 'binder async rcv', NULL, binder_reply.next_dur) AS aidl_dur,
  binder_txn.*,
  binder_reply.id AS binder_reply_id,
  reply_process.name AS server_process,
  reply_thread.name AS server_thread,
  reply_process.upid AS server_upid,
  reply_thread.utid AS server_utid,
  reply_thread.tid AS server_tid,
  reply_process.pid AS server_pid,
  binder_reply.ts AS server_ts,
  binder_reply.dur AS server_dur
FROM _binder_async_txn_raw AS binder_txn
JOIN flow AS binder_flow
  ON binder_txn.binder_txn_id = binder_flow.slice_out
JOIN _async_binder_reply AS binder_reply
  ON binder_flow.slice_in = binder_reply.id
JOIN thread_track AS reply_thread_track
  ON binder_reply.track_id = reply_thread_track.id
JOIN thread AS reply_thread
  ON reply_thread.utid = reply_thread_track.utid
JOIN process AS reply_process
  ON reply_process.upid = reply_thread.upid
WHERE
  binder_reply.name = 'binder async rcv'
ORDER BY
  binder_txn_id;

-- Breakdown asynchronous binder transactions per txn.
-- It returns data about the client and server ends of every binder transaction async.
CREATE PERFETTO VIEW _async_binder_metrics_by_txn AS
SELECT
  binder.*,
  client_oom.value AS client_oom_score,
  server_oom.value AS server_oom_score
FROM _binder_async_txn AS binder
LEFT JOIN _oom_score AS client_oom
  ON binder.client_upid = client_oom.upid
  AND binder.client_ts BETWEEN client_oom.ts AND client_oom.end_ts
LEFT JOIN _oom_score AS server_oom
  ON binder.server_upid = server_oom.upid
  AND binder.server_ts BETWEEN server_oom.ts AND server_oom.end_ts;

-- Breakdown binder transactions per txn.
-- It returns data about the client and server ends of every binder transaction async.
CREATE PERFETTO TABLE android_binder_txns (
  -- Fully qualified name of the binder endpoint if existing.
  aidl_name STRING,
  -- Interface of the binder endpoint if existing.
  interface STRING,
  -- Method name of the binder endpoint if existing.
  method_name STRING,
  -- Timestamp the binder interface name was emitted. Proxy to 'ts' and 'dur' for async txns.
  aidl_ts TIMESTAMP,
  -- Duration of the binder interface name. Proxy to 'ts' and 'dur' for async txns.
  aidl_dur DURATION,
  -- Slice id of the binder txn.
  binder_txn_id JOINID(slice.id),
  -- Name of the client process.
  client_process STRING,
  -- Name of the client thread.
  client_thread STRING,
  -- Upid of the client process.
  client_upid JOINID(process.id),
  -- Utid of the client thread.
  client_utid JOINID(thread.id),
  -- Tid of the client thread.
  client_tid LONG,
  -- Pid of the client thread.
  client_pid LONG,
  -- Whether the txn was initiated from the main thread of the client process.
  is_main_thread BOOL,
  -- Timestamp of the client txn.
  client_ts TIMESTAMP,
  -- Wall clock dur of the client txn.
  client_dur DURATION,
  -- Slice id of the binder reply.
  binder_reply_id JOINID(slice.id),
  -- Name of the server process.
  server_process STRING,
  -- Name of the server thread.
  server_thread STRING,
  -- Upid of the server process.
  server_upid JOINID(process.id),
  -- Utid of the server thread.
  server_utid JOINID(thread.id),
  -- Tid of the server thread.
  server_tid LONG,
  -- Pid of the server thread.
  server_pid LONG,
  -- Timestamp of the server txn.
  server_ts TIMESTAMP,
  -- Wall clock dur of the server txn.
  server_dur DURATION,
  -- Oom score of the client process at the start of the txn.
  client_oom_score LONG,
  -- Oom score of the server process at the start of the reply.
  server_oom_score LONG,
  -- Whether the txn is synchronous or async (oneway).
  is_sync BOOL,
  -- Monotonic clock dur of the client txn.
  client_monotonic_dur DURATION,
  -- Monotonic clock dur of the server txn.
  server_monotonic_dur DURATION,
  -- Client package version_code.
  client_package_version_code LONG,
  -- Server package version_code.
  server_package_version_code LONG,
  -- Whether client package is debuggable.
  is_client_package_debuggable BOOL,
  -- Whether server package is debuggable.
  is_server_package_debuggable BOOL
) AS
WITH
  all_binder AS (
    SELECT
      *,
      1 AS is_sync
    FROM _sync_binder_metrics_by_txn
    UNION ALL
    SELECT
      *,
      0 AS is_sync
    FROM _async_binder_metrics_by_txn
  )
SELECT
  str_split(aidl_name, '::', 2) AS interface,
  str_split(aidl_name, '::', 3) AS method_name,
  all_binder.*,
  _extract_duration_without_suspend(client_ts, client_dur) AS client_monotonic_dur,
  _extract_duration_without_suspend(server_ts, server_dur) AS server_monotonic_dur,
  client_process_metadata.version_code AS client_package_version_code,
  server_process_metadata.version_code AS server_package_version_code,
  client_process_metadata.debuggable AS is_client_package_debuggable,
  server_process_metadata.debuggable AS is_server_package_debuggable
FROM all_binder
LEFT JOIN android_process_metadata AS client_process_metadata
  ON all_binder.client_upid = client_process_metadata.upid
LEFT JOIN android_process_metadata AS server_process_metadata
  ON all_binder.server_upid = server_process_metadata.upid
ORDER BY
  binder_txn_id;

-- Returns a DAG of all outgoing binder txns from a process.
-- The roots of the graph are the threads making the txns and the graph flows from:
-- thread -> server_process -> AIDL interface -> AIDL method.
-- The weights of each node represent the wall execution time in the server_process.
CREATE PERFETTO FUNCTION android_binder_outgoing_graph(
    -- Upid of process to generate an outgoing graph for.
    upid JOINID(process.id)
)
RETURNS TABLE (
  -- Pprof of outgoing binder txns.
  pprof BYTES
) AS
WITH
  threads AS (
    SELECT
      binder_txn_id,
      cat_stacks(client_thread) AS stack
    FROM android_binder_txns
    WHERE
      (
        NOT $upid IS NULL AND client_upid = $upid
      ) OR (
        $upid IS NULL
      )
  ),
  server_process AS (
    SELECT
      binder_txn_id,
      cat_stacks(stack, server_process) AS stack
    FROM android_binder_txns
    JOIN threads
      USING (binder_txn_id)
  ),
  end_points AS (
    SELECT
      binder_txn_id,
      cat_stacks(stack, str_split(aidl_name, '::', iif(aidl_name GLOB 'AIDL*', 2, 1))) AS stack
    FROM android_binder_txns
    JOIN server_process
      USING (binder_txn_id)
  ),
  aidl_names AS (
    SELECT
      binder_txn_id,
      server_dur,
      cat_stacks(stack, str_split(aidl_name, '::', iif(aidl_name GLOB 'AIDL*', 3, 2))) AS stack
    FROM android_binder_txns
    JOIN end_points
      USING (binder_txn_id)
  )
SELECT
  experimental_profile(stack, 'duration', 'ns', server_dur) AS pprof
FROM aidl_names;

-- Returns a DAG of all incoming binder txns from a process.
-- The roots of the graph are the clients making the txns and the graph flows from:
-- client_process -> AIDL interface -> AIDL method.
-- The weights of each node represent the wall execution time in the server_process.
CREATE PERFETTO FUNCTION android_binder_incoming_graph(
    -- Upid of process to generate an incoming graph for.
    upid JOINID(process.id)
)
RETURNS TABLE (
  -- Pprof of incoming binder txns.
  pprof BYTES
) AS
WITH
  client_process AS (
    SELECT
      binder_txn_id,
      cat_stacks(client_process) AS stack
    FROM android_binder_txns
    WHERE
      (
        NOT $upid IS NULL AND server_upid = $upid
      ) OR (
        $upid IS NULL
      )
  ),
  end_points AS (
    SELECT
      binder_txn_id,
      cat_stacks(stack, str_split(aidl_name, '::', iif(aidl_name GLOB 'AIDL*', 2, 1))) AS stack
    FROM android_binder_txns
    JOIN client_process
      USING (binder_txn_id)
  ),
  aidl_names AS (
    SELECT
      binder_txn_id,
      server_dur,
      cat_stacks(stack, str_split(aidl_name, '::', iif(aidl_name GLOB 'AIDL*', 3, 2))) AS stack
    FROM android_binder_txns
    JOIN end_points
      USING (binder_txn_id)
  )
SELECT
  experimental_profile(stack, 'duration', 'ns', server_dur) AS pprof
FROM aidl_names;

-- Returns a graph of all binder txns in a trace.
-- The nodes are client_process and server_process.
-- The weights of each node represent the wall execution time in the server_process.
CREATE PERFETTO FUNCTION android_binder_graph(
    -- Matches txns from client_processes greater than or equal to the OOM score.
    min_client_oom_score LONG,
    -- Matches txns from client_processes less than or equal to the OOM score.
    max_client_oom_score LONG,
    -- Matches txns to server_processes greater than or equal to the OOM score.
    min_server_oom_score LONG,
    -- Matches txns to server_processes less than or equal to the OOM score.
    max_server_oom_score LONG
)
RETURNS TABLE (
  -- Pprof of binder txns.
  pprof BYTES
) AS
WITH
  clients AS (
    SELECT
      binder_txn_id,
      cat_stacks(client_process) AS stack
    FROM android_binder_txns
    WHERE
      client_oom_score BETWEEN $min_client_oom_score AND $max_client_oom_score
  ),
  servers AS (
    SELECT
      binder_txn_id,
      server_dur,
      cat_stacks(stack, server_process) AS stack
    FROM android_binder_txns
    JOIN clients
      USING (binder_txn_id)
    WHERE
      server_oom_score BETWEEN $min_server_oom_score AND $max_server_oom_score
  )
SELECT
  experimental_profile(stack, 'duration', 'ns', server_dur) AS pprof
FROM servers;
