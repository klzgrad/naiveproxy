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

-- @module prelude.after_eof.memory
-- Memory and heap analysis tables.
--
-- This module provides tables and views for analyzing memory usage,
-- including heap graphs for Android Runtime (ART) and memory snapshots
-- for detailed memory profiling.

-- Stores class information within ART heap graphs. It represents Java/Kotlin
-- classes that exist in the heap, including their names, inheritance
-- relationships, and loading context.
CREATE PERFETTO VIEW heap_graph_class (
  -- Unique identifier for this heap graph class.
  id ID,
  -- (potentially obfuscated) name of the class.
  name STRING,
  -- If class name was obfuscated and deobfuscation map for it provided, the
  -- deobfuscated name.
  deobfuscated_name STRING,
  -- the APK / Dex / JAR file the class is contained in.
  location STRING,
  -- The superclass of this class.
  superclass_id JOINID(heap_graph_class.id),
  -- The classloader that loaded this class.
  classloader_id LONG,
  -- The kind of class.
  kind STRING
) AS
SELECT
  id,
  name,
  deobfuscated_name,
  location,
  superclass_id,
  classloader_id,
  kind
FROM __intrinsic_heap_graph_class;

-- The objects on the Dalvik heap.
--
-- All rows with the same (upid, graph_sample_ts) are one dump.
CREATE PERFETTO VIEW heap_graph_object (
  -- Unique identifier for this heap graph object.
  id ID,
  -- Unique PID of the target.
  upid JOINID(process.id),
  -- Timestamp this dump was taken at.
  graph_sample_ts TIMESTAMP,
  -- Size this object uses on the Java Heap.
  self_size LONG,
  -- Approximate amount of native memory used by this object, as reported by
  -- libcore.util.NativeAllocationRegistry.size.
  native_size LONG,
  -- Join key with heap_graph_reference containing all objects referred in this
  -- object's fields.
  reference_set_id JOINID(heap_graph_reference.reference_set_id),
  -- Bool whether this object is reachable from a GC root. If false, this object
  -- is uncollected garbage.
  reachable BOOL,
  -- The type of ART heap this object is stored on (app, zygote, boot image)
  heap_type STRING,
  -- Class this object is an instance of.
  type_id JOINID(heap_graph_class.id),
  -- If not NULL, this object is a GC root.
  root_type STRING,
  -- Distance from the root object.
  root_distance LONG
) AS
SELECT
  id,
  upid,
  graph_sample_ts,
  self_size,
  native_size,
  reference_set_id,
  reachable,
  heap_type,
  type_id,
  root_type,
  root_distance
FROM __intrinsic_heap_graph_object;

-- Many-to-many mapping between heap_graph_object.
--
-- This associates the object with given reference_set_id with the objects
-- that are referred to by its fields.
CREATE PERFETTO VIEW heap_graph_reference (
  -- Unique identifier for this heap graph reference.
  id ID,
  -- Join key to heap_graph_object reference_set_id.
  reference_set_id JOINID(heap_graph_object.reference_set_id),
  -- Id of object that has this reference_set_id.
  owner_id JOINID(heap_graph_object.id),
  -- Id of object that is referred to.
  owned_id JOINID(heap_graph_object.id),
  -- The field that refers to the object. E.g. Foo.name.
  field_name STRING,
  -- The static type of the field. E.g. java.lang.String.
  field_type_name STRING,
  -- The deobfuscated name, if field_name was obfuscated and a deobfuscation
  -- mapping was provided for it.
  deobfuscated_field_name STRING
) AS
SELECT
  id,
  reference_set_id,
  owner_id,
  owned_id,
  field_name,
  field_type_name,
  deobfuscated_field_name
FROM __intrinsic_heap_graph_reference;

-- Table with memory snapshots.
CREATE PERFETTO VIEW memory_snapshot (
  -- Unique identifier for this snapshot.
  id ID,
  -- Time of the snapshot.
  timestamp TIMESTAMP,
  -- Track of this snapshot.
  track_id JOINID(track.id),
  -- Detail level of this snapshot.
  detail_level STRING
) AS
SELECT
  id,
  timestamp,
  track_id,
  detail_level
FROM __intrinsic_memory_snapshot;

-- Table with process memory snapshots.
CREATE PERFETTO VIEW process_memory_snapshot (
  -- Unique identifier for this snapshot.
  id ID,
  -- Snapshot ID for this snapshot.
  snapshot_id JOINID(memory_snapshot.id),
  -- Process for this snapshot.
  upid JOINID(process.id)
) AS
SELECT
  id,
  snapshot_id,
  upid
FROM __intrinsic_process_memory_snapshot;

-- Table with memory snapshot nodes.
CREATE PERFETTO VIEW memory_snapshot_node (
  -- Unique identifier for this node.
  id ID,
  -- Process snapshot ID for to this node.
  process_snapshot_id JOINID(process_memory_snapshot.id),
  -- Parent node for this node, optional.
  parent_node_id JOINID(memory_snapshot_node.id),
  -- Path for this node.
  path STRING,
  -- Size of the memory allocated to this node.
  size LONG,
  -- Effective size used by this node.
  effective_size LONG,
  -- Additional args of the node.
  arg_set_id ARGSETID
) AS
SELECT
  id,
  process_snapshot_id,
  parent_node_id,
  path,
  size,
  effective_size,
  arg_set_id
FROM __intrinsic_memory_snapshot_node;

-- Table with memory snapshot edge
CREATE PERFETTO VIEW memory_snapshot_edge (
  -- Unique identifier for this edge.
  id ID,
  -- Source node for this edge.
  source_node_id JOINID(memory_snapshot_node.id),
  -- Target node for this edge.
  target_node_id JOINID(memory_snapshot_node.id),
  -- Importance for this edge.
  importance LONG
) AS
SELECT
  id,
  source_node_id,
  target_node_id,
  importance
FROM __intrinsic_memory_snapshot_edge;
