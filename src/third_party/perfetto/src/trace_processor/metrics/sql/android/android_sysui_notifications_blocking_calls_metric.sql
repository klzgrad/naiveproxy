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

DROP TABLE IF EXISTS android_sysui_notifications_blocking_calls;
CREATE PERFETTO TABLE android_sysui_notifications_blocking_calls AS
SELECT
    s.name name,
    COUNT(s.name) count,
    MAX(dur) AS max_dur_ns,
    MIN(dur) AS min_dur_ns,
    SUM(dur) AS total_dur_ns
FROM slice s
    JOIN thread_track ON s.track_id = thread_track.id
    JOIN thread USING (utid)
WHERE
    thread.is_main_thread AND
    s.dur > 0 AND (
       s.name GLOB 'NotificationStackScrollLayout#onMeasure'
    OR s.name GLOB 'NotificationToplineView#onMeasure'
    OR s.name GLOB 'ExpNotRow#*'
    OR s.name GLOB 'NotificationShadeWindowView#onMeasure'
    OR s.name GLOB 'ImageFloatingTextView#onMeasure'
)
GROUP BY s.name;

DROP VIEW IF EXISTS android_sysui_notifications_blocking_calls_metric_output;
CREATE PERFETTO VIEW android_sysui_notifications_blocking_calls_metric_output AS
SELECT AndroidSysUINotificationsBlockingCallsMetric('blocking_calls', (
        SELECT RepeatedField(
            AndroidBlockingCall(
                'name', a.name,
                'cnt', a.count,
                'total_dur_ns', a.total_dur_ns,
                'max_dur_ns', a.max_dur_ns,
                'min_dur_ns', a.min_dur_ns
            )
        )
        FROM android_sysui_notifications_blocking_calls a
        ORDER BY total_dur_ns DESC
    )
);
