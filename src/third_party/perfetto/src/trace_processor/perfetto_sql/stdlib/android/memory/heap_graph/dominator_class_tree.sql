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
-- distributed under the License is distributed ON an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

INCLUDE PERFETTO MODULE android.memory.heap_graph.helpers;

INCLUDE PERFETTO MODULE android.memory.heap_graph.raw_dominator_tree;

CREATE PERFETTO TABLE _heap_graph_dominator_path_hashes AS
SELECT
  *
FROM _heap_graph_type_path_hash!((
  SELECT id, idom_id AS parent_id
  FROM _raw_heap_graph_dominator_tree
));

CREATE PERFETTO TABLE _heap_graph_dominator_path_hashes_aggregated AS
SELECT
  *
FROM _heap_graph_path_hash_aggregate!(_heap_graph_dominator_path_hashes);

CREATE PERFETTO TABLE _heap_graph_dominator_class_tree AS
SELECT
  *
FROM _heap_graph_path_hashes_to_class_tree!(
  _heap_graph_dominator_path_hashes_aggregated
);
