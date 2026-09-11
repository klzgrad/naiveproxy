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

INCLUDE PERFETTO MODULE android.binder;

DROP TABLE IF EXISTS android_binder_process_incoming_serial_count_per_second;
CREATE PERFETTO TABLE android_binder_process_incoming_serial_count_per_second
AS
WITH unagg AS (
  SELECT
    server_process AS process_name,
    server_pid AS pid,
    server_ts AS ts,
    COUNT()
      OVER (
        PARTITION BY server_upid
        ORDER BY server_ts
        RANGE BETWEEN cast_int!(1e9) PRECEDING AND CURRENT ROW
      ) AS count_per_second
    FROM android_binder_txns
) SELECT process_name, pid, MAX(count_per_second) AS max_count_per_second
  FROM unagg GROUP BY pid;

DROP TABLE IF EXISTS android_binder_process_outgoing_serial_count_per_second;
CREATE PERFETTO TABLE android_binder_process_outgoing_serial_count_per_second
AS
WITH unagg AS (
  SELECT
    client_process AS process_name,
    client_pid AS pid,
    client_ts AS ts,
    COUNT()
      OVER (
        PARTITION BY client_upid
        ORDER BY client_ts
        RANGE BETWEEN cast_int!(1e9) PRECEDING AND CURRENT ROW
      ) AS count_per_second
    FROM android_binder_txns
) SELECT process_name, pid, MAX(count_per_second) AS max_count_per_second
  FROM unagg GROUP BY pid;

DROP VIEW IF EXISTS android_anomaly_output;
CREATE PERFETTO VIEW android_anomaly_output AS
SELECT AndroidAnomalyMetric(
  'binder', (SELECT AndroidAnomalyMetric_Binder(
    'max_incoming_process_count_per_second', (
      SELECT RepeatedField(
        AndroidAnomalyMetric_ProcessAnomaly(
          'process_name', process_name,
          'pid', pid,
          'unit', 'COUNT_PER_SECOND',
          'value', max_count_per_second
        )
      )
      FROM android_binder_process_incoming_serial_count_per_second
    ),
    'max_outgoing_process_count_per_second', (
      SELECT RepeatedField(
        AndroidAnomalyMetric_ProcessAnomaly(
          'process_name', process_name,
          'pid', pid,
          'unit', 'COUNT_PER_SECOND',
          'value', max_count_per_second
        )
      )
      FROM android_binder_process_outgoing_serial_count_per_second
    )
  ))
);
