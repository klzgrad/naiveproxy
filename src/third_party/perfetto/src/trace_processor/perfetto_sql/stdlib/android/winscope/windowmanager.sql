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

-- Android WindowManager (from android.windowmanager data source).
CREATE PERFETTO VIEW android_windowmanager (
  -- Snapshot id
  id LONG,
  -- Timestamp when the snapshot was triggered
  ts TIMESTAMP,
  -- Extra args parsed from the proto message
  arg_set_id ARGSETID,
  -- Raw proto message
  base64_proto_id LONG,
  -- Focused display id for this snapshot
  focused_display_id LONG,
  -- Indicates whether snapshot was recorded without elapsed timestamp
  has_invalid_elapsed_ts BOOL
) AS
SELECT
  id,
  ts,
  arg_set_id,
  base64_proto_id,
  focused_display_id,
  has_invalid_elapsed_ts
FROM __intrinsic_windowmanager;

-- Android WindowManager WindowContainer (from android.windowmanager data source).
CREATE PERFETTO VIEW android_windowmanager_windowcontainer (
  -- Row id
  id LONG,
  -- Snapshot id
  snapshot_id LONG,
  -- Extra args parsed from the proto message
  arg_set_id ARGSETID,
  -- Raw proto message
  base64_proto_id LONG,
  -- The window container's title
  title STRING,
  -- The window container's token
  token LONG,
  -- The parent window container's token
  parent_token LONG,
  -- The index of this window container within the parent's children
  child_index LONG,
  -- The window container visibility
  is_visible BOOL,
  -- The rect corresponding to this window container
  window_rect_id LONG,
  -- The window container type e.g. DisplayContent, TaskFragment
  container_type STRING,
  -- Optional name override for some container types
  name_override STRING
) AS
SELECT
  id,
  snapshot_id,
  arg_set_id,
  base64_proto_id,
  title,
  token,
  parent_token,
  child_index,
  is_visible,
  window_rect_id,
  container_type,
  name_override
FROM __intrinsic_windowmanager_windowcontainer;
