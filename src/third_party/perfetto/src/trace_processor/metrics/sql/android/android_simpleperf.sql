--
-- Copyright 2021 The Android Open Source Project
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


-- Find all counters from track that satisfies regex 'slc/qurg2_(wr|rd):lvl=0x(0|1|3|7)%'
DROP VIEW IF EXISTS all_qurg2;
CREATE PERFETTO VIEW all_qurg2 AS
SELECT
  ts,
  track_id,
  name,
  value
FROM counters
WHERE name GLOB 'slc/qurg2_??:lvl=0x_*';

-- Find all counters from track that satisfies regex 'slc/qurg2_(wr|rd):lvl=0x(1|3|7)%'
DROP VIEW IF EXISTS non_zero_qurg2;
CREATE PERFETTO VIEW non_zero_qurg2 AS
SELECT
  *
FROM all_qurg2
WHERE name NOT GLOB 'slc/qurg2_??:lvl=0x0*';

-- Find all event counters from simpleperf in the form of
-- (<event_name> + '_tid' + <tid> + '_cpu' + <cpu_id>)
DROP VIEW IF EXISTS simpleperf_event_raw;
CREATE PERFETTO VIEW simpleperf_event_raw AS
SELECT
  SUBSTR(name, 0, tid_pos) AS name,
  CAST(SUBSTR(name, tid_pos + 4, cpu_pos - tid_pos - 4) AS INT) AS tid,
  CAST(SUBSTR(name, cpu_pos + 4) AS INT) AS cpu,
  total
FROM (
  SELECT
    name,
    INSTR(name, '_tid') AS tid_pos,
    INSTR(name, '_cpu') AS cpu_pos,
    SUM(value) AS total
  FROM counters
  WHERE name GLOB '*_tid*_cpu*'
  GROUP BY name
);

DROP VIEW IF EXISTS simpleperf_event_per_process;
CREATE PERFETTO VIEW simpleperf_event_per_process AS
SELECT
  e.name,
  t.upid,
  RepeatedField(
    AndroidSimpleperfMetric_PerfEventMetric_Thread(
      'tid', e.tid,
      'name', t.name,
      'cpu', e.cpu,
      'total', e.total
    )
  ) AS threads,
  SUM(e.total) AS total
FROM simpleperf_event_raw e JOIN thread t USING (tid)
GROUP BY e.name, t.upid;


DROP VIEW IF EXISTS simpleperf_event_metric;
CREATE PERFETTO VIEW simpleperf_event_metric AS
SELECT
  AndroidSimpleperfMetric_PerfEventMetric(
    'name', e.name,
    'processes', RepeatedField(
      AndroidSimpleperfMetric_PerfEventMetric_Process(
        'pid', p.pid,
        'name', p.name,
        'threads', e.threads,
        'total', e.total
      )
    ),
    'total', SUM(total)
  ) AS proto
FROM simpleperf_event_per_process e JOIN process p USING (upid)
GROUP BY e.name;

DROP VIEW IF EXISTS android_simpleperf_output;
CREATE PERFETTO VIEW android_simpleperf_output AS
SELECT AndroidSimpleperfMetric(
  'urgent_ratio', (SELECT sum(value) FROM non_zero_qurg2) / (SELECT sum(value) FROM all_qurg2),
  'events', (SELECT RepeatedField(proto) FROM simpleperf_event_metric)
);
