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

INCLUDE PERFETTO MODULE prelude.after_eof.indexes;

INCLUDE PERFETTO MODULE prelude.after_eof.views;

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
  root_distance LONG,
  -- Optional ID into heap_graph_object_data for HPROF data.
  object_data_id LONG
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
  root_distance,
  object_data_id
FROM __intrinsic_heap_graph_object;

-- HPROF-specific data for heap graph objects.
--
-- Contains decoded string content and primitive array blob references.
-- Only populated for HPROF (ART) heap dumps, not for proto heap graphs.
-- Linked from heap_graph_object.object_data_id.
CREATE PERFETTO VIEW heap_graph_object_data (
  -- Unique identifier for this data entry.
  id ID,
  -- Join key with heap_graph_primitive containing primitive field values.
  field_set_id JOINID(heap_graph_primitive.field_set_id),
  -- Decoded string value for java.lang.String instances.
  value_string STRING,
  -- For primitive array objects, the element type (boolean, byte, char, short,
  -- int, long, float, double).
  array_element_type STRING,
  -- For primitive array objects, the number of elements.
  array_element_count LONG,
  -- For primitive array objects, opaque ID to retrieve raw element bytes via
  -- __intrinsic_heap_graph_array().
  array_data_id LONG,
  -- For primitive array objects, a 64-bit content hash of the raw element
  -- bytes. Two arrays with the same hash have identical content.
  array_data_hash LONG
) AS
SELECT
  id,
  field_set_id,
  value_string,
  array_element_type,
  array_element_count,
  array_data_id,
  array_data_hash
FROM __intrinsic_heap_graph_object_data;

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

-- Primitive field values for heap graph objects.
--
-- This associates the object with given field_set_id with its primitive
-- field values (for instances).
CREATE PERFETTO VIEW heap_graph_primitive (
  -- Unique identifier for this field entry.
  id ID,
  -- Join key to heap_graph_object_data.field_set_id.
  field_set_id JOINID(heap_graph_object_data.field_set_id),
  -- The field name. E.g. Foo.count.
  field_name STRING,
  -- The primitive type. E.g. int, boolean, float.
  field_type STRING,
  -- Value for boolean fields (0 or 1).
  bool_value LONG,
  -- Value for byte fields.
  byte_value LONG,
  -- Value for char fields (as integer codepoint).
  char_value LONG,
  -- Value for short fields.
  short_value LONG,
  -- Value for int fields.
  int_value LONG,
  -- Value for long fields.
  long_value LONG,
  -- Value for float fields.
  float_value DOUBLE,
  -- Value for double fields.
  double_value DOUBLE
) AS
SELECT
  id,
  field_set_id,
  field_name,
  field_type,
  bool_value,
  byte_value,
  char_value,
  short_value,
  int_value,
  long_value,
  float_value,
  double_value
FROM __intrinsic_heap_graph_primitive;

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
