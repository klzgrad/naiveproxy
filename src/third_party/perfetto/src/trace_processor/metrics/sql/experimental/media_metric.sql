--
-- Copyright 2021 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the 'License');
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an 'AS IS' BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

SELECT RUN_METRIC('chrome/chrome_processes.sql');

-- Helper for thread slices
DROP VIEW IF EXISTS thread_slice;
CREATE PERFETTO VIEW thread_slice AS
SELECT s.*, thread.utid, thread.upid
FROM slice s
JOIN thread_track ON s.track_id = thread_track.id
JOIN thread USING(utid);

--------------------------------------------------------------------------------
-- Find all playbacks on renderer main threads.

DROP VIEW IF EXISTS PlaybackStart;
CREATE PERFETTO VIEW PlaybackStart AS
SELECT
  EXTRACT_ARG(s.arg_set_id, 'debug.id') AS playback_id,
  s.ts AS playback_start,
  upid
FROM slice s
JOIN thread_track ON s.track_id = thread_track.id
JOIN thread USING(utid)
WHERE
  s.name = 'WebMediaPlayerImpl::DoLoad'
  AND thread.name = 'CrRendererMain';

--------------------------------------------------------------------------------
-- Find the first video render time after the playback to compute
-- time_to_video_play.

DROP VIEW IF EXISTS VideoStart;
CREATE PERFETTO VIEW VideoStart AS
SELECT
  playback_id,
  playback_start,
  PlaybackStart.upid,
  MIN(s.ts) AS video_start
FROM PlaybackStart, thread_slice s
WHERE
  s.name = 'VideoRendererImpl::Render'
  AND EXTRACT_ARG(s.arg_set_id, 'debug.id') = playback_id
  AND s.upid = PlaybackStart.upid
GROUP BY playback_id, playback_start, PlaybackStart.upid;

--------------------------------------------------------------------------------
-- Find the first audio render time after the playback to compute
-- time_to_audio_play.

DROP VIEW IF EXISTS AudioStart;
CREATE PERFETTO VIEW AudioStart AS
SELECT
  playback_id,
  playback_start,
  PlaybackStart.upid,
  MIN(s.ts) AS audio_start
FROM PlaybackStart, thread_slice s
WHERE
  s.name = 'AudioRendererImpl::Render'
  AND EXTRACT_ARG(s.arg_set_id, 'debug.id') = playback_id
  AND s.upid = PlaybackStart.upid
GROUP BY playback_id, playback_start, PlaybackStart.upid;

--------------------------------------------------------------------------------
-- Sum up the dropped frame count from all such events for each playback to
-- compute dropped_frame_count.

DROP VIEW IF EXISTS DroppedFrameCount;
CREATE PERFETTO VIEW DroppedFrameCount AS
SELECT
  playback_id,
  vs.upid,
  SUM(
    CASE
      WHEN s.arg_set_id IS NULL THEN 0
      ELSE EXTRACT_ARG(s.arg_set_id, 'debug.count') END
  ) AS dropped_frame_count
FROM VideoStart vs
LEFT JOIN thread_slice s ON
  s.name = 'VideoFramesDropped'
  AND EXTRACT_ARG(s.arg_set_id, 'debug.id') = playback_id
  AND s.upid = vs.upid
GROUP BY playback_id, vs.upid;

--------------------------------------------------------------------------------
-- Compute seek times.

-- Find the seeks.
DROP VIEW IF EXISTS SeekStart;
CREATE PERFETTO VIEW SeekStart AS
SELECT
  playback_id,
  PlaybackStart.upid,
  s.ts AS seek_start,
  EXTRACT_ARG(s.arg_set_id, 'debug.target') AS seek_target
FROM PlaybackStart
LEFT JOIN thread_slice s
WHERE
  s.name = 'WebMediaPlayerImpl::DoSeek'
  AND EXTRACT_ARG(s.arg_set_id, 'debug.id') = playback_id
  AND s.upid = PlaybackStart.upid;

-- Partition by the next seek's ts, so that we can filter for events occurring
-- within each seek's window below.
DROP VIEW IF EXISTS SeekPartitioned;
CREATE PERFETTO VIEW SeekPartitioned AS
SELECT
  *,
  LEAD(seek_start) OVER (
    PARTITION BY playback_id, upid
    ORDER BY seek_start ASC
  ) AS seek_end
FROM SeekStart;

-- Find the subsequent matching pipeline seeks that occur before the next seek.
DROP VIEW IF EXISTS PipelineSeek;
CREATE PERFETTO VIEW PipelineSeek AS
SELECT
  seek.*,
  (
    SELECT MIN(s.ts)
    FROM thread_slice s
    WHERE
      s.name = 'WebMediaPlayerImpl::OnPipelineSeeked'
      AND EXTRACT_ARG(s.arg_set_id, 'debug.id') = seek.playback_id
      AND EXTRACT_ARG(s.arg_set_id, 'debug.target') = seek.seek_target
      AND s.upid = seek.upid
      AND s.ts >= seek.seek_start
      AND (seek.seek_end IS NULL OR s.ts < seek.seek_end)
  ) AS pipeline_seek
FROM SeekPartitioned seek;

-- Find the subsequent buffering events that occur before the next seek.
DROP VIEW IF EXISTS SeekComplete;
CREATE PERFETTO VIEW SeekComplete AS
SELECT
  seek.*,
  (
    SELECT MIN(s.ts)
    FROM thread_slice s
    WHERE
      s.name = 'WebMediaPlayerImpl::BufferingHaveEnough'
      AND EXTRACT_ARG(s.arg_set_id, 'debug.id') = seek.playback_id
      AND s.upid = seek.upid
      AND s.ts >= seek.pipeline_seek
      AND (seek.seek_end IS NULL OR s.ts < seek.seek_end)
  ) AS seek_complete
FROM PipelineSeek seek;

-- Find the subsequent buffering events that occur before the next seek.
DROP VIEW IF EXISTS ValidSeek;
CREATE PERFETTO VIEW ValidSeek AS
SELECT
  s.*
FROM SeekComplete s
WHERE
  s.pipeline_seek IS NOT NULL
  AND s.seek_complete IS NOT NULL;

--------------------------------------------------------------------------------
-- Find playback end timestamps and their duration for playbacks without seeks
-- to compute buffering_time.

-- Helper view that shows either video or audio start for each playback
DROP VIEW IF EXISTS AVStart;
CREATE PERFETTO VIEW AVStart AS
SELECT
  v.playback_id,
  v.playback_start,
  v.upid,
  v.video_start AS av_start
FROM VideoStart v
UNION
SELECT
  a.playback_id,
  a.playback_start,
  a.upid,
  a.audio_start AS av_start
FROM AudioStart a
WHERE a.playback_id NOT IN (SELECT playback_id FROM VideoStart);

-- Find the corresponding media end events and their reported duration.
DROP VIEW IF EXISTS PlaybackEnd;
CREATE PERFETTO VIEW PlaybackEnd AS
SELECT
  AVStart.*,
  slice.ts AS playback_end,
  EXTRACT_ARG(slice.arg_set_id, 'debug.duration') * 1e9 AS duration
FROM AVStart
JOIN slice ON slice.id = (
  SELECT s.id
  FROM thread_slice s
  WHERE
    s.name = 'WebMediaPlayerImpl::OnEnded'
    AND EXTRACT_ARG(s.arg_set_id, 'debug.id') = AVStart.playback_id
    AND s.upid = AVStart.upid
  ORDER BY s.ts ASC
  LIMIT 1
  )
WHERE NOT EXISTS (
  SELECT 1 FROM SeekStart
  WHERE SeekStart.playback_id = AVStart.playback_id
);

--------------------------------------------------------------------------------
-- Find maximum video roughness and freezing events per playback.

DROP VIEW IF EXISTS VideoRoughness;
CREATE PERFETTO VIEW VideoRoughness AS
SELECT
  playback_id,
  playback_start,
  PlaybackStart.upid,
  MAX(EXTRACT_ARG(s.arg_set_id, 'debug.roughness')) AS roughness
FROM PlaybackStart
JOIN thread_slice s
WHERE
  s.name = 'VideoPlaybackRoughness'
  AND EXTRACT_ARG(s.arg_set_id, 'debug.id') = playback_id
  AND s.upid = PlaybackStart.upid
GROUP BY playback_id, playback_start, PlaybackStart.upid;

DROP VIEW IF EXISTS VideoFreezing;
CREATE PERFETTO VIEW VideoFreezing AS
SELECT
  playback_id,
  playback_start,
  PlaybackStart.upid,
  MAX(EXTRACT_ARG(s.arg_set_id, 'debug.freezing')) AS freezing
FROM PlaybackStart
JOIN thread_slice s
WHERE
  s.name = 'VideoPlaybackFreezing'
  AND EXTRACT_ARG(s.arg_set_id, 'debug.id') = playback_id
  AND s.upid = PlaybackStart.upid
GROUP BY playback_id, playback_start, PlaybackStart.upid;

--------------------------------------------------------------------------------
-- Output to proto

DROP VIEW IF EXISTS media_metric_output;
CREATE PERFETTO VIEW media_metric_output AS
SELECT MediaMetric(
  'time_to_video_play', (
    SELECT RepeatedField((video_start - playback_start) / 1e6)
    FROM VideoStart
  ),
  'time_to_audio_play', (
    SELECT RepeatedField((audio_start - playback_start) / 1e6)
    FROM AudioStart
  ),
  'dropped_frame_count', (
    SELECT RepeatedField(CAST(dropped_frame_count AS INTEGER))
    FROM DroppedFrameCount
  ),
  'buffering_time', (
    SELECT RepeatedField((playback_end - duration - av_start) / 1e6)
    FROM PlaybackEnd
  ),
  'roughness', (
    SELECT RepeatedField(roughness / 1e0)
    FROM VideoRoughness
  ),
  'freezing', (
    SELECT RepeatedField(freezing / 1e0)
    FROM VideoFreezing
  ),
  'seek_time', (
    SELECT RepeatedField((seek_complete - seek_start) / 1e6)
    FROM ValidSeek
  ),
  'pipeline_seek_time', (
    SELECT RepeatedField((pipeline_seek - seek_start) / 1e6)
    FROM ValidSeek
  )
);
