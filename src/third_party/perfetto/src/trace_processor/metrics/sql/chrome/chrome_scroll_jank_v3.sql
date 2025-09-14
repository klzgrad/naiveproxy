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

INCLUDE PERFETTO MODULE chrome.scroll_jank.scroll_jank_v3;

DROP VIEW IF EXISTS chrome_scroll_jank_v3_causes_per_scroll;

-- An "intermediate" view with jank causes per scroll.
--
-- @column scroll_id                   The ID of the scroll.
-- @column max_delay_since_last_frame  The maximum time a frame was delayed
--                                     after the presentation of the previous
--                                     frame.
-- @column vsync_interval              The expected vsync interval.
-- @column scrolls                     A proto amalgamation of each scroll jank
--                                     cause including cause name, sub cause and
--                                     the duration of the delay since the
--                                     previous frame was presented.
CREATE PERFETTO VIEW chrome_scroll_jank_v3_causes_per_scroll AS
SELECT
  scroll_id,
  max(1.0 * delay_since_last_frame / vsync_interval) AS max_delay_since_last_frame,
  -- MAX does not matter, since `vsync_interval` is the computed as the same
  -- value for a single trace.
  max(vsync_interval) AS vsync_interval,
  repeatedfield(
    chromescrolljankv3_scroll_scrolljankcause(
      'cause',
      cause_of_jank,
      'sub_cause',
      sub_cause_of_jank,
      'delay_since_last_frame',
      1.0 * delay_since_last_frame / vsync_interval
    )
  ) AS scroll_jank_causes
FROM chrome_janky_frames
GROUP BY
  scroll_id;

DROP VIEW IF EXISTS chrome_scroll_jank_v3_intermediate;

-- An "intermediate" view for computing `chrome_scroll_jank_v3_output` below.
--
-- @column trace_num_frames          The number of frames in the trace.
-- @column trace_num_janky_frames    The number of delayed/janky frames in the
--                                   trace.
-- @column vsync_interval            The standard vsync interval.
-- @column scrolls                   A proto amalgamation of metrics per scroll
--                                   including the number of frames, number of
--                                   janky frames, percent of janky frames,
--                                   maximum presentation delay, and the causes
--                                   of jank (cause, sub-cause, delay).
CREATE PERFETTO VIEW chrome_scroll_jank_v3_intermediate AS
SELECT
  -- MAX does not matter for these aggregations, since the values are the
  -- same across rows.
  (SELECT COUNT(*) FROM chrome_frame_info_with_delay)
    AS trace_num_frames,
  (SELECT COUNT(*) FROM chrome_janky_frames)
    AS trace_num_janky_frames,
  causes.vsync_interval,
  RepeatedField(
    ChromeScrollJankV3_Scroll(
      'num_frames',
      frames.num_frames,
      'num_janky_frames',
      frames.num_janky_frames,
      'scroll_jank_percentage',
      frames.scroll_jank_percentage,
      'max_delay_since_last_frame',
      causes.max_delay_since_last_frame,
      'scroll_jank_causes',
      causes.scroll_jank_causes))
    AS scrolls
FROM
  chrome_frames_per_scroll AS frames
INNER JOIN chrome_scroll_jank_v3_causes_per_scroll AS causes
  ON frames.scroll_id = causes.scroll_id;

DROP VIEW IF EXISTS chrome_scroll_jank_v3_output;

-- For producing a "native" Perfetto UI metric.
--
-- @column scroll_jank_summary     A proto amalgamation summarizing all of the
--                                 scroll jank in a trace, including the number
--                                 of frames, janky frames, percentage of janky
--                                 frames, vsync interval, and a summary of this
--                                 data (including individual causes) for each
--                                 scroll.
CREATE PERFETTO VIEW chrome_scroll_jank_v3_output AS
SELECT
  ChromeScrollJankV3(
    'trace_num_frames',
    trace_num_frames,
    'trace_num_janky_frames',
    trace_num_janky_frames,
    'trace_scroll_jank_percentage',
    100.0 * trace_num_janky_frames / trace_num_frames,
    'vsync_interval_ms',
    vsync_interval,
    'scrolls',
    scrolls) AS scroll_jank_summary
FROM
  chrome_scroll_jank_v3_intermediate;
