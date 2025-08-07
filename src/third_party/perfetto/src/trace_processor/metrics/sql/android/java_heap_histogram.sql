--
-- Copyright 2020 The Android Open Source Project
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

SELECT RUN_METRIC('android/process_metadata.sql');

DROP TABLE IF EXISTS android_special_classes;
CREATE PERFETTO TABLE android_special_classes AS
WITH RECURSIVE cls_visitor(cls_id, category) AS (
  SELECT id, name FROM heap_graph_class WHERE name IN (
    'android.view.View',
    'android.app.Activity',
    'android.app.Fragment',
    'android.content.ContentProviderClient',
    'android.os.Binder',
    'android.os.BinderProxy',
    'android.os.Parcel',
    'com.android.server.am.ConnectionRecord',
    'com.android.server.am.PendingIntentRecord')
  UNION ALL
  SELECT child.id, parent.category
  FROM heap_graph_class child JOIN cls_visitor parent ON parent.cls_id = child.superclass_id
)
SELECT * FROM cls_visitor;

DROP TABLE IF EXISTS heap_obj_histograms;
CREATE PERFETTO TABLE heap_obj_histograms AS
SELECT
  o.upid,
  o.graph_sample_ts,
  o.type_id AS cls_id,
  COUNT(1) AS obj_count,
  SUM(IIF(o.reachable, 1, 0)) AS reachable_obj_count,
  SUM(self_size) / 1024 AS size_kb,
  SUM(IIF(o.reachable, self_size, 0)) / 1024 AS reachable_size_kb,
  SUM(native_size) / 1024 AS native_size_kb,
  SUM(IIF(o.reachable, native_size, 0)) / 1024 AS reachable_native_size_kb
FROM heap_graph_object o
GROUP BY 1, 2, 3
ORDER BY 1, 2, 3;

DROP VIEW IF EXISTS java_heap_histogram_output;
CREATE PERFETTO VIEW java_heap_histogram_output AS
WITH
-- Group by to build the repeated field by upid, ts
heap_obj_histogram_count_protos AS (
  SELECT
    upid,
    graph_sample_ts,
    RepeatedField(JavaHeapHistogram_TypeCount(
      'type_name', IFNULL(c.deobfuscated_name, c.name),
      'category', category,
      'obj_count', obj_count,
      'reachable_obj_count', reachable_obj_count,
      'size_kb', size_kb,
      'reachable_size_kb', reachable_size_kb,
      'native_size_kb', native_size_kb,
      'reachable_native_size_kb', reachable_native_size_kb
    )) AS count_protos
  FROM heap_obj_histograms hist
  JOIN heap_graph_class c ON hist.cls_id = c.id
  LEFT JOIN android_special_classes special USING(cls_id)
  GROUP BY 1, 2
),
-- Group by to build the repeated field by upid
heap_obj_histogram_sample_protos AS (
  SELECT
    upid,
    RepeatedField(JavaHeapHistogram_Sample(
      'ts', graph_sample_ts,
      'type_count', count_protos
    )) AS sample_protos
  FROM heap_obj_histogram_count_protos
  GROUP BY 1
)
SELECT JavaHeapHistogram(
  'instance_stats', RepeatedField(JavaHeapHistogram_InstanceStats(
    'upid', upid,
    'process', process_metadata.metadata,
    'samples', sample_protos
  )))
FROM heap_obj_histogram_sample_protos JOIN process_metadata USING (upid);
