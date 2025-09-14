--
-- Copyright 2025 The Android Open Source Project
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

-- Android surfaceflinger transactions (from android.surfaceflinger.transactions data source).
CREATE PERFETTO VIEW android_surfaceflinger_transaction (
  -- Row id
  id LONG,
  -- Snapshot id
  snapshot_id LONG,
  -- Arg set id
  arg_set_id LONG,
  -- Transaction id
  transaction_id LONG,
  -- PID
  pid LONG,
  -- UID
  uid LONG,
  -- Layer id
  layer_id LONG,
  -- Display id
  display_id LONG,
  -- Flags id
  flags_id LONG,
  -- Transaction type
  transaction_type STRING
) AS
SELECT
  id,
  snapshot_id,
  arg_set_id,
  transaction_id,
  pid,
  uid,
  layer_id,
  display_id,
  flags_id,
  transaction_type
FROM __intrinsic_surfaceflinger_transaction;

-- Android surfaceflinger transaction flags.
CREATE PERFETTO VIEW android_surfaceflinger_transaction_flag (
  -- Flags id
  flags_id LONG,
  -- Flag
  flag STRING
) AS
SELECT
  flags_id,
  flag
FROM __intrinsic_surfaceflinger_transaction_flag;

-- Android surfaceflinger displays (from android.surfaceflinger.layers data source).
CREATE PERFETTO VIEW android_surfaceflinger_display (
  -- Id
  id LONG,
  -- Snapshot id
  snapshot_id LONG,
  -- Is on
  is_on LONG,
  -- Is virtual
  is_virtual LONG,
  -- Trace rect id
  trace_rect_id LONG,
  -- Display id
  display_id LONG,
  -- Display name
  display_name STRING
) AS
SELECT
  *
FROM __intrinsic_surfaceflinger_display;

-- Android surfaceflinger input rect fill regions (from android.surfaceflinger.layers data source).
CREATE PERFETTO VIEW android_winscope_fill_region (
  -- Fill region id
  id LONG,
  -- Trace rect id
  trace_rect_id LONG,
  -- Rect id
  rect_id LONG
) AS
SELECT
  *
FROM __intrinsic_winscope_fill_region;
