--
-- Copyright 2025 The Android Open Source Project
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

-- This module primarily performs composition between the blocking calls, frames and CUJs.
-- This is used for capturing blocking call per frame metrics, and the related plugins.
INCLUDE PERFETTO MODULE android.critical_blocking_calls;

INCLUDE PERFETTO MODULE android.cujs.sysui_cujs;

-- For cases when a blocking call starts within a frame, but does not end before the actual frame
-- ends, a part of the blocking call can be missed while calculating the metric. To avoid this
-- issue, the frame boundary is extended, spanning from the start of the current actual frame, till
-- the start of the next actual frame.
CREATE PERFETTO VIEW _extended_frame_boundary AS
SELECT
  frame_ts AS ts,
  ui_thread_utid,
  frame_id,
  layer_id,
  cuj_id,
  cuj_name,
  -- Calculate the end timestamp (ts_end) by taking the start time (frame_ts) of the next frame in the session.
  -- For the last frame, fall back to the default ts_end.
  coalesce(lead(frame_ts) OVER (PARTITION BY cuj_id ORDER BY frame_id ASC), ts_end) AS ts_end,
  frame_id
FROM _android_frames_in_cuj
ORDER BY
  frame_id;

-- Capture blocking call duration within frames within a CUJ.
CREATE PERFETTO VIEW _blocking_calls_frame_cuj AS
SELECT
  min(bc.dur, frame.ts_end - bc.ts, bc.ts_end - frame.ts) AS dur,
  max(frame.ts, bc.ts) AS ts,
  bc.upid,
  bc.name,
  bc.process_name,
  bc.utid,
  frame.frame_id,
  frame.layer_id,
  cuj_id,
  cuj_name
FROM _android_critical_blocking_calls AS bc
JOIN _extended_frame_boundary AS frame
  ON bc.utid = frame.ui_thread_utid
-- The following condition to accommodate blocking call crossing frame boundary. The blocking
-- call starts in a frame or ends in a frame. It can either be the same frame or a different
-- frame.
WHERE
  (
    -- Blocking call starts within the frame.
    (
      bc.ts >= frame.ts AND bc.ts <= frame.ts_end
    )
    OR (
      bc.ts_end >= frame.ts AND bc.ts_end <= frame.ts_end
    )
  );
