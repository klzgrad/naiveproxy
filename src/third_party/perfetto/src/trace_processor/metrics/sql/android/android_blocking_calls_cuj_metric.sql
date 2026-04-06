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

SELECT RUN_METRIC('android/process_metadata.sql');

INCLUDE PERFETTO MODULE android.slices;
INCLUDE PERFETTO MODULE android.binder;
INCLUDE PERFETTO MODULE android.critical_blocking_calls;
INCLUDE PERFETTO MODULE android.cujs.sysui_cujs;

-- We have:
--  (1) a list of slices from the main thread of each process from the
--  all_main_thread_relevant_slices table.
--  (2) a list of android cuj with beginning, end, and process
-- It's needed to:
--  (1) assign a cuj to each slice. If there are multiple cujs going on during a
--      slice, there needs to be 2 entries for that slice, one for each cuj id.
--  (2) each slice needs to be trimmed to be fully inside the cuj associated
--      (as we don't care about what's outside cujs)
DROP TABLE IF EXISTS main_thread_slices_scoped_to_cujs;
CREATE PERFETTO TABLE main_thread_slices_scoped_to_cujs AS
SELECT
    s.id,
    s.id AS slice_id,
    s.name,
    max(s.ts, cuj.ts) AS ts,
    min(s.ts + s.dur, cuj.ts_end) as ts_end,
    min(s.ts + s.dur, cuj.ts_end) - max(s.ts, cuj.ts) AS dur,
    cuj.cuj_id,
    cuj.cuj_name,
    s.process_name,
    s.upid,
    s.utid
FROM _android_critical_blocking_calls s
    JOIN  android_jank_latency_cujs cuj
    -- only when there is an overlap
    ON s.ts + s.dur > cuj.ts AND s.ts < cuj.ts_end
        -- and are from the same process
        AND s.upid = cuj.upid
        -- from the CUJ ui thread only
        AND s.utid = cuj.ui_thread;


DROP TABLE IF EXISTS android_blocking_calls_cuj_calls;
CREATE TABLE android_blocking_calls_cuj_calls AS
SELECT
    name,
    COUNT(*) AS occurrences,
    MAX(dur) AS max_dur_ns,
    MIN(dur) AS min_dur_ns,
    SUM(dur) AS total_dur_ns,
    upid,
    cuj_id,
    cuj_name,
    process_name
FROM
    main_thread_slices_scoped_to_cujs
GROUP BY name, upid, cuj_id, cuj_name, process_name
ORDER BY cuj_id;


DROP VIEW IF EXISTS android_blocking_calls_cuj_metric_output;
CREATE PERFETTO VIEW android_blocking_calls_cuj_metric_output AS
SELECT AndroidBlockingCallsCujMetric('cuj', (
    SELECT RepeatedField(
        AndroidBlockingCallsCujMetric_Cuj(
            'id', cuj_id,
            'name', cuj_name,
            'process', process_metadata_proto(cuj.upid),
            'ts',  cuj.ts,
            'dur', cuj.dur,
            'blocking_calls', (
                SELECT RepeatedField(
                    AndroidBlockingCall(
                        'name', b.name,
                        'cnt', b.occurrences,
                        'total_dur_ms', CAST(total_dur_ns / 1e6 AS INT),
                        'max_dur_ms', CAST(max_dur_ns / 1e6 AS INT),
                        'min_dur_ms', CAST(min_dur_ns / 1e6 AS INT),
                        'total_dur_ns', b.total_dur_ns,
                        'max_dur_ns', b.max_dur_ns,
                        'min_dur_ns', b.min_dur_ns
                    )
                )
                FROM android_blocking_calls_cuj_calls b
                WHERE b.cuj_id = cuj.cuj_id and b.upid = cuj.upid
                ORDER BY total_dur_ns DESC
            )
        )
    )
    FROM android_jank_latency_cujs cuj
    ORDER BY cuj.cuj_id ASC
));
