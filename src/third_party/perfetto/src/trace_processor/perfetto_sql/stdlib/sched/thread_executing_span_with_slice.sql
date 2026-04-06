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

INCLUDE PERFETTO MODULE slices.flat_slices;

INCLUDE PERFETTO MODULE sched.thread_executing_span;

INCLUDE PERFETTO MODULE graphs.critical_path;

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE intervals.overlap;

CREATE PERFETTO TABLE _critical_path_userspace AS
SELECT
  *
FROM _critical_path_intervals
    !(_wakeup_userspace_edges,
      (SELECT id AS root_node_id, id - COALESCE(prev_id, id) AS capacity FROM _wakeup_graph),
      _wakeup_intervals);

CREATE PERFETTO TABLE _critical_path_kernel AS
WITH
  _kernel_nodes AS (
    SELECT
      id,
      root_id
    FROM _critical_path_userspace
    JOIN _wakeup_graph
      USING (id)
    WHERE
      is_idle_reason_self = 1
  )
SELECT
  _kernel_nodes.root_id,
  cr.root_id AS parent_id,
  cr.id,
  cr.ts,
  cr.dur
FROM _critical_path_intervals
    !(_wakeup_kernel_edges,
      (
        SELECT graph.id AS root_node_id, graph.id - COALESCE(graph.prev_id, graph.id) AS capacity
        FROM _kernel_nodes
        JOIN _wakeup_graph graph USING(id)
      ),
      _wakeup_intervals) AS cr
JOIN _kernel_nodes
  ON _kernel_nodes.id = cr.root_id
ORDER BY
  -- Important to allow for fast lookup of the parent_id in
  -- `_critical_path_kernel_adjusted`.
  parent_id;

CREATE PERFETTO TABLE _critical_path_userspace_adjusted AS
SELECT DISTINCT
  *
FROM _critical_path_userspace_adjusted!(_critical_path_userspace, _wakeup_graph);

CREATE PERFETTO TABLE _critical_path_kernel_adjusted AS
SELECT DISTINCT
  *
FROM _critical_path_kernel_adjusted!(_critical_path_userspace_adjusted, _critical_path_kernel, _wakeup_graph);

CREATE PERFETTO TABLE _critical_path_merged_adjusted AS
SELECT
  root_id,
  parent_id,
  id,
  ts,
  dur
FROM _critical_path_userspace_adjusted
UNION ALL
SELECT
  root_id,
  parent_id,
  id,
  ts,
  dur
FROM _critical_path_kernel_adjusted
WHERE
  id != parent_id;

CREATE PERFETTO TABLE _critical_path_roots AS
SELECT
  root_id,
  min(ts) AS root_ts,
  max(ts + dur) - min(ts) AS root_dur
FROM _critical_path_userspace_adjusted
GROUP BY
  root_id;

CREATE PERFETTO TABLE _critical_path_roots_and_merged AS
WITH
  roots_and_merged_critical_path AS (
    SELECT
      root_id,
      root_ts,
      root_dur,
      parent_id,
      id,
      ts,
      dur
    FROM _critical_path_merged_adjusted
    JOIN _critical_path_roots
      USING (root_id)
  )
SELECT
  flat.root_id,
  flat.id,
  flat.ts,
  flat.dur
FROM _intervals_flatten!(roots_and_merged_critical_path) AS flat
WHERE
  flat.dur > 0
GROUP BY
  flat.root_id,
  flat.ts;

CREATE PERFETTO TABLE _critical_path_all AS
SELECT
  row_number() OVER (ORDER BY cr.ts) AS id,
  cr.ts,
  cr.dur,
  cr.ts + cr.dur AS ts_end,
  id_graph.utid,
  root_id_graph.utid AS root_utid
FROM _critical_path_roots_and_merged AS cr
JOIN _wakeup_graph AS id_graph
  ON cr.id = id_graph.id
JOIN _wakeup_graph AS root_id_graph
  ON cr.root_id = root_id_graph.id
ORDER BY
  cr.ts;

-- Limited thread_state view that will later be span joined with the |_thread_executing_span_graph|.
CREATE PERFETTO VIEW _span_thread_state_view AS
SELECT
  id AS thread_state_id,
  ts,
  dur,
  utid,
  state,
  blocked_function AS function,
  io_wait,
  cpu
FROM thread_state;

-- Limited slice_view that will later be span joined with the critical path.
CREATE PERFETTO VIEW _span_slice_view AS
SELECT
  slice_id,
  depth AS slice_depth,
  name AS slice_name,
  cast_int!(ts) AS ts,
  cast_int!(dur) AS dur,
  utid
FROM _slice_flattened;

-- thread state span joined with slice.
CREATE VIRTUAL TABLE _span_thread_state_slice_sp USING SPAN_LEFT_JOIN (
    _span_thread_state_view PARTITIONED utid,
    _span_slice_view PARTITIONED utid);

CREATE PERFETTO TABLE _span_thread_state_slice AS
SELECT
  row_number() OVER (ORDER BY ts) AS id,
  ts,
  dur,
  ts + dur AS ts_end,
  utid,
  thread_state_id,
  state,
  function,
  cpu,
  io_wait,
  slice_id,
  slice_name,
  slice_depth
FROM _span_thread_state_slice_sp
WHERE
  dur > 0
ORDER BY
  ts;

CREATE PERFETTO TABLE _critical_path_thread_state_slice_raw AS
SELECT
  id_0 AS cr_id,
  id_1 AS th_id,
  ts,
  dur
FROM _interval_intersect!((_critical_path_all, _span_thread_state_slice), (utid));

CREATE PERFETTO TABLE _critical_path_thread_state_slice AS
SELECT
  raw.ts,
  raw.dur,
  cr.utid,
  thread_state_id,
  state,
  function,
  cpu,
  io_wait,
  slice_id,
  slice_name,
  slice_depth,
  root_utid
FROM _critical_path_thread_state_slice_raw AS raw
JOIN _critical_path_all AS cr
  ON cr.id = raw.cr_id
JOIN _span_thread_state_slice AS th
  ON th.id = raw.th_id;

-- Flattened slices span joined with their thread_states. This contains the 'self' information
-- without 'critical_path' (blocking) information.
CREATE VIRTUAL TABLE _self_sp USING SPAN_LEFT_JOIN (thread_state PARTITIONED utid, _slice_flattened PARTITIONED utid);

-- Limited view of |_self_sp|.
CREATE PERFETTO VIEW _self_view AS
SELECT
  id AS self_thread_state_id,
  slice_id AS self_slice_id,
  ts,
  dur,
  utid AS root_utid,
  state AS self_state,
  blocked_function AS self_function,
  cpu AS self_cpu,
  io_wait AS self_io_wait,
  name AS self_slice_name,
  depth AS self_slice_depth
FROM _self_sp;

-- Self and critical path span join. This contains the union of the time intervals from the following:
--  a. Self slice stack + thread_state.
--  b. Critical path stack + thread_state.
CREATE VIRTUAL TABLE _self_and_critical_path_sp USING SPAN_JOIN (
    _self_view PARTITIONED root_utid,
    _critical_path_thread_state_slice PARTITIONED root_utid);

-- Returns a view of |_self_and_critical_path_sp| unpivoted over the following columns:
-- self thread_state.
-- self blocked_function (if one exists).
-- self process_name (enabled with |enable_process_name|).
-- self thread_name (enabled with |enable_thread_name|).
-- self slice_stack (enabled with |enable_self_slice|).
-- critical_path thread_state.
-- critical_path process_name.
-- critical_path thread_name.
-- critical_path slice_stack (enabled with |enable_critical_path_slice|).
-- running cpu (if one exists).
-- A 'stack' is the group of resulting unpivoted rows sharing the same timestamp.
CREATE PERFETTO FUNCTION _critical_path_stack(
    root_utid JOINID(thread.id),
    ts TIMESTAMP,
    dur DURATION,
    enable_process_name LONG,
    enable_thread_name LONG,
    enable_self_slice LONG,
    enable_critical_path_slice LONG
)
RETURNS TABLE (
  id LONG,
  ts TIMESTAMP,
  dur DURATION,
  utid JOINID(thread.id),
  stack_depth LONG,
  name STRING,
  table_name STRING,
  root_utid JOINID(thread.id)
) AS
-- Spans filtered to the query time window and root_utid.
-- This is a preliminary step that gets the start and end ts of all the rows
-- so that we can chop the ends of each interval correctly if it overlaps with the query time interval.
WITH
  relevant_spans_starts AS (
    SELECT
      self_thread_state_id,
      self_state,
      self_slice_id,
      self_slice_name,
      self_slice_depth,
      self_function,
      self_io_wait,
      thread_state_id,
      state,
      function,
      io_wait,
      slice_id,
      slice_name,
      slice_depth,
      cpu,
      utid,
      max(ts, $ts) AS ts,
      min(ts + dur, $ts + $dur) AS end_ts,
      root_utid
    FROM _self_and_critical_path_sp
    WHERE
      dur > 0 AND root_utid = $root_utid
  ),
  -- This is the final step that gets the |dur| of each span from the start and
  -- and end ts of the previous step.
  -- Now we manually unpivot the result with 3 key steps: 1) Self 2) Critical path 3) CPU
  -- This CTE is heavily used throughout the entire function so materializing it is
  -- very important.
  relevant_spans AS MATERIALIZED (
    SELECT
      self_thread_state_id,
      self_state,
      self_slice_id,
      self_slice_name,
      self_slice_depth,
      self_function,
      self_io_wait,
      thread_state_id,
      state,
      function,
      io_wait,
      slice_id,
      slice_name,
      slice_depth,
      cpu,
      utid,
      ts,
      end_ts - ts AS dur,
      root_utid,
      utid
    FROM relevant_spans_starts
    WHERE
      dur > 0
  ),
  -- 1. Builds the 'self' stack of items as an ordered UNION ALL
  self_stack AS MATERIALIZED (
    -- Builds the self thread_state
    SELECT
      self_thread_state_id AS id,
      ts,
      dur,
      root_utid AS utid,
      0 AS stack_depth,
      'thread_state: ' || self_state AS name,
      'thread_state' AS table_name,
      root_utid
    FROM relevant_spans
    UNION ALL
    -- Builds the self kernel blocked_function
    SELECT
      self_thread_state_id AS id,
      ts,
      dur,
      root_utid AS utid,
      1 AS stack_depth,
      iif(self_state GLOB 'R*', NULL, 'kernel function: ' || self_function) AS name,
      'thread_state' AS table_name,
      root_utid
    FROM relevant_spans
    UNION ALL
    -- Builds the self kernel io_wait
    SELECT
      self_thread_state_id AS id,
      ts,
      dur,
      root_utid AS utid,
      2 AS stack_depth,
      iif(self_state GLOB 'R*', NULL, 'io_wait: ' || self_io_wait) AS name,
      'thread_state' AS table_name,
      root_utid
    FROM relevant_spans
    UNION ALL
    -- Builds the self process_name
    SELECT
      self_thread_state_id AS id,
      ts,
      dur,
      thread.utid,
      3 AS stack_depth,
      iif($enable_process_name, 'process_name: ' || process.name, NULL) AS name,
      'thread_state' AS table_name,
      root_utid
    FROM relevant_spans
    LEFT JOIN thread
      ON thread.utid = root_utid
    LEFT JOIN process
      USING (upid)
    -- Builds the self thread_name
    UNION ALL
    SELECT
      self_thread_state_id AS id,
      ts,
      dur,
      thread.utid,
      4 AS stack_depth,
      iif($enable_thread_name, 'thread_name: ' || thread.name, NULL) AS name,
      'thread_state' AS table_name,
      root_utid
    FROM relevant_spans
    LEFT JOIN thread
      ON thread.utid = root_utid
    JOIN process
      USING (upid)
    UNION ALL
    -- Builds the self 'ancestor' slice stack
    SELECT
      anc.id,
      slice.ts,
      slice.dur,
      root_utid AS utid,
      anc.depth + 5 AS stack_depth,
      iif($enable_self_slice, anc.name, NULL) AS name,
      'slice' AS table_name,
      root_utid
    FROM relevant_spans AS slice, ancestor_slice(self_slice_id) AS anc
    WHERE
      anc.dur != -1
    UNION ALL
    -- Builds the self 'deepest' ancestor slice stack
    SELECT
      self_slice_id AS id,
      ts,
      dur,
      root_utid AS utid,
      self_slice_depth + 5 AS stack_depth,
      iif($enable_self_slice, self_slice_name, NULL) AS name,
      'slice' AS table_name,
      root_utid
    FROM relevant_spans AS slice
    ORDER BY
      stack_depth
  ),
  -- Prepares for stage 2 in building the entire stack.
  -- Computes the starting depth for each stack. This is necessary because
  -- each self slice stack has variable depth and the depth in each stack
  -- most be contiguous in order to efficiently generate a pprof in the future.
  critical_path_start_depth AS MATERIALIZED (
    SELECT
      root_utid,
      ts,
      max(stack_depth) + 1 AS start_depth
    FROM self_stack
    GROUP BY
      root_utid,
      ts
  ),
  critical_path_span AS MATERIALIZED (
    SELECT
      thread_state_id,
      state,
      function,
      io_wait,
      slice_id,
      slice_name,
      slice_depth,
      spans.ts,
      spans.dur,
      spans.root_utid,
      utid,
      start_depth
    FROM relevant_spans AS spans
    JOIN critical_path_start_depth
      ON critical_path_start_depth.root_utid = spans.root_utid
      AND critical_path_start_depth.ts = spans.ts
    WHERE
      critical_path_start_depth.root_utid = $root_utid
      AND spans.root_utid != spans.utid
  ),
  -- 2. Builds the 'critical_path' stack of items as an ordered UNION ALL
  critical_path_stack AS MATERIALIZED (
    -- Builds the critical_path thread_state
    SELECT
      thread_state_id AS id,
      ts,
      dur,
      utid,
      start_depth AS stack_depth,
      'blocking thread_state: ' || state AS name,
      'thread_state' AS table_name,
      root_utid
    FROM critical_path_span
    UNION ALL
    -- Builds the critical_path process_name
    SELECT
      thread_state_id AS id,
      ts,
      dur,
      thread.utid,
      start_depth + 1 AS stack_depth,
      'blocking process_name: ' || process.name,
      'thread_state' AS table_name,
      root_utid
    FROM critical_path_span
    JOIN thread
      USING (utid)
    LEFT JOIN process
      USING (upid)
    UNION ALL
    -- Builds the critical_path thread_name
    SELECT
      thread_state_id AS id,
      ts,
      dur,
      thread.utid,
      start_depth + 2 AS stack_depth,
      'blocking thread_name: ' || thread.name,
      'thread_state' AS table_name,
      root_utid
    FROM critical_path_span
    JOIN thread
      USING (utid)
    UNION ALL
    -- Builds the critical_path kernel blocked_function
    SELECT
      thread_state_id AS id,
      ts,
      dur,
      thread.utid,
      start_depth + 3 AS stack_depth,
      'blocking kernel_function: ' || function,
      'thread_state' AS table_name,
      root_utid
    FROM critical_path_span
    JOIN thread
      USING (utid)
    UNION ALL
    -- Builds the critical_path kernel io_wait
    SELECT
      thread_state_id AS id,
      ts,
      dur,
      thread.utid,
      start_depth + 4 AS stack_depth,
      'blocking io_wait: ' || io_wait,
      'thread_state' AS table_name,
      root_utid
    FROM critical_path_span
    JOIN thread
      USING (utid)
    UNION ALL
    -- Builds the critical_path 'ancestor' slice stack
    SELECT
      anc.id,
      slice.ts,
      slice.dur,
      slice.utid,
      anc.depth + start_depth + 5 AS stack_depth,
      iif($enable_critical_path_slice, anc.name, NULL) AS name,
      'slice' AS table_name,
      root_utid
    FROM critical_path_span AS slice, ancestor_slice(slice_id) AS anc
    WHERE
      anc.dur != -1
    UNION ALL
    -- Builds the critical_path 'deepest' slice
    SELECT
      slice_id AS id,
      ts,
      dur,
      utid,
      slice_depth + start_depth + 5 AS stack_depth,
      iif($enable_critical_path_slice, slice_name, NULL) AS name,
      'slice' AS table_name,
      root_utid
    FROM critical_path_span AS slice
    ORDER BY
      stack_depth
  ),
  -- Prepares for stage 3 in building the entire stack.
  -- Computes the starting depth for each stack using the deepest stack_depth between
  -- the critical_path stack and self stack. The self stack depth is
  -- already computed and materialized in |critical_path_start_depth|.
  cpu_start_depth_raw AS (
    SELECT
      root_utid,
      ts,
      max(stack_depth) + 1 AS start_depth
    FROM critical_path_stack
    GROUP BY
      root_utid,
      ts
    UNION ALL
    SELECT
      *
    FROM critical_path_start_depth
  ),
  cpu_start_depth AS (
    SELECT
      root_utid,
      ts,
      max(start_depth) AS start_depth
    FROM cpu_start_depth_raw
    GROUP BY
      root_utid,
      ts
  ),
  -- 3. Builds the 'CPU' stack for 'Running' states in either the self or critical path stack.
  cpu_stack AS (
    SELECT
      thread_state_id AS id,
      spans.ts,
      spans.dur,
      utid,
      start_depth AS stack_depth,
      'cpu: ' || cpu AS name,
      'thread_state' AS table_name,
      spans.root_utid
    FROM relevant_spans AS spans
    JOIN cpu_start_depth
      ON cpu_start_depth.root_utid = spans.root_utid AND cpu_start_depth.ts = spans.ts
    WHERE
      cpu_start_depth.root_utid = $root_utid
      AND state = 'Running'
      OR self_state = 'Running'
  ),
  merged AS (
    SELECT
      *
    FROM self_stack
    UNION ALL
    SELECT
      *
    FROM critical_path_stack
    UNION ALL
    SELECT
      *
    FROM cpu_stack
  )
SELECT
  *
FROM merged
WHERE
  id IS NOT NULL;

-- Critical path stack of thread_executing_spans with the following entities in the critical path
-- stacked from top to bottom: self thread_state, self blocked_function, self process_name,
-- self thread_name, slice stack, critical_path thread_state, critical_path process_name,
-- critical_path thread_name, critical_path slice_stack, running_cpu.
CREATE PERFETTO FUNCTION _thread_executing_span_critical_path_stack(
    -- Thread utid to filter critical paths to.
    root_utid JOINID(thread.id),
    -- Timestamp of start of time range to filter critical paths to.
    ts TIMESTAMP,
    -- Duration of time range to filter critical paths to.
    dur DURATION
)
RETURNS TABLE (
  -- Id of the thread_state or slice in the thread_executing_span.
  id LONG,
  -- Timestamp of slice in the critical path.
  ts TIMESTAMP,
  -- Duration of slice in the critical path.
  dur DURATION,
  -- Utid of thread that emitted the slice.
  utid JOINID(thread.id),
  -- Stack depth of the entitity in the debug track.
  stack_depth LONG,
  -- Name of entity in the critical path (could be a thread_state, kernel blocked_function, process_name, thread_name, slice name or cpu).
  name STRING,
  -- Table name of entity in the critical path (could be either slice or thread_state).
  table_name STRING,
  -- Utid of the thread the critical path was filtered to.
  root_utid JOINID(thread.id)
) AS
SELECT
  *
FROM _critical_path_stack($root_utid, $ts, $dur, 1, 1, 1, 1);

-- Returns a pprof aggregation of the stacks in |_critical_path_stack|.
CREATE PERFETTO FUNCTION _critical_path_graph(
    graph_title STRING,
    root_utid JOINID(thread.id),
    ts TIMESTAMP,
    dur DURATION,
    enable_process_name LONG,
    enable_thread_name LONG,
    enable_self_slice LONG,
    enable_critical_path_slice LONG
)
RETURNS TABLE (
  pprof BYTES
) AS
WITH
  stack AS MATERIALIZED (
    SELECT
      ts,
      dur - coalesce(lead(dur) OVER (PARTITION BY root_utid, ts ORDER BY stack_depth), 0) AS dur,
      name,
      utid,
      root_utid,
      stack_depth
    FROM _critical_path_stack(
      $root_utid,
      $ts,
      $dur,
      $enable_process_name,
      $enable_thread_name,
      $enable_self_slice,
      $enable_critical_path_slice
    )
  ),
  graph AS (
    SELECT
      cat_stacks($graph_title) AS stack
  ),
  parent AS (
    SELECT
      cr.ts,
      cr.dur,
      cr.name,
      cr.utid,
      cr.stack_depth,
      cat_stacks(graph.stack, cr.name) AS stack,
      cr.root_utid
    FROM stack AS cr, graph
    WHERE
      stack_depth = 0
    UNION ALL
    SELECT
      child.ts,
      child.dur,
      child.name,
      child.utid,
      child.stack_depth,
      cat_stacks(stack, child.name) AS stack,
      child.root_utid
    FROM stack AS child
    JOIN parent
      ON parent.root_utid = child.root_utid
      AND parent.ts = child.ts
      AND child.stack_depth = parent.stack_depth + 1
  ),
  stacks AS (
    SELECT
      dur,
      stack
    FROM parent
  )
SELECT
  experimental_profile(stack, 'duration', 'ns', dur) AS pprof
FROM stacks;

-- Returns a pprof aggreagation of the stacks in |_thread_executing_span_critical_path_stack|
CREATE PERFETTO FUNCTION _thread_executing_span_critical_path_graph(
    -- Descriptive name for the graph.
    graph_title STRING,
    -- Thread utid to filter critical paths to.
    root_utid JOINID(thread.id),
    -- Timestamp of start of time range to filter critical paths to.
    ts TIMESTAMP,
    -- Duration of time range to filter critical paths to.
    dur DURATION
)
RETURNS TABLE (
  -- Pprof of critical path stacks.
  pprof BYTES
) AS
SELECT
  *
FROM _critical_path_graph($graph_title, $root_utid, $ts, $dur, 1, 1, 1, 1);
