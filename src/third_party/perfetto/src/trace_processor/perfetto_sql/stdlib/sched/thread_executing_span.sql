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

INCLUDE PERFETTO MODULE graphs.critical_path;

INCLUDE PERFETTO MODULE intervals.overlap;

INCLUDE PERFETTO MODULE intervals.intersect;

-- A 'thread_executing_span' is thread_state span starting with a runnable slice
-- until the next runnable slice that's woken up by a process (as opposed
-- to an interrupt). Note that within a 'thread_executing_span' we can have sleep
-- spans blocked on an interrupt.
-- We consider the id of this span to be the id of the first thread_state in the span.

--
-- Finds all runnable states that are woken up by a process.
--
-- We achieve this by checking that the |thread_state.irq_context|
-- value is NOT 1. In otherwords, it is either 0 or NULL. The NULL check
-- is important to support older Android versions.
--
-- On older versions of Android (<U). We don't have IRQ context information,
-- so this table might contain wakeups from interrupt context, consequently, the
-- wakeup graph generated might not be accurate.
--
CREATE PERFETTO TABLE _runnable_state AS
SELECT
  thread_state.id,
  thread_state.ts,
  thread_state.dur,
  thread_state.state,
  thread_state.utid,
  thread_state.waker_id,
  thread_state.waker_utid,
  iif(
    thread_state.irq_context = 0 OR thread_state.irq_context IS NULL,
    coalesce(thread_state.io_wait, 0),
    1
  ) AS is_irq
FROM thread_state
WHERE
  thread_state.dur != -1 AND thread_state.waker_id IS NOT NULL;

-- Similar to |_runnable_state| but finds the first runnable state at thread.
CREATE PERFETTO TABLE _first_runnable_state AS
WITH
  first_state AS (
    SELECT
      min(thread_state.id) AS id
    FROM thread_state
    GROUP BY
      utid
  )
SELECT
  thread_state.id,
  thread_state.ts,
  thread_state.dur,
  thread_state.state,
  thread_state.utid,
  thread_state.waker_id,
  thread_state.waker_utid,
  iif(
    thread_state.irq_context = 0 OR thread_state.irq_context IS NULL,
    coalesce(thread_state.io_wait, 0),
    1
  ) AS is_irq
FROM thread_state
JOIN first_state
  USING (id)
WHERE
  thread_state.dur != -1 AND thread_state.state = 'R';

--
-- Finds all sleep states including interruptible (S) and uninterruptible (D).
CREATE PERFETTO TABLE _sleep_state AS
SELECT
  thread_state.id,
  thread_state.ts,
  thread_state.dur,
  thread_state.state,
  thread_state.blocked_function,
  thread_state.utid
FROM thread_state
WHERE
  dur != -1 AND (
    state = 'S' OR state = 'D' OR state = 'I'
  );

--
-- Finds the last execution for every thread to end executing_spans without a Sleep.
--
CREATE PERFETTO TABLE _thread_end_ts AS
SELECT
  max(ts) + dur AS end_ts,
  utid
FROM thread_state
WHERE
  dur != -1
GROUP BY
  utid;

-- Similar to |_sleep_state| but finds the first sleep state in a thread.
CREATE PERFETTO TABLE _first_sleep_state AS
SELECT
  min(s.id) AS id,
  s.ts,
  s.dur,
  s.state,
  s.blocked_function,
  s.utid
FROM _sleep_state AS s
JOIN _runnable_state AS r
  ON s.utid = r.utid AND (
    s.ts + s.dur = r.ts
  )
GROUP BY
  s.utid;

--
-- Finds all neighbouring ('Sleeping', 'Runnable') thread_states pairs from the same thread.
-- More succintly, pairs of S[n-1]-R[n] where R is woken by a process context and S is an
-- interruptible or uninterruptible sleep state.
--
-- This is achieved by joining the |_runnable_state|.ts with the
-- |_sleep_state|.|ts + dur|.
--
-- With the S-R pairs of a thread, we can re-align to [R-S) intervals with LEADS and LAGS.
--
-- Given the following thread_states on a thread:
-- S0__|R0__Running0___|S1__|R1__Running1___|S2__|R2__Running2__S2|.
--
-- We have 3 thread_executing_spans: [R0, S0), [R1, S1), [R2, S2).
--
-- We define the following markers in this table:
--
-- prev_id          = R0_id.
--
-- prev_end_ts      = S0_ts.
-- state            = 'S' or 'D'.
-- blocked_function = <kernel blocking function>
--
-- id               = R1_id.
-- ts               = R1_ts.
--
-- end_ts           = S1_ts.
CREATE PERFETTO TABLE _wakeup AS
WITH
  all_wakeups AS (
    SELECT
      s.state,
      s.blocked_function,
      r.id,
      r.ts AS ts,
      r.utid AS utid,
      r.waker_id,
      r.waker_utid,
      s.ts AS prev_end_ts,
      is_irq
    FROM _runnable_state AS r
    JOIN _sleep_state AS s
      ON s.utid = r.utid AND (
        s.ts + s.dur = r.ts
      )
    UNION ALL
    SELECT
      NULL AS state,
      NULL AS blocked_function,
      r.id,
      r.ts,
      r.utid AS utid,
      r.waker_id,
      r.waker_utid,
      NULL AS prev_end_ts,
      is_irq
    FROM _first_runnable_state AS r
    LEFT JOIN _first_sleep_state AS s
      ON s.utid = r.utid
  )
SELECT
  all_wakeups.*,
  thread_end.end_ts AS thread_end_ts
FROM all_wakeups
LEFT JOIN _thread_end_ts AS thread_end
  USING (utid);

-- Mapping from running thread state to runnable
-- TODO(zezeozue): Switch to use `sched_previous_runnable_on_thread`.
CREATE PERFETTO TABLE _wakeup_map AS
WITH
  x AS (
    SELECT
      id,
      waker_id,
      utid,
      state
    FROM thread_state
    WHERE
      state = 'Running' AND dur != -1
    UNION ALL
    SELECT
      id,
      waker_id,
      utid,
      state
    FROM _first_runnable_state
    UNION ALL
    SELECT
      id,
      waker_id,
      utid,
      state
    FROM _runnable_state
  ),
  y AS (
    SELECT
      id AS waker_id,
      state,
      max(id) FILTER(WHERE
        state = 'R') OVER (PARTITION BY utid ORDER BY id) AS id
    FROM x
  )
SELECT
  id,
  waker_id
FROM y
WHERE
  state = 'Running'
ORDER BY
  waker_id;

--
-- Builds the waker and prev relationships for all thread_executing_spans.
--
CREATE PERFETTO TABLE _wakeup_graph AS
WITH
  _wakeup_events AS (
    SELECT
      utid,
      thread_end_ts,
      iif(is_irq, 'IRQ', state) AS idle_state,
      blocked_function AS idle_reason,
      _wakeup.id,
      iif(is_irq, NULL, _wakeup_map.id) AS waker_id,
      _wakeup.ts,
      prev_end_ts AS idle_ts,
      iif(is_irq OR _wakeup_map.id IS NULL OR (
        NOT state IS NULL AND state != 'S'
      ), 1, 0) AS is_idle_reason_self
    FROM _wakeup
    LEFT JOIN _wakeup_map
      USING (waker_id)
  )
SELECT
  utid,
  id,
  waker_id,
  ts,
  idle_state,
  idle_reason,
  ts - idle_ts AS idle_dur,
  is_idle_reason_self,
  lag(id) OVER (PARTITION BY utid ORDER BY ts) AS prev_id,
  lead(id) OVER (PARTITION BY utid ORDER BY ts) AS next_id,
  coalesce(lead(idle_ts) OVER (PARTITION BY utid ORDER BY ts), thread_end_ts) - ts AS dur,
  lead(is_idle_reason_self) OVER (PARTITION BY utid ORDER BY ts) AS is_next_idle_reason_self
FROM _wakeup_events
ORDER BY
  id;

-- View of all the edges for the userspace critical path.
CREATE PERFETTO VIEW _wakeup_userspace_edges AS
SELECT
  id AS source_node_id,
  coalesce(iif(is_idle_reason_self, prev_id, waker_id), id) AS dest_node_id,
  id - coalesce(iif(is_idle_reason_self, prev_id, waker_id), id) AS edge_weight
FROM _wakeup_graph;

-- View of all the edges for the kernel critical path.
CREATE PERFETTO VIEW _wakeup_kernel_edges AS
SELECT
  id AS source_node_id,
  coalesce(waker_id, id) AS dest_node_id,
  id - coalesce(waker_id, id) AS edge_weight
FROM _wakeup_graph;

-- View of the relevant timestamp and intervals for all nodes in the critical path.
CREATE PERFETTO VIEW _wakeup_intervals AS
SELECT
  id,
  ts,
  dur,
  idle_dur
FROM _wakeup_graph;

-- Converts a table with <ts, dur, utid> columns to a unique set of wakeup roots <id> that
-- completely cover the time intervals.
CREATE PERFETTO MACRO _intervals_to_roots(
    _source_table TableOrSubQuery,
    _node_table TableOrSubQuery
)
RETURNS TableOrSubQuery AS
(
  WITH
    _interval_to_root_nodes AS (
      SELECT
        *
      FROM $_node_table
    ),
    _source AS (
      SELECT
        *
      FROM $_source_table
    ),
    _thread_bounds AS (
      SELECT
        utid,
        min(ts) AS min_start,
        max(ts) AS max_start
      FROM _interval_to_root_nodes
      GROUP BY
        utid
    ),
    _start AS (
      SELECT
        _interval_to_root_nodes.utid,
        max(_interval_to_root_nodes.id) AS _start_id,
        _source.ts,
        _source.dur
      FROM _interval_to_root_nodes
      JOIN _thread_bounds
        USING (utid)
      JOIN _source
        ON _source.utid = _interval_to_root_nodes.utid
        AND max(_source.ts, min_start) >= _interval_to_root_nodes.ts
      GROUP BY
        _source.ts,
        _source.utid
    ),
    _end AS (
      SELECT
        _interval_to_root_nodes.utid,
        min(_interval_to_root_nodes.id) AS _end_id,
        _source.ts,
        _source.dur
      FROM _interval_to_root_nodes
      JOIN _thread_bounds
        USING (utid)
      JOIN _source
        ON _source.utid = _interval_to_root_nodes.utid
        AND min((
          _source.ts + _source.dur
        ), max_start) <= _interval_to_root_nodes.ts
      GROUP BY
        _source.ts,
        _source.utid
    ),
    _bound AS (
      SELECT
        _start.utid,
        _start.ts,
        _start.dur,
        _start_id,
        _end_id
      FROM _start
      JOIN _end
        ON _start.ts = _end.ts AND _start.dur = _end.dur AND _start.utid = _end.utid
    )
  SELECT DISTINCT
    id AS root_node_id,
    id - coalesce(prev_id, id) AS capacity
  FROM _bound
  JOIN _interval_to_root_nodes
    ON _interval_to_root_nodes.id BETWEEN _start_id AND _end_id
    AND _interval_to_root_nodes.utid = _bound.utid
);

-- Adjusts the userspace critical path such that any interval that includes a kernel stall
-- gets the next id, the root id of the kernel critical path. This ensures that the merge
-- step associates the userspace critical path and kernel critical path on the same interval
-- correctly.
CREATE PERFETTO MACRO _critical_path_userspace_adjusted(
    _critical_path_table TableOrSubQuery,
    _node_table TableOrSubQuery
)
RETURNS TableOrSubQuery AS
(
  SELECT
    cr.root_id,
    cr.root_id AS parent_id,
    iif(node.is_next_idle_reason_self, node.next_id, cr.id) AS id,
    cr.ts,
    cr.dur
  FROM (
    SELECT
      *
    FROM $_critical_path_table
  ) AS cr
  JOIN $_node_table AS node
    USING (id)
);

-- Adjusts the start and end of the kernel critical path such that it is completely bounded within
-- its corresponding userspace critical path.
CREATE PERFETTO MACRO _critical_path_kernel_adjusted(
    _userspace_critical_path_table TableOrSubQuery,
    _kernel_critical_path_table TableOrSubQuery,
    _node_table TableOrSubQuery
)
RETURNS TableOrSubQuery AS
(
  SELECT
    kernel_cr.root_id,
    kernel_cr.root_id AS parent_id,
    kernel_cr.id,
    max(kernel_cr.ts, userspace_cr.ts) AS ts,
    min(kernel_cr.ts + kernel_cr.dur, userspace_cr.ts + userspace_cr.dur) - max(kernel_cr.ts, userspace_cr.ts) AS dur
  FROM $_kernel_critical_path_table AS kernel_cr
  JOIN $_node_table AS node
    ON kernel_cr.parent_id = node.id
  JOIN $_userspace_critical_path_table AS userspace_cr
    ON userspace_cr.id = kernel_cr.parent_id
    AND userspace_cr.root_id = kernel_cr.root_id
);

-- Merge the kernel and userspace critical path such that the corresponding kernel critical path
-- has priority over userpsace critical path it overlaps.
CREATE PERFETTO MACRO _critical_path_merged(
    _userspace_critical_path_table TableOrSubQuery,
    _kernel_critical_path_table TableOrSubQuery,
    _node_table TableOrSubQuery
)
RETURNS TableOrSubQuery AS
(
  WITH
    _userspace_critical_path AS (
      SELECT DISTINCT
        *
      FROM _critical_path_userspace_adjusted!(
    $_userspace_critical_path_table,
    $_node_table)
    ),
    _merged_critical_path AS (
      SELECT
        *
      FROM _userspace_critical_path
      UNION ALL
      SELECT DISTINCT
        *
      FROM _critical_path_kernel_adjusted!(
      _userspace_critical_path,
      $_kernel_critical_path_table,
      $_node_table)
      WHERE
        id != parent_id
    ),
    _roots_critical_path AS (
      SELECT
        root_id,
        min(ts) AS root_ts,
        max(ts + dur) - min(ts) AS root_dur
      FROM _userspace_critical_path
      GROUP BY
        root_id
    ),
    _roots_and_merged_critical_path AS (
      SELECT
        root_id,
        root_ts,
        root_dur,
        parent_id,
        id,
        ts,
        dur
      FROM _merged_critical_path
      JOIN _roots_critical_path
        USING (root_id)
    )
  SELECT
    flat.root_id,
    flat.id,
    flat.ts,
    flat.dur
  FROM _intervals_flatten!(_roots_and_merged_critical_path) AS flat
  WHERE
    flat.dur > 0
  GROUP BY
    flat.root_id,
    flat.ts
);

-- Generates the critical path for only the set of roots <id> passed in.
-- _intervals_to_roots can be used to generate root ids from a given time interval.
-- This can be used to genrate the critical path over sparse regions of a trace, e.g
-- binder transactions. It might be more efficient to generate the _critical_path
-- for the entire trace, see _thread_executing_span_critical_path_all, but for a
-- per-process susbset of binder txns for instance, this is likely faster.
CREATE PERFETTO MACRO _critical_path_by_roots(
    _roots_table TableOrSubQuery,
    _node_table TableOrSubQuery
)
RETURNS TableOrSubQuery AS
(
  WITH
    _userspace_critical_path_by_roots AS (
      SELECT
        *
      FROM _critical_path_intervals
        !(_wakeup_userspace_edges,
          $_roots_table,
          _wakeup_intervals)
    ),
    _kernel_nodes AS (
      SELECT
        id,
        root_id
      FROM _userspace_critical_path_by_roots
      JOIN $_node_table AS node
        USING (id)
      WHERE
        is_idle_reason_self = 1
    ),
    _kernel_critical_path_by_roots AS (
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
    )
  SELECT
    *
  FROM _critical_path_merged!(
    _userspace_critical_path_by_roots,
    _kernel_critical_path_by_roots,
    $_node_table)
);

-- Generates the critical path for only the time intervals for the utids given.
-- Currently expensive because of naive interval_intersect implementation.
-- Prefer _critical_paths_by_roots for performance. This is useful for a small
-- set of intervals, e.g app startups in a trace.
CREATE PERFETTO MACRO _critical_path_by_intervals(
    _intervals_table TableOrSubQuery,
    _node_table TableOrSubQuery
)
RETURNS TableOrSubQuery AS
(
  WITH
    _nodes AS (
      SELECT
        *
      FROM $_node_table
    ),
    _intervals AS (
      SELECT
        row_number() OVER (ORDER BY ts) AS id,
        ts,
        dur,
        utid AS root_utid
      FROM $_intervals_table
    ),
    _critical_path AS (
      SELECT
        row_number() OVER (ORDER BY ts) AS id,
        root_id,
        id AS cr_id,
        ts,
        dur
      FROM _critical_path_by_roots!(
      _intervals_to_roots!($_intervals_table, $_node_table),
      _nodes)
    ),
    _span AS (
      SELECT
        _root_nodes.utid AS root_utid,
        _nodes.utid,
        cr.root_id,
        cr.cr_id,
        cr.id,
        cr.ts,
        cr.dur
      FROM _critical_path AS cr
      JOIN _nodes AS _root_nodes
        ON _root_nodes.id = cr.root_id
      JOIN _nodes
        ON _nodes.id = cr.cr_id
    )
  SELECT DISTINCT
    _span.root_utid,
    _span.utid,
    _span.root_id,
    _span.cr_id AS id,
    ii.ts,
    ii.dur,
    _intervals.ts AS interval_ts,
    _intervals.dur AS interval_dur
  FROM _interval_intersect!((_span, _intervals), (root_utid)) AS ii
  JOIN _span
    ON _span.id = ii.id_0
  JOIN _intervals
    ON _intervals.id = ii.id_1
);

-- Generates the critical path for a given utid over the <ts, dur> interval.
-- The duration of a thread executing span in the critical path is the range between the
-- start of the thread_executing_span and the start of the next span in the critical path.
CREATE PERFETTO FUNCTION _thread_executing_span_critical_path(
    -- Utid of the thread to compute the critical path for.
    root_utid JOINID(thread.id),
    -- Timestamp.
    ts TIMESTAMP,
    -- Duration.
    dur DURATION
)
RETURNS TABLE (
  -- Thread Utid the critical path was filtered to.
  root_utid JOINID(thread.id),
  -- Id of thread executing span following the sleeping thread state for which the critical path is
  -- computed.
  root_id LONG,
  -- Id of the first (runnable) thread state in thread_executing_span.
  id LONG,
  -- Timestamp of first thread_state in thread_executing_span.
  ts TIMESTAMP,
  -- Duration of thread_executing_span.
  dur DURATION,
  -- Utid of thread with thread_state.
  utid JOINID(thread.id)
) AS
SELECT
  root_utid,
  root_id,
  id,
  ts,
  dur,
  utid
FROM _critical_path_by_intervals!(
  (SELECT $root_utid AS utid, $ts as ts, $dur AS dur),
  _wakeup_graph);
