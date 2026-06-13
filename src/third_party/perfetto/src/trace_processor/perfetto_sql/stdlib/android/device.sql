--
-- Copyright 2024 The Android Open Source Project
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

-- Extract name of the device based on metadata from the trace.
CREATE PERFETTO TABLE android_device_name (
  -- Device name.
  name STRING,
  -- Machine identifier
  machine_id JOINID(machine.id)
) AS
WITH
  -- Example str_value:
  -- Android/aosp_raven/raven:VanillaIceCream/UDC/11197703:userdebug/test-keys
  -- Gets substring after first slash;
  after_first_slash(str, machine_id) AS (
    SELECT
      substr(android_build_fingerprint, instr(android_build_fingerprint, '/') + 1) AS str,
      id AS machine_id
    FROM machine
    WHERE
      android_build_fingerprint IS NOT NULL
  ),
  -- Gets substring after second slash
  after_second_slash(str, machine_id) AS (
    SELECT
      substr(str, instr(str, '/') + 1) AS str,
      machine_id
    FROM after_first_slash
  ),
  -- Gets substring after second slash and before the colon
  before_colon(str, machine_id) AS (
    SELECT
      substr(str, 0, instr(str, ':')) AS str,
      machine_id
    FROM after_second_slash
  )
SELECT
  str AS name,
  machine_id
FROM before_colon;
