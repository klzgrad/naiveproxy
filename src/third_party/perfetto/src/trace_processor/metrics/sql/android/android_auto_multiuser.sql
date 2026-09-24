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
--

INCLUDE PERFETTO MODULE android.auto.multiuser;
INCLUDE PERFETTO MODULE time.conversion;

DROP VIEW IF EXISTS android_auto_multiuser_output;
CREATE PERFETTO VIEW android_auto_multiuser_output AS
SELECT AndroidAutoMultiuserMetric(
    'user_switch', (
        SELECT RepeatedField(
            AndroidAutoMultiuserMetric_EventData(
                'user_id', cast_int!(event_start_user_id),
                'start_event', event_start_name,
                'end_event', event_end_name,
                'duration_ms', time_to_ms(duration),
                'previous_user_info', AndroidAutoMultiuserMetric_EventData_UserData(
                    'user_id', user_id,
                    'total_cpu_time_ms', time_to_ms(total_cpu_time),
                    'total_memory_usage_kb', total_memory_usage_kb
                )
            )
        )
        FROM android_auto_multiuser_timing_with_previous_user_resource_usage
    )
);