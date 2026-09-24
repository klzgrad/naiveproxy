--
-- Copyright 2026 The Android Open Source Project
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

-- Details of Java OutOfMemoryError exceptions that triggered heap dumps.
CREATE PERFETTO VIEW android_heap_graph_java_oome_details(
  -- Unique identifier for this details row.
  id ID,
  -- The heap graph instance this OOM trigger details belongs to.
  heap_graph_id JOINID(heap_graph.id),
  -- Number of bytes that triggered the OOME.
  allocation_size_bytes LONG,
  -- Total free bytes in the Java heap at OOME time.
  total_bytes_free LONG,
  -- Free bytes remaining until OOME.
  free_bytes_until_oom LONG,
  -- Error message associated with the OOME.
  error_msg STRING
)
AS
SELECT
  id,
  heap_graph_id,
  allocation_size_bytes,
  total_bytes_free,
  free_bytes_until_oom,
  error_msg
FROM __intrinsic_heap_graph_java_oome_details;
