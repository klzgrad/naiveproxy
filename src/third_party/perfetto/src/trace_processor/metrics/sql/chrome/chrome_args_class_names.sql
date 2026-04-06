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

-- Creates a table view with a list of class names with a specific key
-- from the "args" table per |package_name| & |version_code|.
-- |package_name| and |version_code| can be NULL.

DROP VIEW IF EXISTS chrome_args_class_names_per_version;
CREATE PERFETTO VIEW chrome_args_class_names_per_version AS
WITH class_info AS (
  SELECT
    package_list.package_name AS package_name,
    package_list.version_code AS version_code,
    RepeatedField(args.string_value) AS class_names
  FROM args
  JOIN slice
    ON args.arg_set_id = slice.arg_set_id
      AND args.flat_key = 'android_view_dump.activity.view.class_name'
  JOIN thread_track
    ON slice.track_id = thread_track.id
  JOIN thread
    ON thread_track.utid = thread.utid
  JOIN process
    ON thread.upid = process.upid
  LEFT JOIN package_list
    ON process.uid = package_list.uid
  GROUP BY package_name, version_code
)
SELECT
  ChromeArgsClassNames_ChromeArgsClassNamesPerVersion(
    'package_name', package_name,
    'version_code', version_code,
    'class_name', class_names
  ) AS class_names_per_version
FROM class_info;

DROP VIEW IF EXISTS chrome_args_class_names_output;
CREATE PERFETTO VIEW chrome_args_class_names_output
AS
SELECT
  ChromeArgsClassNames(
    'class_names_per_version', RepeatedField(class_names_per_version)
  )
FROM chrome_args_class_names_per_version;
