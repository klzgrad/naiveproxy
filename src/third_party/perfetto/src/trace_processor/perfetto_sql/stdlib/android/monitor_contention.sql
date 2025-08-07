--
-- Copyright 2023 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.suspend;

-- Extracts the blocking thread from a slice name
CREATE PERFETTO FUNCTION android_extract_android_monitor_contention_blocking_thread(
    -- Name of slice
    slice_name STRING
)
-- Blocking thread
RETURNS STRING AS
SELECT
  str_split(str_split($slice_name, "with owner ", 1), " (", 0);

-- Extracts the blocking thread tid from a slice name
CREATE PERFETTO FUNCTION android_extract_android_monitor_contention_blocking_tid(
    -- Name of slice
    slice_name STRING
)
-- Blocking thread tid
RETURNS LONG AS
SELECT
  cast_int!(STR_SPLIT(STR_SPLIT($slice_name, " (", 1), ")", 0));

-- Extracts the blocking method from a slice name
CREATE PERFETTO FUNCTION android_extract_android_monitor_contention_blocking_method(
    -- Name of slice
    slice_name STRING
)
-- Blocking thread
RETURNS STRING AS
SELECT
  str_split(str_split($slice_name, ") at ", 1), "(", 0) || "(" || str_split(str_split($slice_name, ") at ", 1), "(", 1);

-- Extracts a shortened form of the blocking method name from a slice name.
-- The shortened form discards the parameter and return
-- types.
CREATE PERFETTO FUNCTION android_extract_android_monitor_contention_short_blocking_method(
    -- Name of slice
    slice_name STRING
)
-- Blocking thread
RETURNS STRING AS
SELECT
  str_split(
    str_split(android_extract_android_monitor_contention_blocking_method($slice_name), " ", 1),
    "(",
    0
  );

-- Extracts the monitor contention blocked method from a slice name
CREATE PERFETTO FUNCTION android_extract_android_monitor_contention_blocked_method(
    -- Name of slice
    slice_name STRING
)
-- Blocking thread
RETURNS STRING AS
SELECT
  str_split(str_split($slice_name, "blocking from ", 1), "(", 0) || "(" || str_split(str_split($slice_name, "blocking from ", 1), "(", 1);

-- Extracts a shortened form of the monitor contention blocked method name
-- from a slice name. The shortened form discards the parameter and return
-- types.
CREATE PERFETTO FUNCTION android_extract_android_monitor_contention_short_blocked_method(
    -- Name of slice
    slice_name STRING
)
-- Blocking thread
RETURNS STRING AS
SELECT
  str_split(
    str_split(android_extract_android_monitor_contention_blocked_method($slice_name), " ", 1),
    "(",
    0
  );

-- Extracts the number of waiters on the monitor from a slice name
CREATE PERFETTO FUNCTION android_extract_android_monitor_contention_waiter_count(
    -- Name of slice
    slice_name STRING
)
-- Count of waiters on the lock
RETURNS LONG AS
SELECT
  cast_int!(STR_SPLIT(STR_SPLIT($slice_name, "waiters=", 1), " ", 0));

-- Extracts the monitor contention blocking source location from a slice name
CREATE PERFETTO FUNCTION android_extract_android_monitor_contention_blocking_src(
    -- Name of slice
    slice_name STRING
)
-- Blocking thread
RETURNS STRING AS
SELECT
  str_split(str_split($slice_name, ")(", 1), ")", 0);

-- Extracts the monitor contention blocked source location from a slice name
CREATE PERFETTO FUNCTION android_extract_android_monitor_contention_blocked_src(
    -- Name of slice
    slice_name STRING
)
-- Blocking thread
RETURNS STRING AS
SELECT
  str_split(str_split($slice_name, ")(", 2), ")", 0);

CREATE PERFETTO TABLE _valid_android_monitor_contention AS
SELECT
  slice.id AS id
FROM slice
LEFT JOIN slice AS child
  ON child.parent_id = slice.id
LEFT JOIN slice AS grand_child
  ON grand_child.parent_id = child.id
WHERE
  slice.name GLOB 'monitor contention*'
  AND (
    child.name GLOB 'Lock contention*' OR child.name IS NULL
  )
  AND (
    grand_child.name IS NULL
  )
GROUP BY
  slice.id;

-- Contains parsed monitor contention slices.
CREATE PERFETTO TABLE android_monitor_contention (
  -- Name of the method holding the lock.
  blocking_method STRING,
  -- Blocked_method without arguments and return types.
  blocked_method STRING,
  -- Blocking_method without arguments and return types.
  short_blocking_method STRING,
  -- Blocked_method without arguments and return types.
  short_blocked_method STRING,
  -- File location of blocking_method in form <filename:linenumber>.
  blocking_src STRING,
  -- File location of blocked_method in form <filename:linenumber>.
  blocked_src STRING,
  -- Zero indexed number of threads trying to acquire the lock.
  waiter_count LONG,
  -- Utid of thread holding the lock.
  blocked_utid JOINID(thread.id),
  -- Thread name of thread holding the lock.
  blocked_thread_name STRING,
  -- Utid of thread holding the lock.
  blocking_utid JOINID(thread.id),
  -- Thread name of thread holding the lock.
  blocking_thread_name STRING,
  -- Tid of thread holding the lock.
  blocking_tid LONG,
  -- Upid of process experiencing lock contention.
  upid JOINID(process.id),
  -- Process name of process experiencing lock contention.
  process_name STRING,
  -- Slice id of lock contention.
  id LONG,
  -- Timestamp of lock contention start.
  ts TIMESTAMP,
  -- Wall clock duration of lock contention.
  dur DURATION,
  -- Monotonic clock duration of lock contention.
  monotonic_dur DURATION,
  -- Thread track id of blocked thread.
  track_id JOINID(track.id),
  -- Whether the blocked thread is the main thread.
  is_blocked_thread_main LONG,
  -- Tid of the blocked thread
  blocked_thread_tid LONG,
  -- Whether the blocking thread is the main thread.
  is_blocking_thread_main LONG,
  -- Tid of thread holding the lock.
  blocking_thread_tid LONG,
  -- Slice id of binder reply slice if lock contention was part of a binder txn.
  binder_reply_id LONG,
  -- Timestamp of binder reply slice if lock contention was part of a binder txn.
  binder_reply_ts TIMESTAMP,
  -- Tid of binder reply slice if lock contention was part of a binder txn.
  binder_reply_tid LONG,
  -- Pid of process experiencing lock contention.
  pid LONG
) AS
SELECT
  android_extract_android_monitor_contention_blocking_method(slice.name) AS blocking_method,
  android_extract_android_monitor_contention_blocked_method(slice.name) AS blocked_method,
  android_extract_android_monitor_contention_short_blocking_method(slice.name) AS short_blocking_method,
  android_extract_android_monitor_contention_short_blocked_method(slice.name) AS short_blocked_method,
  android_extract_android_monitor_contention_blocking_src(slice.name) AS blocking_src,
  android_extract_android_monitor_contention_blocked_src(slice.name) AS blocked_src,
  android_extract_android_monitor_contention_waiter_count(slice.name) AS waiter_count,
  thread.utid AS blocked_utid,
  thread.name AS blocked_thread_name,
  blocking_thread.utid AS blocking_utid,
  android_extract_android_monitor_contention_blocking_thread(slice.name) AS blocking_thread_name,
  android_extract_android_monitor_contention_blocking_tid(slice.name) AS blocking_tid,
  thread.upid AS upid,
  process.name AS process_name,
  slice.id,
  slice.ts,
  slice.dur,
  _extract_duration_without_suspend(slice.ts, slice.dur) AS monotonic_dur,
  slice.track_id,
  thread.is_main_thread AS is_blocked_thread_main,
  thread.tid AS blocked_thread_tid,
  blocking_thread.is_main_thread AS is_blocking_thread_main,
  blocking_thread.tid AS blocking_thread_tid,
  binder_reply.id AS binder_reply_id,
  binder_reply.ts AS binder_reply_ts,
  binder_reply_thread.tid AS binder_reply_tid,
  process.pid
FROM slice
JOIN thread_track
  ON thread_track.id = slice.track_id
LEFT JOIN thread
  USING (utid)
LEFT JOIN process
  USING (upid)
LEFT JOIN ancestor_slice(slice.id) AS binder_reply
  ON binder_reply.name = 'binder reply'
LEFT JOIN thread_track AS binder_reply_thread_track
  ON binder_reply.track_id = binder_reply_thread_track.id
LEFT JOIN thread AS binder_reply_thread
  ON binder_reply_thread_track.utid = binder_reply_thread.utid
-- Before Android U, we didn't have blocking_thread tid (aosp/3000578). We do a LEFT JOIN instead
-- of JOIN so that on older devices we can at least capture the list of contentions without edges.
LEFT JOIN thread AS blocking_thread
  ON blocking_thread.tid = blocking_tid AND blocking_thread.upid = thread.upid
JOIN _valid_android_monitor_contention
  ON _valid_android_monitor_contention.id = slice.id
WHERE
  slice.name GLOB 'monitor contention*'
  AND slice.dur != -1
  AND NOT short_blocking_method IS NULL AND short_blocked_method IS NOT NULL
GROUP BY
  slice.id;

CREATE PERFETTO INDEX _android_monitor_contention_blocking_utid_idx ON android_monitor_contention(blocking_utid, ts);

CREATE PERFETTO INDEX _android_monitor_contention_id_idx ON android_monitor_contention(id);

-- Monitor contention slices that are blocked by another monitor contention slice.
-- They will have a |parent_id| field which is the id of the slice they are blocked by.
CREATE PERFETTO TABLE _children AS
SELECT
  parent.id AS parent_id,
  child.*
FROM android_monitor_contention AS child
JOIN android_monitor_contention AS parent
  ON parent.blocked_utid = child.blocking_utid
  AND child.ts BETWEEN parent.ts AND parent.ts + parent.dur;

-- Monitor contention slices that are blocking another monitor contention slice.
-- They will have a |child_id| field which is the id of the slice they are blocking.
CREATE PERFETTO TABLE _parents AS
SELECT
  parent.*,
  child.id AS child_id
FROM android_monitor_contention AS parent
JOIN android_monitor_contention AS child
  ON parent.blocked_utid = child.blocking_utid
  AND child.ts BETWEEN parent.ts AND parent.ts + parent.dur;

-- Monitor contention slices that are neither blocking nor blocked by another monitor contention
-- slice. They neither have |parent_id| nor |child_id| fields.
CREATE PERFETTO TABLE _isolated AS
WITH
  parents_and_children AS (
    SELECT
      id
    FROM _children
    UNION ALL
    SELECT
      id
    FROM _parents
  ),
  isolated AS (
    SELECT
      id
    FROM android_monitor_contention
    EXCEPT
    SELECT
      id
    FROM parents_and_children
  )
SELECT
  *
FROM android_monitor_contention
JOIN isolated
  USING (id);

-- Contains parsed monitor contention slices with the parent-child relationships.
CREATE PERFETTO TABLE android_monitor_contention_chain (
  -- Id of monitor contention slice blocking this contention.
  parent_id LONG,
  -- Name of the method holding the lock.
  blocking_method STRING,
  -- Blocked_method without arguments and return types.
  blocked_method STRING,
  -- Blocking_method without arguments and return types.
  short_blocking_method STRING,
  -- Blocked_method without arguments and return types.
  short_blocked_method STRING,
  -- File location of blocking_method in form <filename:linenumber>.
  blocking_src STRING,
  -- File location of blocked_method in form <filename:linenumber>.
  blocked_src STRING,
  -- Zero indexed number of threads trying to acquire the lock.
  waiter_count LONG,
  -- Utid of thread holding the lock.
  blocked_utid JOINID(thread.id),
  -- Thread name of thread holding the lock.
  blocked_thread_name STRING,
  -- Utid of thread holding the lock.
  blocking_utid JOINID(thread.id),
  -- Thread name of thread holding the lock.
  blocking_thread_name STRING,
  -- Tid of thread holding the lock.
  blocking_tid LONG,
  -- Upid of process experiencing lock contention.
  upid JOINID(process.id),
  -- Process name of process experiencing lock contention.
  process_name STRING,
  -- Slice id of lock contention.
  id LONG,
  -- Timestamp of lock contention start.
  ts TIMESTAMP,
  -- Wall clock duration of lock contention.
  dur DURATION,
  -- Monotonic clock duration of lock contention.
  monotonic_dur DURATION,
  -- Thread track id of blocked thread.
  track_id JOINID(track.id),
  -- Whether the blocked thread is the main thread.
  is_blocked_thread_main LONG,
  -- Tid of the blocked thread
  blocked_thread_tid LONG,
  -- Whether the blocking thread is the main thread.
  is_blocking_thread_main LONG,
  -- Tid of thread holding the lock.
  blocking_thread_tid LONG,
  -- Slice id of binder reply slice if lock contention was part of a binder txn.
  binder_reply_id LONG,
  -- Timestamp of binder reply slice if lock contention was part of a binder txn.
  binder_reply_ts TIMESTAMP,
  -- Tid of binder reply slice if lock contention was part of a binder txn.
  binder_reply_tid LONG,
  -- Pid of process experiencing lock contention.
  pid LONG,
  -- Id of monitor contention slice blocked by this contention.
  child_id LONG
) AS
SELECT
  NULL AS parent_id,
  *,
  NULL AS child_id
FROM _isolated
UNION ALL
SELECT
  c.*,
  p.child_id
FROM _children AS c
LEFT JOIN _parents AS p
  USING (id)
UNION
SELECT
  c.parent_id,
  p.*
FROM _parents AS p
LEFT JOIN _children AS c
  USING (id);

CREATE PERFETTO INDEX _android_monitor_contention_chain_idx ON android_monitor_contention_chain(blocking_method, blocking_utid, ts);

-- First blocked node on a lock, i.e nodes with |waiter_count| = 0. The |dur| here is adjusted
-- to only account for the time between the first thread waiting and the first thread to acquire
-- the lock. That way, the thread state span joins below only compute the thread states where
-- the blocking thread is actually holding the lock. This avoids counting the time when another
-- waiter acquired the lock before the first waiter.
CREATE PERFETTO VIEW _first_blocked_contention AS
SELECT
  start.id,
  start.blocking_utid,
  start.ts,
  min(end.ts + end.dur) - start.ts AS dur
FROM android_monitor_contention_chain AS start
JOIN android_monitor_contention_chain AS end
  ON start.blocking_utid = end.blocking_utid
  AND start.blocking_method = end.blocking_method
  AND end.ts BETWEEN start.ts AND start.ts + start.dur
WHERE
  start.waiter_count = 0
GROUP BY
  start.id;

CREATE PERFETTO VIEW _blocking_thread_state AS
SELECT
  utid AS blocking_utid,
  ts,
  dur,
  state,
  blocked_function
FROM thread_state;

CREATE VIRTUAL TABLE _android_monitor_contention_chain_thread_state USING SPAN_JOIN (_first_blocked_contention PARTITIONED blocking_utid,
            _blocking_thread_state PARTITIONED blocking_utid);

-- Contains the span join of the first waiters in the |android_monitor_contention_chain| with their
-- blocking_thread thread state.
--
-- Note that we only span join the duration where the lock was actually held and contended.
-- This can be less than the duration the lock was 'waited on' when a different waiter acquired the
-- lock earlier than the first waiter.
CREATE PERFETTO TABLE android_monitor_contention_chain_thread_state (
  -- Slice id of lock contention.
  id LONG,
  -- Timestamp of lock contention start.
  ts TIMESTAMP,
  -- Wall clock duration of lock contention.
  dur DURATION,
  -- Utid of the blocking |thread_state|.
  blocking_utid JOINID(thread.id),
  -- Blocked kernel function of the blocking thread.
  blocked_function STRING,
  -- Thread state of the blocking thread.
  state STRING
) AS
SELECT
  id,
  ts,
  dur,
  blocking_utid,
  blocked_function,
  state
FROM _android_monitor_contention_chain_thread_state;

-- Aggregated thread_states on the 'blocking thread', the thread holding the lock.
-- This builds on the data from |android_monitor_contention_chain| and
-- for each contention slice, it returns the aggregated sum of all the thread states on the
-- blocking thread.
--
-- Note that this data is only available for the first waiter on a lock.
--
CREATE PERFETTO VIEW android_monitor_contention_chain_thread_state_by_txn (
  -- Slice id of the monitor contention.
  id LONG,
  -- A |thread_state| that occurred in the blocking thread during the contention.
  thread_state STRING,
  -- Total time the blocking thread spent in the |thread_state| during contention.
  thread_state_dur DURATION,
  -- Count of all times the blocking thread entered |thread_state| during the contention.
  thread_state_count LONG
) AS
SELECT
  id,
  state AS thread_state,
  sum(dur) AS thread_state_dur,
  count(dur) AS thread_state_count
FROM android_monitor_contention_chain_thread_state
GROUP BY
  id,
  thread_state;

-- Aggregated blocked_functions on the 'blocking thread', the thread holding the lock.
-- This builds on the data from |android_monitor_contention_chain| and
-- for each contention, it returns the aggregated sum of all the kernel
-- blocked function durations on the blocking thread.
--
-- Note that this data is only available for the first waiter on a lock.
CREATE PERFETTO VIEW android_monitor_contention_chain_blocked_functions_by_txn (
  -- Slice id of the monitor contention.
  id LONG,
  -- Blocked kernel function in a thread state in the blocking thread during the contention.
  blocked_function STRING,
  -- Total time the blocking thread spent in the |blocked_function| during the contention.
  blocked_function_dur DURATION,
  -- Count of all times the blocking thread executed the |blocked_function| during the contention.
  blocked_function_count LONG
) AS
SELECT
  id,
  blocked_function,
  sum(dur) AS blocked_function_dur,
  count(dur) AS blocked_function_count
FROM android_monitor_contention_chain_thread_state
WHERE
  blocked_function IS NOT NULL
GROUP BY
  id,
  blocked_function;

-- Returns a DAG of all Java lock contentions in a process.
-- Each node in the graph is a <thread:Java method> pair.
-- Each edge connects from a node waiting on a lock to a node holding a lock.
-- The weights of each node represent the cumulative wall time the node blocked
-- other nodes connected to it.
CREATE PERFETTO FUNCTION android_monitor_contention_graph(
    -- Upid of process to generate a lock graph for.
    upid JOINID(process.id)
)
RETURNS TABLE (
  -- Pprof of lock graph.
  pprof BYTES
) AS
WITH
  contention_chain AS (
    SELECT
      *,
      iif(blocked_thread_name GLOB 'binder:*', 'binder', blocked_thread_name) AS blocked_thread_name_norm,
      iif(blocking_thread_name GLOB 'binder:*', 'binder', blocking_thread_name) AS blocking_thread_name_norm
    FROM android_monitor_contention_chain
    WHERE
      upid = $upid
    GROUP BY
      id,
      parent_id
  ),
  graph AS (
    SELECT
      id,
      dur,
      cat_stacks(
        blocked_thread_name_norm || ':' || short_blocked_method,
        blocking_thread_name_norm || ':' || short_blocking_method
      ) AS stack
    FROM contention_chain
    WHERE
      parent_id IS NULL
    UNION ALL
    SELECT
      c.id,
      c.dur AS dur,
      cat_stacks(
        blocked_thread_name_norm || ':' || short_blocked_method,
        blocking_thread_name_norm || ':' || short_blocking_method,
        stack
      ) AS stack
    FROM contention_chain AS c, graph AS p
    WHERE
      p.id = c.parent_id
  )
SELECT
  experimental_profile(stack, 'duration', 'ns', dur) AS pprof
FROM graph;
