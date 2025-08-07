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

INCLUDE PERFETTO MODULE android.binder_breakdown;
INCLUDE PERFETTO MODULE linux.cpu.utilization.thread;
INCLUDE PERFETTO MODULE linux.cpu.utilization.slice;
INCLUDE PERFETTO MODULE slices.with_context;
INCLUDE PERFETTO MODULE slices.cpu_time;
INCLUDE PERFETTO MODULE slices.flat_slices;

SELECT RUN_METRIC('android/android_cpu.sql');
SELECT RUN_METRIC('android/android_powrails.sql');

-- Attaching thread proto with media thread name
DROP VIEW IF EXISTS core_type_proto_per_thread_name;
CREATE PERFETTO VIEW core_type_proto_per_thread_name AS
SELECT
utid,
thread.name AS thread_name,
core_type_proto_per_thread.proto AS proto
FROM core_type_proto_per_thread
JOIN thread using(utid)
WHERE thread.name = 'MediaCodec_loop' OR
      thread.name = 'CodecLooper'
GROUP BY thread.name;

-- All process that has codec thread
DROP TABLE IF EXISTS android_codec_process;
CREATE PERFETTO TABLE android_codec_process AS
SELECT
  utid,
  upid,
  process.name AS process_name
FROM thread
JOIN process using(upid)
WHERE thread.name = 'MediaCodec_loop' OR
      thread.name = 'CodecLooper'
GROUP BY process_name, thread.name;

-- Getting cpu cycles for the threads
DROP VIEW IF EXISTS cpu_cycles_runtime;
CREATE PERFETTO VIEW cpu_cycles_runtime AS
SELECT
  utid,
  megacycles,
  runtime,
  proto,
  process_name,
  thread_name
FROM android_codec_process
JOIN cpu_cycles_per_thread using(utid)
JOIN core_type_proto_per_thread_name using(utid);

-- Traces are collected using specific traits in codec framework. These traits
-- are mapped to actual names of slices and then combined with other tables to
-- give out the total_cpu and cpu_running time.

-- Utility function to trim codec trace string: extract the string demilited
-- by the limiter.
CREATE OR REPLACE PERFETTO FUNCTION extract_codec_string(slice_name STRING, limiter STRING)
RETURNS STRING AS
SELECT CASE
  -- Delimit with the first occurrence
  WHEN instr($slice_name, $limiter) > 0
  THEN substr($slice_name, 1, instr($slice_name, $limiter) - 1)
  ELSE $slice_name
END;

CREATE OR REPLACE PERFETTO FUNCTION extract_codec_string_after(slice_name STRING, limiter STRING)
RETURNS STRING AS
SELECT CASE
  -- Delimit with the first occurrence
  WHEN instr($slice_name, $limiter) > 0
  THEN substr($slice_name, instr($slice_name, $limiter) + length($limiter), length($slice_name))
  ELSE $slice_name
END;

-- Traits strings from codec framework
DROP TABLE IF EXISTS trace_trait_table;
CREATE TABLE trace_trait_table(trace_trait TEXT UNIQUE);
INSERT INTO trace_trait_table VALUES
  ('MediaCodec::*'),
  ('CCodec::*'),
  ('CCodecBufferChannel::*'),
  ('C2PooledBlockPool::*'),
  ('C2hal::*'),
  ('ACodec::*'),
  ('FrameDecoder::*'),
  ('*android.hardware.media.c2.*'),
  ('*android.hardware.drm.ICryptoPlugin*');

-- Maps traits to slice strings. Any string with '@' is considered to indicate
-- the same trace with different information.Hence those strings are delimited
-- using '@' and considered as part of single slice.

-- View to hold slice ids(sid) and the assigned slice ids for codec slices.
DROP TABLE IF EXISTS codec_slices;
CREATE PERFETTO TABLE codec_slices AS
WITH
  __codec_slices AS (
    SELECT DISTINCT
      IIF(instr(name, 'android.hardware.') > 0,
        extract_codec_string_after(name, 'android.hardware.'),
        extract_codec_string(name, '(')) AS codec_string,
      slice.id AS sid,
      slice.name AS sname
    FROM slice
    JOIN trace_trait_table ON slice.name GLOB trace_trait
  ),
  _codec_slices AS (
    SELECT DISTINCT codec_string,
      ROW_NUMBER() OVER() AS codec_slice_idx
    FROM __codec_slices
    GROUP BY codec_string
  )
SELECT
  codec_slice_idx,
  a.codec_string,
  sid
FROM __codec_slices a
JOIN _codec_slices b USING(codec_string);

-- Combine slice and and cpu dur and cycles info
DROP TABLE IF EXISTS codec_slice_cpu_running;
CREATE PERFETTO TABLE codec_slice_cpu_running AS
SELECT
  codec_string,
  MIN(ts) AS min_ts,
  MAX(ts + t.dur) AS max_ts,
  SUM(t.dur) AS sum_dur_ns,
  SUM(ct.cpu_time) AS cpu_run_ns,
  AVG(t.dur) AS avg_dur_ns,
  CAST(SUM(millicycles) AS INT64) AS cpu_cycles,
  CAST(SUM(megacycles) AS INT64) AS cpu_mega_cycles,
  IIF(INSTR(cc.thread_name, 'binder') > 0,
    'binder', cc.thread_name) as thread_name_mod,
  cc.process_name as process_name,
  COUNT (*) as count
FROM codec_slices
JOIN thread_slice t ON(sid = t.id)
JOIN thread_slice_cpu_cycles cc ON(sid = cc.id)
JOIN thread_slice_cpu_time ct ON(sid = ct.id)
GROUP BY codec_slice_idx, thread_name_mod, cc.process_name;

-- Codec framework message latencies
DROP TABLE IF EXISTS  fwk_looper_msg_latency;
CREATE PERFETTO TABLE fwk_looper_msg_latency AS
SELECT
  slice.name AS normalized_name,
  NULL AS process_name,
  extract_codec_string(slice.name,'::') AS thread_name,
  COUNT(*) AS count,
  MAX(slice.dur/1e3) AS max_us,
  MIN(slice.dur/1e3) AS min_us,
  AVG(slice.dur)/1e3 AS avg_us,
  SUM(slice.dur)/1e3 AS agg_us
FROM slice
WHERE slice.name GLOB '*::Looper_msg*'
GROUP BY slice.name, thread_name, process_name;

-- Qualifying individual slice latency
DROP TABLE IF EXISTS latency_codec_slices;
CREATE PERFETTO TABLE latency_codec_slices AS
WITH
  _filtered_raw_codec_flattened_slices AS(
    SELECT
      IIF(instr(name, 'android.hardware.') > 0,
        extract_codec_string_after(name, 'android.hardware.'),
        extract_codec_string(name, '(')) AS normalized_name,
    slice_id,
    root_id,
    dur,
    IIF(INSTR(thread_name, 'binder') > 0,
    'binder', thread_name) as thread_name,
    process_name,
    depth
    FROM _slice_flattened
    JOIN trace_trait_table ON name GLOB trace_trait
  ),
  _filtered_agg_codec_flattened_slices AS(
    SELECT
      root_id,
      normalized_name,
      process_name,
      thread_name,
      SUM(dur)/1e3 AS _agg_us
    FROM _filtered_raw_codec_flattened_slices
    GROUP BY slice_id, root_id
    ORDER BY depth)
SELECT
  normalized_name,
  process_name,
  thread_name,
  COUNT(*) as count,
  MAX(_agg_us) as max_us,
  MIN(_agg_us) as min_us,
  AVG(_agg_us) as avg_us,
  SUM(_agg_us) as agg_us
  FROM _filtered_agg_codec_flattened_slices
  GROUP BY normalized_name, thread_name, process_name
UNION ALL
SELECT
  normalized_name as codec_string,
  process_name,
  thread_name,
  count,
  max_us,
  min_us,
  avg_us,
  agg_us
  FROM fwk_looper_msg_latency;

-- POWER consumed during codec use.
DROP VIEW IF EXISTS codec_power_mw;
CREATE PERFETTO VIEW codec_power_mw AS
SELECT
  AndroidCodecMetrics_Rail_Info (
    'energy', tot_used_power,
    'power_mw', tot_used_power / (powrail_end_ts - powrail_start_ts)
  ) AS proto,
  name
FROM avg_used_powers;

-- Generate proto for the trace
DROP VIEW IF EXISTS metrics_per_slice_type;
CREATE PERFETTO VIEW metrics_per_slice_type AS
SELECT
  COALESCE(codec_string, normalized_name) as codec_string,
  COALESCE(rslice.process_name, lat.process_name) as process_name,
  COALESCE(lat.thread_name, rslice.thread_name_mod) as thread_name,
  max_us,
  min_us,
  avg_us,
  agg_us,
  lat.count AS count,
  AndroidCodecMetrics_Detail(
    'total_cpu_ns', CAST(sum_dur_ns AS INT64),
    'running_cpu_ns', CAST(cpu_run_ns AS INT64),
    'avg_running_cpu_ns', CAST((cpu_run_ns/rslice.count) AS INT64),
    'avg_time_ns', CAST(avg_dur_ns AS INT64),
    'total_cpu_cycles', CAST(cpu_cycles AS INT64),
    'avg_cpu_cycles', CAST((cpu_cycles/rslice.count) AS INT64),
    'count', rslice.count,
    'self', AndroidCodecMetrics_Detail_Latency (
      'max_us', CAST(max_us AS INT64),
      'min_us', CAST(min_us AS INT64),
      'avg_us', CAST(avg_us AS INT64),
      'agg_us', CAST(agg_us AS INT64),
      'count', lat.count
    )
  ) AS proto
FROM latency_codec_slices lat
FULL JOIN codec_slice_cpu_running  rslice
ON codec_string = lat.normalized_name
AND lat.thread_name = rslice.thread_name_mod
AND lat.process_name = rslice.process_name;

-- Generating codec framework cpu metric
DROP VIEW IF EXISTS codec_metrics_output;
CREATE PERFETTO VIEW codec_metrics_output AS
SELECT AndroidCodecMetrics (
  'cpu_usage', (
    SELECT RepeatedField(
      AndroidCodecMetrics_CpuUsage(
        'process_name', process_name,
        'thread', AndroidCodecMetrics_CpuUsage_ThreadInfo(
          'name', thread_name,
          'info', AndroidCodecMetrics_CpuUsage_ThreadInfo_Details(
            'thread_cpu_ns', CAST((runtime) AS INT64),
            'core_data', proto
          )
        )
      )
    ) FROM cpu_cycles_runtime
  ),
  'codec_function', (
    SELECT RepeatedField (
      AndroidCodecMetrics_CodecFunction(
        'codec_string', codec_string,
        'process', AndroidCodecMetrics_CodecFunction_Process(
          'name', process_name,
          'thread', AndroidCodecMetrics_CodecFunction_Process_Thread(
            'name', thread_name,
            'detail', metrics_per_slice_type.proto
          )
        )
      )
    ) FROM metrics_per_slice_type
  ),
  'energy', (
    AndroidCodecMetrics_Energy(
      'total_energy', (SELECT SUM(tot_used_power) FROM avg_used_powers),
      'duration', (SELECT MAX(powrail_end_ts) - MIN(powrail_start_ts)  FROM avg_used_powers),
      'power_mw', (SELECT SUM(tot_used_power) /  (MAX(powrail_end_ts) - MIN(powrail_start_ts)) FROM avg_used_powers),
      'rail', (
        SELECT RepeatedField (
          AndroidCodecMetrics_Rail (
            'name', name,
            'info', codec_power_mw.proto
          )
        ) FROM codec_power_mw
      )
    )
  )
);
