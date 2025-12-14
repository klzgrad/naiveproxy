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


DROP VIEW IF EXISTS chrome_slice_names_output;

CREATE PERFETTO VIEW chrome_slice_names_output AS
SELECT ChromeSliceNames(
  'chrome_version_code', (
    SELECT RepeatedField(int_value)
    FROM metadata
    WHERE name GLOB 'cr-*playstore_version_code'
    ORDER BY int_value
  ),
  'slice_name', (
    SELECT RepeatedField(DISTINCT(name))
    FROM slice
    WHERE name IS NOT NULL
    ORDER BY name
  )
);
