--
-- Copyright 2022 The Android Open Source Project
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
INCLUDE PERFETTO MODULE android.cujs.base;
INCLUDE PERFETTO MODULE android.cujs.cuj_frame_counters;

DROP TABLE IF EXISTS android_jank_cuj_counter_metrics;
CREATE PERFETTO TABLE android_jank_cuj_counter_metrics AS
-- Order CUJs to get the ts of the next CUJ with the same name.
-- This is to avoid selecting counters logged for the next CUJ in case multiple
-- CUJs happened in a short succession.
WITH cujs_ordered AS (
  SELECT
    cuj_id,
    cuj_name,
    cuj_slice_name,
    upid,
    state,
    ts_end,
    CASE
      WHEN process_name GLOB 'com.android.*' THEN ts_end
      WHEN process_name = 'com.google.android.apps.nexuslauncher' THEN ts_end
      -- Some processes publish counters just before logging the CUJ end
      ELSE MAX(ts, ts_end - 4000000)
    END AS ts_earliest_allowed_counter,
    LEAD(ts_end) OVER (PARTITION BY cuj_name ORDER BY ts_end ASC) AS ts_end_next_cuj
  FROM android_jank_cuj
)
SELECT
  cuj_id,
  cuj_name,
  upid,
  state,
  _android_jank_cuj_counter_value(cuj_name, 'totalFrames', ts_earliest_allowed_counter, ts_end_next_cuj) AS total_frames,
  _android_jank_cuj_counter_value(cuj_name, 'missedFrames', ts_earliest_allowed_counter, ts_end_next_cuj) AS missed_frames,
  _android_jank_cuj_counter_value(cuj_name, 'missedAppFrames', ts_earliest_allowed_counter, ts_end_next_cuj) AS missed_app_frames,
  _android_jank_cuj_counter_value(cuj_name, 'missedSfFrames', ts_earliest_allowed_counter, ts_end_next_cuj) AS missed_sf_frames,
  _android_jank_cuj_counter_value(cuj_name, 'maxSuccessiveMissedFrames', ts_earliest_allowed_counter, ts_end_next_cuj) AS missed_frames_max_successive,
  _android_jank_cuj_counter_value(cuj_name, 'totalAnimTime', ts_earliest_allowed_counter, ts_end_next_cuj) AS anim_duration_ms,
  -- weighted jank is stored in janks per ms in the counters, since the counters are ints.
  _android_jank_cuj_counter_value(cuj_name, 'weightedAppJank', ts_earliest_allowed_counter, ts_end_next_cuj) / 1000.0 AS weighted_missed_app_frames,
  _android_jank_cuj_counter_value(cuj_name, 'weightedSfJank', ts_earliest_allowed_counter, ts_end_next_cuj) / 1000.0 AS weighted_missed_sf_frames,
  -- convert ms to nanos to align with the unit for `dur` in the other tables
  _android_jank_cuj_counter_value(cuj_name, 'maxFrameTimeMillis', ts_earliest_allowed_counter, ts_end_next_cuj) * 1000000 AS frame_dur_max,
  _android_cuj_missed_vsyncs_for_callback(cuj_slice_name, ts_earliest_allowed_counter, ts_end_next_cuj, '*SF*') AS sf_callback_missed_frames,
  _android_cuj_missed_vsyncs_for_callback(cuj_slice_name, ts_earliest_allowed_counter, ts_end_next_cuj, '*HWUI*') AS hwui_callback_missed_frames
FROM cujs_ordered cuj;
