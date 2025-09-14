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

-- Returns an instance of `RawMarkerTable` as defined in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
CREATE PERFETTO FUNCTION _export_firefox_thread_markers()
RETURNS STRING AS
SELECT
  json_object(
    'data', json_array(),
    'name', json_array(),
    'startTime', json_array(),
    'endTime', json_array(),
    'phase', json_array(),
    'category', json_array(),
    -- threadId?: Tid[]
    'length', 0
  );

-- Returns an empty instance of `NativeSymbolTable` as defined in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
CREATE PERFETTO FUNCTION _export_firefox_native_symbol_table()
RETURNS STRING AS
SELECT
  json_object(
    'libIndex', json_array(),
    'address', json_array(),
    'name', json_array(),
    'functionSize', json_array(),
    'length', 0
  );

-- Returns an empty instance of `ResourceTable` as defined in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
CREATE PERFETTO FUNCTION _export_firefox_resource_table()
RETURNS STRING AS
SELECT
  json_object(
    'length', 0,
    'lib', json_array(),
    'name', json_array(),
    'host', json_array(),
    'type', json_array()
  );

-- Materialize this intermediate table and sort by `callsite_id` to speedup the
-- generation of the stack_table further down.
CREATE PERFETTO TABLE _export_to_firefox_table AS
WITH
  symbol AS (
    SELECT
      symbol_set_id,
      rank() OVER (PARTITION BY symbol_set_id ORDER BY id DESC RANGE BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING) - 1 AS inline_depth,
      count() OVER (PARTITION BY symbol_set_id) - 1 AS max_inline_depth,
      name
    FROM stack_profile_symbol
  ),
  callsite_base AS (
    SELECT
      id,
      parent_id,
      name,
      symbol_set_id,
      iif(inline_count = 0, 0, inline_count - 1) AS max_inline_depth
    FROM (
      SELECT
        spc.id,
        spc.parent_id,
        coalesce(s.name, spf.name, '') AS name,
        sfp.symbol_set_id,
        (
          SELECT
            count(*)
          FROM stack_profile_symbol AS s
          WHERE
            s.symbol_set_id = sfp.symbol_set_id
        ) AS inline_count
      FROM stack_profile_callsite AS spc
      JOIN stack_profile_frame AS spf
        ON (
          spc.frame_id = spf.id
        )
    )
  ),
  callsite_recursive AS (
    SELECT
      s.utid,
      spc.id,
      spc.parent_id,
      spc.frame_id
    FROM (
      SELECT DISTINCT
        callsite_id,
        utid
      FROM perf_sample
    ) AS s
    JOIN stack_profile_callsite AS spc
      ON spc.id = s.callsite_id
    UNION ALL
    SELECT
      child.utid,
      parent.id,
      parent.parent_id,
      parent.frame_id
    FROM callsite_recursive AS child
    JOIN stack_profile_callsite AS parent
      ON child.parent_id = parent.id
  ),
  unique_callsite AS (
    SELECT DISTINCT
      *
    FROM callsite_recursive
  ),
  expanded_callsite AS (
    SELECT
      c.utid,
      c.id,
      c.parent_id,
      c.frame_id,
      coalesce(s.name, spf.name, '') AS name,
      coalesce(s.inline_depth, 0) AS inline_depth,
      coalesce(s.max_inline_depth, 0) AS max_inline_depth
    FROM unique_callsite AS c
    JOIN stack_profile_frame AS spf
      ON (
        c.frame_id = spf.id
      )
    LEFT JOIN symbol AS s
      USING (symbol_set_id)
  )
SELECT
  utid,
  id AS callsite_id,
  parent_id AS parent_callsite_id,
  name,
  inline_depth,
  inline_depth = max_inline_depth AS is_most_inlined,
  dense_rank() OVER (PARTITION BY utid ORDER BY id, inline_depth) - 1 AS stack_table_index,
  dense_rank() OVER (PARTITION BY utid ORDER BY frame_id, inline_depth) - 1 AS frame_table_index,
  dense_rank() OVER (PARTITION BY utid ORDER BY name) - 1 AS func_table_index,
  dense_rank() OVER (PARTITION BY utid ORDER BY name) - 1 AS string_table_index
FROM expanded_callsite
ORDER BY
  utid,
  id;

-- Returns an instance of `SamplesTable` as defined in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
-- for the given `utid`.
CREATE PERFETTO FUNCTION _export_firefox_samples_table(
    utid JOINID(thread.id)
)
RETURNS STRING AS
WITH
  samples_table AS (
    SELECT
      row_number() OVER (ORDER BY s.id) - 1 AS idx,
      s.ts AS time,
      t.stack_table_index AS stack
    FROM perf_sample AS s
    JOIN _export_to_firefox_table AS t
      USING (utid, callsite_id)
    WHERE
      utid = $utid AND t.is_most_inlined
  )
SELECT
  json_object(
    -- responsiveness?: Array<?Milliseconds>
    -- eventDelay?: Array<?Milliseconds>
    'stack', json_group_array(stack ORDER BY idx),
    'time', json_group_array(time ORDER BY idx),
    'weight', NULL,
    'weightType', 'samples',
    -- threadCPUDelta?: Array<number | null>
    -- threadId?: Tid[]
    'length', count(*)
  )
FROM samples_table;

-- Returns an instance of `StackTable` as defined in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
-- for the given `utid`.
CREATE PERFETTO FUNCTION _export_firefox_stack_table(
    utid JOINID(thread.id)
)
RETURNS STRING AS
WITH
  parent AS (
    SELECT
      *
    FROM _export_to_firefox_table
    WHERE
      utid = $utid
  ),
  stack_table AS (
    SELECT
      stack_table_index AS idx,
      frame_table_index AS frame,
      0 AS category,
      0 AS subcategory,
      -- It is key that this lookup is fast. That is why we have materialized
      -- the `_export_to_firefox_table` table and sorted it by `utid` and
      -- `callsite_id`.
      iif(
        child.inline_depth = 0,
        (
          SELECT
            stack_table_index
          FROM parent
          WHERE
            child.parent_callsite_id = parent.callsite_id AND parent.is_most_inlined
        ),
        (
          SELECT
            stack_table_index
          FROM parent
          WHERE
            child.callsite_id = parent.callsite_id
            AND child.inline_depth - 1 = parent.inline_depth
        )
      ) AS prefix
    FROM _export_to_firefox_table AS child
    WHERE
      child.utid = $utid
  )
SELECT
  json_object(
    'frame', json_group_array(frame ORDER BY idx),
    'category', json_group_array(category ORDER BY idx),
    'subcategory', json_group_array(subcategory ORDER BY idx),
    'prefix', json_group_array(prefix ORDER BY idx),
    'length', count(*)
  )
FROM stack_table;

-- Returns an instance of `FrameTable` as defined in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
-- for the given `utid`.
CREATE PERFETTO FUNCTION _export_firefox_frame_table(
    utid JOINID(thread.id)
)
RETURNS STRING AS
WITH
  frame_table AS (
    SELECT DISTINCT
      frame_table_index AS idx,
      -1 AS address,
      inline_depth,
      0 AS category,
      0 AS subcategory,
      func_table_index AS func,
      NULL AS native_symbol,
      NULL AS inner_window_id,
      NULL AS implementation,
      NULL AS line,
      NULL AS column
    FROM _export_to_firefox_table
    WHERE
      utid = $utid
  )
SELECT
  json_object(
    'address', json_group_array(address ORDER BY idx),
    'inlineDepth', json_group_array(inline_depth ORDER BY idx),
    'category', json_group_array(category ORDER BY idx),
    'subcategory', json_group_array(subcategory ORDER BY idx),
    'func', json_group_array(func ORDER BY idx),
    'nativeSymbol', json_group_array(native_symbol ORDER BY idx),
    'innerWindowID', json_group_array(inner_window_id ORDER BY idx),
    'implementation', json_group_array(implementation ORDER BY idx),
    'line', json_group_array(line ORDER BY idx),
    'column', json_group_array(column ORDER BY idx),
    'length', count(*)
  )
FROM frame_table;

-- Returns an array of strings for the given `utid`.
CREATE PERFETTO FUNCTION _export_firefox_string_array(
    utid JOINID(thread.id)
)
RETURNS STRING AS
WITH
  string_table AS (
    SELECT DISTINCT
      string_table_index AS idx,
      name AS str
    FROM _export_to_firefox_table
    WHERE
      utid = $utid
  )
SELECT
  json_group_array(str ORDER BY idx)
FROM string_table;

-- Returns an instance of `FuncTable` as defined in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
-- for the given `utid`.
CREATE PERFETTO FUNCTION _export_firefox_func_table(
    utid JOINID(thread.id)
)
RETURNS STRING AS
WITH
  func_table AS (
    SELECT DISTINCT
      func_table_index AS idx,
      string_table_index AS name,
      FALSE AS is_js,
      FALSE AS relevant_for_js,
      -1 AS resource,
      NULL AS file_name,
      NULL AS line_number,
      NULL AS column_number
    FROM _export_to_firefox_table
    WHERE
      utid = $utid
  )
SELECT
  json_object(
    'name', json_group_array(name ORDER BY idx),
    'isJS', json_group_array(is_js ORDER BY idx),
    'relevantForJS', json_group_array(relevant_for_js ORDER BY idx),
    'resource', json_group_array(resource ORDER BY idx),
    'fileName', json_group_array(file_name ORDER BY idx),
    'lineNumber', json_group_array(line_number ORDER BY idx),
    'columnNumber', json_group_array(column_number ORDER BY idx),
    'length', count(*)
  )
FROM func_table;

-- Returns an instance of `Thread` as defined in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
-- for the given `utid`.
CREATE PERFETTO FUNCTION _export_firefox_thread(
    utid JOINID(thread.id)
)
RETURNS STRING AS
SELECT
  -- jsTracer?: JsTracerTable
  -- isPrivateBrowsing?: boolean
  -- userContextId?: number
  json_object(
    'processType', 'default',
    -- processStartupTime: Milliseconds
    -- processShutdownTime: Milliseconds | null
    -- registerTime: Milliseconds
    -- unregisterTime: Milliseconds | null
    -- pausedRanges: PausedRange[]
    -- showMarkersInTimeline?: boolean
    'name', coalesce(thread.name, ''),
    'isMainThread', FALSE,
    -- 'eTLD+1'?: string
    -- processName?: string
    -- isJsTracer?: boolean
    'pid', coalesce(process.pid, 0),
    'tid', coalesce(thread.tid, 0),
    'samples', json(_export_firefox_samples_table($utid)),
    -- jsAllocations?: JsAllocationsTable
    -- nativeAllocations?: NativeAllocationsTable
    'markers', json(_export_firefox_thread_markers()),
    'stackTable', json(_export_firefox_stack_table($utid)),
    'frameTable', json(_export_firefox_frame_table($utid)),
    'stringArray', json(_export_firefox_string_array($utid)),
    'funcTable', json(_export_firefox_func_table($utid)),
    'resourceTable', json(_export_firefox_resource_table()),
    'nativeSymbols', json(_export_firefox_native_symbol_table())
  )
FROM thread
JOIN process
  USING (upid)
WHERE
  utid = $utid;

-- Returns an array of `Thread` instances as defined in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
-- for each thread present in the trace.
CREATE PERFETTO FUNCTION _export_firefox_threads()
RETURNS STRING AS
SELECT
  json_group_array(json(_export_firefox_thread(utid)))
FROM thread;

-- Returns an instance of `ProfileMeta` as defined in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
CREATE PERFETTO FUNCTION _export_firefox_meta()
RETURNS STRING AS
SELECT
  -- device?: string
  -- importedFrom?: string
  -- usesOnlyOneStackType?: boolean
  -- doesNotUseFrameImplementation?: boolean
  -- sourceCodeIsNotOnSearchfox?: boolean
  -- extra?: ExtraProfileInfoSection[]
  -- initialVisibleThreads?: ThreadIndex[]
  -- initialSelectedThreads?: ThreadIndex[]
  -- keepProfileThreadOrder?: boolean
  -- gramsOfCO2ePerKWh?: number
  json_object(
    'interval', 1,
    'startTime', 0,
    -- default
    -- endTime?: Milliseconds
    -- profilingStartTime?: Milliseconds
    -- profilingEndTime?: Milliseconds
    'processType', 0,
    -- extensions?: ExtensionTable
    'categories', json_array(
      json_object('name', 'Other', 'color', 'grey', 'subcategories', json_array('Other'))
    ),
    'product', 'Perfetto',
    'stackwalk', 1,
    -- Taken from a generated profile
    -- debug?: boolean
    'version', 29,
    -- Taken from a generated profile
    'preprocessedProfileVersion', 48,
    -- abi?: string
    -- misc?: string
    -- oscpu?: string
    -- mainMemory?: Bytes
    -- platform?: 'Android' | 'Windows' | 'Macintosh' | 'X11' | string
    -- toolkit?: 'gtk' | 'gtk3' | 'windows' | 'cocoa' | 'android' | string
    -- appBuildID?: string
    -- arguments?: string
    -- sourceURL?: string
    -- physicalCPUs?: number
    -- logicalCPUs?: number
    -- CPUName?: string
    -- symbolicated?: boolean
    -- symbolicationNotSupported?: boolean
    -- updateChannel?: 'default' | 'nightly' | 'nightly-try' | 'aurora' | 'beta' | 'release' | 'esr' | string
    -- visualMetrics?: VisualMetrics
    -- configuration?: ProfilerConfiguration
    'markerSchema', json_array(),
    'sampleUnits', json_object('time', 'ms', 'eventDelay', 'ms', 'threadCPUDelta', 'µs')
  );

-- Dumps all trace data as a Firefox profile json string
-- See `Profile` in
-- https://github.com/firefox-devtools/profiler/blob/main/src/types/profile.js
-- Also
-- https://firefox-source-docs.mozilla.org/tools/profiler/code-overview.html
--
-- You would probably want to download the generated json and then open at
-- https://https://profiler.firefox.com
-- You can easily do this from the UI via the following SQL
-- `SELECT CAST(export_to_firefox_profile() AS BLOB) AS profile;`
-- The result will have a link for you to download this json as a file.
CREATE PERFETTO FUNCTION export_to_firefox_profile()
-- Json profile
RETURNS STRING AS
SELECT
  json_object(
    'meta', json(_export_firefox_meta()),
    'libs', json_array(),
    'pages', NULL,
    'counters', NULL,
    'profilerOverhead', NULL,
    'threads', json(_export_firefox_threads()),
    'profilingLog', NULL,
    'profileGatheringLog', NULL
  );
