--
-- Copyright 22025 The Android Open Source Project
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

-- Contains information about Android users in the trace.
--
-- This is populated by the `android.user_list` data-source which lives in
-- traced_probes and is available on default from 26Q2+ devices.
--
-- Note: `users` here corresponds to Android users *not* Linux users. So
-- this is not about Linux uids (which in Android correspons to different
-- apps).
CREATE PERFETTO VIEW android_user_list (
  -- The Android user id.
  --
  -- Often useful to join with `android_process_metadata.user_id`
  android_user_id LONG,
  -- A string "enum" indicating the type of the user according to the system.
  --
  -- Will be one for a few constant values e.g. HEADLESS, SECONDARY, GUEST.
  type STRING
) AS
SELECT
  android_user_id,
  type
FROM __intrinsic_android_user_list;
