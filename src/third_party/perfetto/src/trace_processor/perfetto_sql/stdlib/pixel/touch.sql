-- Copyright (C) 2026 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- you may obtain a copy of the License at
--
--      http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

INCLUDE PERFETTO MODULE intervals.intersect;

CREATE PERFETTO TABLE _pixel_touch_twoshay_events AS
WITH
  base AS (
    SELECT
      s.id AS twoshay_slice_id,
      s.ts AS twoshay_ts,
      s.dur AS twoshay_dur,
      cast_int!(STR_SPLIT(STR_SPLIT(s.name, 'IN_TS=', 1), '.', 0)) AS in_ts
    FROM slice AS s
    WHERE
      s.name GLOB 'algo->processFrame:*'
  )
SELECT
  twoshay_slice_id,
  twoshay_ts,
  twoshay_dur,
  in_ts,
  cast_int!(IFNULL(
      (
        SELECT c.value
        FROM counter AS c
        WHERE
          c.track_id
          = (
            SELECT id FROM track WHERE name = 'Resample latency offset' LIMIT 1
          )
          AND c.ts >= twoshay_ts
          AND c.ts <= twoshay_ts + twoshay_dur
        LIMIT 1
      ),
      0
    )) AS resample_latency_offset
FROM base;

CREATE PERFETTO TABLE _pixel_touch_bottom_half_events AS
WITH
  bh_base AS (
    SELECT
      id AS bh_slice_id,
      ts AS bh_start_ts,
      dur AS bh_dur,
      track_id,
      cast_int!(STR_SPLIT(STR_SPLIT(name, 'IRQ_IDX=', 1), '.', 0)) AS irq_idx
    FROM slice
    WHERE
      name GLOB 'gti_irq_thread_fn: IRQ_IDX=*'
  ),
  bh_with_in_ts AS (
    SELECT
      b.*,
      (
        SELECT cast_int!(STR_SPLIT(STR_SPLIT(s.name, 'IN_TS=', 1), '.', 0))
        FROM slice AS s
        WHERE
          s.parent_id = b.bh_slice_id
          AND s.name GLOB 'goog_offload_populate_frame:*'
        LIMIT 1
      ) AS in_ts
    FROM bh_base AS b
  )
SELECT * FROM bh_with_in_ts;

CREATE PERFETTO TABLE _pixel_touch_top_half_events AS
WITH
  irq_counters AS (
    SELECT id, ts, 0 AS dur, value, track_id
    FROM counter
    WHERE
      track_id = (SELECT id FROM track WHERE name = 'gti_th_irq_index' LIMIT 1)
  ),
  irq_slices AS (
    SELECT id, ts, dur, track_id
    FROM slice
    WHERE
      name GLOB 'IRQ (*)'
      AND dur > 0
  )
SELECT
  s.id AS th_slice_id,
  s.ts AS th_start_ts,
  s.dur AS th_dur,
  s.track_id AS th_track_id,
  c.value AS irq_idx
FROM _interval_intersect!((irq_slices, irq_counters), ()) AS ii
JOIN irq_slices AS s
  ON s.id = ii.id_0
JOIN irq_counters AS c
  ON c.id = ii.id_1;

-- Pixel-specific touch events, including top and bottom half IRQ, and Twoshay touch processing.
CREATE PERFETTO TABLE pixel_touch_events(
  -- Hardware input event timestamp parsed from Twoshay driver logs.
  in_ts LONG,
  -- Start timestamp of the top-half touch IRQ.
  ts_pixel_touch_th TIMESTAMP,
  -- Duration of the top-half touch IRQ.
  dur_pixel_touch_th DURATION,
  -- Slice ID of the top-half touch IRQ.
  id_pixel_touch_th LONG,
  -- Track ID of the top-half touch IRQ.
  track_pixel_touch_th JOINID(track.id),
  -- Start timestamp of the bottom-half touch IRQ thread.
  ts_pixel_touch_bh TIMESTAMP,
  -- Duration of the bottom-half touch IRQ thread.
  dur_pixel_touch_bh DURATION,
  -- Slice ID of the bottom-half touch IRQ thread.
  id_pixel_touch_bh LONG,
  -- Track ID of the bottom-half touch IRQ thread.
  track_pixel_touch_bh JOINID(track.id),
  -- Start timestamp of the Twoshay processing frame.
  ts_pixel_touch TIMESTAMP,
  -- Duration of the Twoshay processing frame.
  dur_pixel_touch DURATION,
  -- Slice ID of the Twoshay processing frame.
  id_pixel_touch LONG,
  -- Track ID of the Twoshay processing frame.
  track_pixel_touch JOINID(track.id),
  -- Resample latency offset (in nanoseconds).
  resample_latency_offset LONG
)
AS
SELECT
  t.in_ts AS in_ts,
  th.th_start_ts AS ts_pixel_touch_th,
  th.th_dur AS dur_pixel_touch_th,
  th.th_slice_id AS id_pixel_touch_th,
  th.th_track_id AS track_pixel_touch_th,
  bh.bh_start_ts AS ts_pixel_touch_bh,
  bh.bh_dur AS dur_pixel_touch_bh,
  bh.bh_slice_id AS id_pixel_touch_bh,
  bh.track_id AS track_pixel_touch_bh,
  t.twoshay_ts AS ts_pixel_touch,
  t.twoshay_dur AS dur_pixel_touch,
  t.twoshay_slice_id AS id_pixel_touch,
  s.track_id AS track_pixel_touch,
  t.resample_latency_offset AS resample_latency_offset
FROM _pixel_touch_twoshay_events AS t
LEFT JOIN _pixel_touch_bottom_half_events AS bh
  ON bh.in_ts = t.in_ts
LEFT JOIN _pixel_touch_top_half_events AS th
  ON th.irq_idx = bh.irq_idx
LEFT JOIN slice AS s
  ON s.id = t.twoshay_slice_id;
