DROP VIEW IF EXISTS fastrpc_timeline;
CREATE PERFETTO VIEW fastrpc_timeline AS
SELECT
  ts,
  LEAD(ts, 1, trace_end())
  OVER(PARTITION BY track_id ORDER BY ts) - ts AS dur,
  RTRIM(SUBSTR(name, 13), ']') AS subsystem_name,
  track_id,
  value
FROM counter JOIN counter_track
  ON counter.track_id = counter_track.id
WHERE (name GLOB 'mem.fastrpc[[]*');

DROP VIEW IF EXISTS fastrpc_subsystem_stats;
CREATE PERFETTO VIEW fastrpc_subsystem_stats AS
SELECT
  subsystem_name,
  SUM(value * dur) / SUM(dur) AS avg_size,
  MIN(value) AS min_size,
  MAX(value) AS max_size
FROM fastrpc_timeline
GROUP BY 1;

DROP VIEW IF EXISTS fastrpc_raw_allocs;
CREATE PERFETTO VIEW fastrpc_raw_allocs AS
SELECT
  RTRIM(SUBSTR(name, 20), ']') AS subsystem_name,
  ts,
  value AS instant_value,
  SUM(value) OVER win AS value
FROM counter c JOIN thread_counter_track t ON c.track_id = t.id
WHERE name GLOB 'mem.fastrpc_change*' AND value > 0
WINDOW win AS (
  PARTITION BY name ORDER BY ts
  ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
);

DROP VIEW IF EXISTS fastrpc_alloc_stats;
CREATE PERFETTO VIEW fastrpc_alloc_stats AS
SELECT
  subsystem_name,
  SUM(instant_value) AS total_alloc_size_bytes
FROM fastrpc_raw_allocs
GROUP BY 1;

DROP VIEW IF EXISTS android_fastrpc_output;
CREATE PERFETTO VIEW android_fastrpc_output AS
SELECT AndroidFastrpcMetric(
  'subsystem', RepeatedField(
    AndroidFastrpcMetric_Subsystem(
      'name', subsystem_name,
      'avg_size_bytes', avg_size,
      'min_size_bytes', min_size,
      'max_size_bytes', max_size,
      'total_alloc_size_bytes', total_alloc_size_bytes
    )
  ))
FROM fastrpc_subsystem_stats JOIN fastrpc_alloc_stats USING (subsystem_name);
