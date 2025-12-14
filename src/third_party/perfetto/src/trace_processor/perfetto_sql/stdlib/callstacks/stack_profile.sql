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

INCLUDE PERFETTO MODULE graphs.hierarchy;

INCLUDE PERFETTO MODULE graphs.scan;

INCLUDE PERFETTO MODULE v8.jit;

CREATE PERFETTO TABLE _callstack_spf_summary AS
SELECT
  id,
  symbol_set_id,
  (
    SELECT
      id
    FROM stack_profile_symbol AS s
    WHERE
      s.symbol_set_id = f.symbol_set_id
    ORDER BY
      id
    LIMIT 1
  ) AS min_symbol_id,
  (
    SELECT
      id
    FROM stack_profile_symbol AS s
    WHERE
      s.symbol_set_id = f.symbol_set_id
    ORDER BY
      id DESC
    LIMIT 1
  ) AS max_symbol_id
FROM stack_profile_frame AS f
ORDER BY
  id;

CREATE PERFETTO TABLE _callstack_spc_raw_forest AS
SELECT
  c.id AS callsite_id,
  s.id AS symbol_id,
  iif(s.id IS f.max_symbol_id, c.parent_id, c.id) AS parent_callsite_id,
  iif(s.id IS f.max_symbol_id, pf.min_symbol_id, s.id + 1) AS parent_symbol_id,
  f.id AS frame_id,
  jf.jit_code_id AS jit_code_id,
  s.id IS f.min_symbol_id AS is_leaf
FROM stack_profile_callsite AS c
JOIN _callstack_spf_summary AS f
  ON c.frame_id = f.id
LEFT JOIN __intrinsic_jit_frame AS jf
  ON jf.frame_id = f.id
LEFT JOIN stack_profile_symbol AS s
  USING (symbol_set_id)
LEFT JOIN stack_profile_callsite AS p
  ON c.parent_id = p.id
LEFT JOIN _callstack_spf_summary AS pf
  ON p.frame_id = pf.id
ORDER BY
  c.id;

CREATE PERFETTO TABLE _callstack_spc_forest AS
SELECT
  c._auto_id AS id,
  p._auto_id AS parent_id,
  -- TODO(lalitm): consider demangling in a separate table as
  -- demangling is suprisingly inefficient and is taking a
  -- significant fraction of the runtime on big traces.
  coalesce(
    'JS: ' || iif(jsf.name = "", "(anonymous)", jsf.name) || ':' || jsf.line || ':' || jsf.col || ' [' || lower(jsc.tier) || ']',
    'WASM: ' || wc.function_name || ' [' || lower(wc.tier) || ']',
    'REGEXP: ' || rc.pattern,
    'V8: ' || v8c.function_name,
    'JIT: ' || jc.function_name,
    demangle(coalesce(s.name, f.deobfuscated_name, f.name)),
    coalesce(s.name, f.deobfuscated_name, f.name, '[Unknown]')
  ) AS name,
  f.mapping AS mapping_id,
  s.source_file,
  coalesce(jsf.line, s.line_number) AS line_number,
  coalesce(jsf.col, 0) AS column_number,
  s.inlined,
  c.callsite_id,
  c.is_leaf AS is_leaf_function_in_callsite_frame
FROM _callstack_spc_raw_forest AS c
JOIN stack_profile_frame AS f
  ON c.frame_id = f.id
LEFT JOIN _v8_js_code AS jsc
  USING (jit_code_id)
LEFT JOIN v8_js_function AS jsf
  USING (v8_js_function_id)
LEFT JOIN _v8_internal_code AS v8c
  USING (jit_code_id)
LEFT JOIN _v8_wasm_code AS wc
  USING (jit_code_id)
LEFT JOIN _v8_regexp_code AS rc
  USING (jit_code_id)
LEFT JOIN __intrinsic_jit_code AS jc
  ON c.jit_code_id = jc.id
LEFT JOIN stack_profile_symbol AS s
  ON c.symbol_id = s.id
LEFT JOIN _callstack_spc_raw_forest AS p
  ON p.callsite_id = c.parent_callsite_id AND p.symbol_id IS c.parent_symbol_id
ORDER BY
  c._auto_id;

-- This index is used to efficiently join the callstack forest with the
-- sample data on callsite_id. This is a key operation in the
-- _callstacks_for_callsites and _callstacks_for_stack_profile_samples macros.
CREATE PERFETTO INDEX _callstack_spc_index ON _callstack_spc_forest(callsite_id);

-- This index is necessary to optimize the leaf-finding query in
-- _callstacks_self_to_cumulative. Without this index, the anti-join on
-- parent_id can be very slow on large traces.
CREATE PERFETTO INDEX _callstack_spc_parent_index ON _callstack_spc_forest(parent_id);

CREATE PERFETTO MACRO _callstacks_for_stack_profile_samples(
    spc_samples TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    f.id,
    f.parent_id,
    f.callsite_id,
    f.name,
    m.name AS mapping_name,
    f.source_file,
    f.line_number,
    f.inlined,
    f.is_leaf_function_in_callsite_frame
  FROM _tree_reachable_ancestors_or_self!(
    _callstack_spc_forest,
    (
      SELECT f.id
      FROM $spc_samples s
      JOIN _callstack_spc_forest f USING (callsite_id)
      WHERE f.is_leaf_function_in_callsite_frame
    )
  ) AS g
  JOIN _callstack_spc_forest AS f
    USING (id)
  JOIN stack_profile_mapping AS m
    ON f.mapping_id = m.id
);

CREATE PERFETTO MACRO _callstacks_for_callsites(
    samples TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  WITH
    metrics AS MATERIALIZED (
      SELECT
        callsite_id,
        count() AS self_count
      FROM $samples
      GROUP BY
        callsite_id
    )
  SELECT
    c.id,
    c.parent_id,
    c.name,
    c.mapping_name,
    c.source_file,
    c.line_number,
    iif(c.is_leaf_function_in_callsite_frame, coalesce(m.self_count, 0), 0) AS self_count
  FROM _callstacks_for_stack_profile_samples!(metrics) AS c
  LEFT JOIN metrics AS m
    USING (callsite_id)
);

CREATE PERFETTO MACRO _callstacks_self_to_cumulative(
    callstacks TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    a.*
  FROM _graph_aggregating_scan!(
    (
      SELECT id AS source_node_id, parent_id AS dest_node_id
      FROM $callstacks
      WHERE parent_id IS NOT NULL
    ),
  (
    SELECT id, self_count AS cumulative_count
    FROM $callstacks
    WHERE id NOT IN (SELECT parent_id FROM $callstacks WHERE parent_id IS NOT NULL)
  ),
    (cumulative_count),
    (
      WITH agg AS (
        SELECT t.id, SUM(t.cumulative_count) AS child_count
        FROM $table t
        GROUP BY t.id
      )
      SELECT
        a.id,
        a.child_count + r.self_count as cumulative_count
      FROM agg a
      JOIN $callstacks r USING (id)
    )
  ) AS a
);
