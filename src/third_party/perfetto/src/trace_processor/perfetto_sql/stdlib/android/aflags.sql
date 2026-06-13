--
-- Copyright (C) 2026 The Android Open Source Project
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

-- Android aconfig flags ("aflags") are a configuration system used to manage
-- feature rollout and behavior across the Android platform.
--
-- For more details on aconfig, see:
-- https://source.android.com/docs/setup/build/feature-flagging/declare-flag
--
-- This view presents snapshots of the state of these flags as captured
-- during the trace. Data is collected by the `android.aflags` data-source,
-- which periodically polls the `aflags` tool on Android devices.
-- Each row in this view represents the state of a single flag at a specific
-- point in time (the `ts` column).
CREATE PERFETTO VIEW android_aflags (
  -- Timestamp of the flag snapshot.
  ts TIMESTAMP,
  -- Package name of the flag (e.g. "com.android.window.flags").
  package STRING,
  -- Name of the flag within the package (e.g. "enable_multi_window").
  name STRING,
  -- The namespace this flag belongs to (often used for grouping).
  flag_namespace STRING,
  -- The container for the flag (e.g. "system", "product").
  container STRING,
  -- Current effective value of the flag at this timestamp.
  value STRING,
  -- The value this flag will take after the next reboot, if a change has
  -- been staged.
  staged_value STRING,
  -- Whether the flag is read-only or read-write.
  permission STRING,
  -- Where the current value was picked from (e.g. "default", "local", "server").
  value_picked_from STRING,
  -- The underlying storage backend for this flag (e.g. "aconfigd", "device_config").
  storage_backend STRING,
  -- Value type of the flag (e.g. "boolean", "integer").
  type STRING
) AS
SELECT
  ts,
  package,
  name,
  flag_namespace,
  container,
  value,
  staged_value,
  permission,
  value_picked_from,
  storage_backend,
  type
FROM __intrinsic_android_aflags;
