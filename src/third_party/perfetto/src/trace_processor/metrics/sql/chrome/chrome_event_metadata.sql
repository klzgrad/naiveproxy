--
-- Copyright 2020 The Android Open Source Project
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
--

-- Creates a view containing name-value pairs extracted from
-- chrome_event.metadata. Some names can have multiple values:
-- e.g. trace-category
DROP VIEW IF EXISTS chrome_event_metadata;
CREATE PERFETTO VIEW chrome_event_metadata AS
WITH metadata (arg_set_id) AS (
  SELECT arg_set_id
  FROM __intrinsic_chrome_raw
  WHERE name = "chrome_event.metadata"
)
-- TODO(b/173201788): Once this is fixed, extract all the fields.
SELECT 'os-name' AS name,
  EXTRACT_ARG(metadata.arg_set_id, 'os-name') AS value
FROM metadata
UNION
SELECT "trace-category" AS name,
  value AS category
FROM metadata, json_each(
    json_extract(
      EXTRACT_ARG(metadata.arg_set_id, "trace-config"),
      '$.included_categories'
    )
  );
