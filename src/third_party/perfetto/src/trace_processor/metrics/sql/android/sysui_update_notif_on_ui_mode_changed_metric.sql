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

-- Table of updateNotifOnUiModeChanged slices
DROP TABLE IF EXISTS sysui_update_notif_on_ui_mode_changed_slices;
CREATE PERFETTO TABLE sysui_update_notif_on_ui_mode_changed_slices AS
SELECT
  s.name name,
  dur,
  s.id id
FROM slice s
  JOIN thread_track ON thread_track.id = s.track_id
  JOIN thread USING (utid)
WHERE
  thread.is_main_thread AND
  s.dur > 0 AND (
    s.name GLOB 'updateNotifOnUiModeChanged'
  );

-- Table of updateNotifOnUiModeChanged slices statistical performance information
DROP TABLE IF EXISTS sysui_update_notif_on_ui_mode_changed_metric;
CREATE PERFETTO TABLE sysui_update_notif_on_ui_mode_changed_metric AS
SELECT
  s.name name,
  COUNT(s.name) AS count,
  cast(avg(dur) as int) average_dur_ns,
  max(dur) maximum_dur_ns
FROM sysui_update_notif_on_ui_mode_changed_slices s
GROUP BY s.name;

DROP VIEW IF EXISTS sysui_update_notif_on_ui_mode_changed_metric_output;
CREATE PERFETTO VIEW sysui_update_notif_on_ui_mode_changed_metric_output AS
SELECT SysuiUpdateNotifOnUiModeChangedMetric(
        'all_slices_performance', (
            SELECT SysUiSlicePerformanceStatisticalData(
                'name', a.name,
                'cnt', a.count,
                'avg_dur_ms', cast (a.average_dur_ns / 1000000 as int),
                'max_dur_ms', cast (a.maximum_dur_ns / 1000000 as int),
                'avg_dur_ns', a.average_dur_ns,
                'max_dur_ns', a.maximum_dur_ns
            )
            FROM sysui_update_notif_on_ui_mode_changed_metric a
        ),
        'slice', (
            SELECT RepeatedField(
                SysuiUpdateNotifOnUiModeChangedMetric_SliceDuration(
                    'name', a.name,
                    'dur_ms', cast (a.dur / 1000000 as int),
                    'dur_ns', a.dur
                )
            )
            FROM sysui_update_notif_on_ui_mode_changed_slices a
            ORDER BY dur DESC
        )
);