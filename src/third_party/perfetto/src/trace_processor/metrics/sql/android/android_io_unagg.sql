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

DROP VIEW IF EXISTS android_io_unagg_output;
CREATE PERFETTO VIEW android_io_unagg_output AS
SELECT AndroidIoUnaggregated(
    'f2fs_write_unaggregated_stats', (
        SELECT RepeatedField(
            AndroidIoUnaggregated_F2fsWriteUnaggreagatedStat(
                'tid', tid,
                'thread_name', thread_name,
                'pid', pid,
                'process_name', process_name,
                'ino', ino,
                'dev', dev
            )
        )
        FROM _android_io_f2fs_write_stats
    )
);