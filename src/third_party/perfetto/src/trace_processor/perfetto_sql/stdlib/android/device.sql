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

-- Extracts the device code name from an Android build fingerprint.
CREATE PERFETTO FUNCTION android_device_name_from_fingerprint(
  -- Android build fingerprint (e.g. 'Android/aosp_raven/raven:VanillaIceCream/...').
  fingerprint STRING
)
-- Device code name (e.g. 'raven').
RETURNS STRING
AS
SELECT STR_SPLIT(STR_SPLIT($fingerprint, '/', 2), ':', 0);

-- Extract name of the device based on metadata from the trace.
CREATE PERFETTO TABLE android_device_name(
  -- Device name.
  name STRING,
  -- Machine identifier
  machine_id JOINID(machine.id)
)
AS
SELECT
  android_device_name_from_fingerprint(android_build_fingerprint) AS name,
  id AS machine_id
FROM machine
WHERE
  android_build_fingerprint IS NOT NULL;
