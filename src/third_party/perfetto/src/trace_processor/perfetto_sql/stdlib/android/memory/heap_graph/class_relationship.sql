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

INCLUDE PERFETTO MODULE graphs.scan;

-- Given a list of classes as ancestor classes, return all the classes that
-- descend from them.
CREATE PERFETTO MACRO android_heap_graph_class_find_descendants(
    -- ancestor class `id`s from the heap_graph_class table containing a
    -- single column: `id`
    ancestor_class_ids TableOrSubquery
)
-- Table of the schema
-- (id JOINID(heap_graph_class.id), ancestor_class_id JOINID(heap_graph_class.id), ancestor_class_name STRING)
-- id: `id` of the class as in heap_graph_class
-- ancestor_class_id: `id` of the ancestor class as given in the input
-- ancestor_class_name: `name` of the ancestor class as in heap_graph_class
RETURNS TableOrSubquery AS
(
  WITH
    class_forest(source_node_id, dest_node_id) AS (
      SELECT
        superclass_id AS source_node_id,
        id AS dest_node_id
      FROM heap_graph_class
      WHERE
        superclass_id IS NOT NULL
    ),
    ancestors(id, ancestor_class_id, ancestor_class_name) AS (
      SELECT
        id,
        id AS ancestor_class_id,
        name AS ancestor_class_name
      FROM $ancestor_class_ids
      JOIN heap_graph_class
        USING (id)
    )
  SELECT
    id,
    ancestor_class_id,
    ancestor_class_name
  FROM _graph_scan!(
    class_forest,
    ancestors,
    (ancestor_class_id, ancestor_class_name),
    (
      SELECT id, ancestor_class_id, ancestor_class_name
      FROM $table
    )
  )
);
