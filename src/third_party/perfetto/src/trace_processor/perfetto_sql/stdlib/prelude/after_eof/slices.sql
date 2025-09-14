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

INCLUDE PERFETTO MODULE prelude.after_eof.views;

-- Given two slice ids, returns whether the first is an ancestor of the second.
CREATE PERFETTO FUNCTION slice_is_ancestor(
    -- Id of the potential ancestor slice.
    ancestor_id LONG,
    -- Id of the potential descendant slice.
    descendant_id LONG
)
-- Whether `ancestor_id` slice is an ancestor of `descendant_id`.
RETURNS BOOL AS
SELECT
  ancestor.track_id = descendant.track_id
  AND ancestor.ts <= descendant.ts
  AND (
    ancestor.dur = -1 OR ancestor.ts + ancestor.dur >= descendant.ts + descendant.dur
  )
FROM slice AS ancestor, slice AS descendant
WHERE
  ancestor.id = $ancestor_id AND descendant.id = $descendant_id;
