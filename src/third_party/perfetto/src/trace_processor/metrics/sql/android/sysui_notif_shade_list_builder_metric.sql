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

INCLUDE PERFETTO MODULE android.slices;

SELECT RUN_METRIC('android/sysui_notif_shade_list_builder_slices.sql');

-- Get statics of all ShadeListBuilder.buildList slices
DROP TABLE IF EXISTS shade_list_builder_all;
CREATE PERFETTO TABLE shade_list_builder_all AS
SELECT
  s.name name,
  COUNT(s.name) AS count,
  cast(avg(dur) as int) average_dur_ns,
  max(dur) maximum_dur_ns,
  s.id id
FROM shade_list_builder_build_list_slices s
GROUP BY s.name;

-- Id of shade_list_builder slices that has a descendant of inflation
DROP VIEW IF EXISTS slices_id_with_inflation_descendants;
CREATE PERFETTO VIEW slices_id_with_inflation_descendants AS
SELECT DISTINCT id
  FROM slices_and_descendants
  WHERE
    descendant_name = 'HybridGroupManager#inflateHybridView' OR
    descendant_name = 'NotifChildCont#recreateHeader';

-- Id of shade_list_builder slices that has a descendant of ShadeNode modification
DROP VIEW IF EXISTS slices_id_with_modification_descendants;
CREATE PERFETTO VIEW slices_id_with_modification_descendants AS
SELECT DISTINCT id
  FROM slices_and_descendants
  WHERE
    descendant_name = 'ShadeNode#addChildAt' OR
    descendant_name = 'ShadeNode#removeChildAt' OR
    descendant_name = 'ShadeNode#moveChildTo';

DROP TABLE IF EXISTS shade_list_builder_slices_with_inflation;
CREATE PERFETTO TABLE shade_list_builder_slices_with_inflation AS
SELECT
  s.name || "_with_inflation" name,
  COUNT(s.name) AS count,
  cast(avg(dur) as int) average_dur_ns,
  max(dur) maximum_dur_ns
FROM shade_list_builder_build_list_slices s
WHERE s.id IN slices_id_with_inflation_descendants
GROUP BY s.name;

DROP TABLE IF EXISTS shade_list_builder_slices_with_modification;
CREATE PERFETTO TABLE shade_list_builder_slices_with_modification AS
SELECT
  s.name || "_with_node_modification" name,
  COUNT(s.name) AS count,
  cast(avg(dur) as int) average_dur_ns,
  max(dur) maximum_dur_ns
FROM shade_list_builder_build_list_slices s
WHERE s.id IN slices_id_with_modification_descendants
GROUP BY s.name;


DROP VIEW IF EXISTS sysui_notif_shade_list_builder_metric_output;
CREATE PERFETTO VIEW sysui_notif_shade_list_builder_metric_output AS
SELECT SysuiNotifShadeListBuilderMetric(
        'all_slices_performance', (
            SELECT SysUiSlicePerformanceStatisticalData(
                'name', a.name,
                'cnt', a.count,
                'avg_dur_ms', cast (a.average_dur_ns / 1000000 as int),
                'max_dur_ms', cast (a.maximum_dur_ns / 1000000 as int),
                'avg_dur_ns', a.average_dur_ns,
                'max_dur_ns', a.maximum_dur_ns
            )
            FROM shade_list_builder_all a
        ),
        'slices_with_inflation_performance', (
            SELECT SysUiSlicePerformanceStatisticalData(
                'name', a.name,
                'cnt', a.count,
                'avg_dur_ms', cast (a.average_dur_ns / 1000000 as int),
                'max_dur_ms', cast (a.maximum_dur_ns / 1000000 as int),
                'avg_dur_ns', a.average_dur_ns,
                'max_dur_ns', a.maximum_dur_ns
            )
            FROM shade_list_builder_slices_with_inflation a
        ),
        'slices_with_modification_performance', (
            SELECT SysUiSlicePerformanceStatisticalData(
                'name', a.name,
                'cnt', a.count,
                'avg_dur_ms', cast (a.average_dur_ns / 1000000 as int),
                'max_dur_ms', cast (a.maximum_dur_ns / 1000000 as int),
                'avg_dur_ns', a.average_dur_ns,
                'max_dur_ns', a.maximum_dur_ns
            )
            FROM shade_list_builder_slices_with_modification a
        ),
        'slice', (
            SELECT RepeatedField(
                SysuiNotifShadeListBuilderMetric_SliceDuration(
                    'name', a.name,
                    'dur_ms', cast (a.dur / 1000000 as int),
                    'dur_ns', a.dur
                )
            )
            FROM shade_list_builder_build_list_slices a
            ORDER BY dur DESC
        )
);