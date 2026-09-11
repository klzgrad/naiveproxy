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

INCLUDE PERFETTO MODULE android.slices;

-- Table of ShadeListBuilder.buildList slices
DROP TABLE IF EXISTS shade_list_builder_build_list_slices;
CREATE PERFETTO TABLE shade_list_builder_build_list_slices AS
SELECT
  s.name name,
  dur,
  s.id id
FROM slice s
  JOIN thread_track ON thread_track.id = s.track_id
  JOIN thread USING (utid)
WHERE
  thread.is_main_thread AND
  s.dur > 0 AND (
    s.name GLOB 'ShadeListBuilder.buildList'
  );

-- Table of ShadeListBuilder.buildList slices with the descendants
DROP TABLE IF EXISTS slices_and_descendants;
CREATE PERFETTO TABLE slices_and_descendants AS
SELECT
  parent.name name,
  descendant.name descendant_name,
  parent.dur dur_ns,
  parent.id id
FROM shade_list_builder_build_list_slices parent
LEFT JOIN descendant_slice(parent.id) AS descendant;