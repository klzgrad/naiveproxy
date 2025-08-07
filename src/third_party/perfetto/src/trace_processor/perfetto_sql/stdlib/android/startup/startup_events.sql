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

INCLUDE PERFETTO MODULE slices.with_context;

-- All activity startup events.
CREATE PERFETTO TABLE _startup_events AS
SELECT
  ts,
  dur,
  ts + dur AS ts_end,
  str_split(s.name, ": ", 1) AS package_name
FROM process_slice AS s
WHERE
  s.name GLOB 'launching: *'
  AND (
    process_name IS NULL OR process_name = 'system_server'
  );
