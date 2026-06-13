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

-- Screenshot slices, used in perfetto UI.
CREATE PERFETTO TABLE android_screenshots (
  -- Id of the screenshot slice.
  id ID(slice.id),
  -- Slice timestamp.
  ts TIMESTAMP,
  -- Slice duration, should be typically 0 since screeenshot slices are of instant
  -- type.
  dur DURATION,
  -- Slice name.
  name STRING
) AS
SELECT
  slice.id AS id,
  slice.ts AS ts,
  slice.dur AS dur,
  slice.name AS name
FROM slice
JOIN args
  USING (arg_set_id)
WHERE
  slice.name = "Screenshot"
  AND slice.category = "android_screenshot"
  AND args.key = "screenshot.jpg_image";
