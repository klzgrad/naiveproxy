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
--

INCLUDE PERFETTO MODULE counters.intervals;

CREATE PERFETTO TABLE _dmabuf_spans AS
WITH
  dmabuf_track AS (
    SELECT
      counter.*
    FROM counter
    JOIN counter_track AS track
      ON track.id = counter.track_id AND track.name = 'mem.dmabuf_rss'
  )
SELECT
  upid,
  ts,
  dur,
  CAST(value AS INTEGER) AS dmabuf_rss
FROM counter_leading_intervals!(dmabuf_track) AS dmabuf_counter
JOIN process_counter_track
  ON dmabuf_counter.track_id = process_counter_track.id;
