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
--

INCLUDE PERFETTO MODULE android.io;

DROP VIEW IF EXISTS android_io_output;
CREATE PERFETTO VIEW android_io_output AS
SELECT AndroidIo(
    'f2fs_counter_stats', (
        SELECT RepeatedField(
            AndroidIo_F2fsCounterStats(
                'name', name,
                'sum', sum,
                'max', max,
                'min', min,
                'dur', dur,
                'count', count,
                'avg', avg
            )
        )
        FROM _android_io_f2fs_counter_stats
    ),
    'f2fs_write_stats', (
        SELECT RepeatedField(
            AndroidIo_F2fsWriteStats(
                'total_write_count', total_write_count,
                'distinct_processes', distinct_processes,
                'total_bytes_written', total_bytes_written,
                'distinct_device_count', distinct_device_count,
                'distinct_inode_count', distinct_inode_count,
                'distinct_thread_count', distinct_thread_count
            )
        )
        FROM _android_io_f2fs_aggregate_write_stats
    )
);