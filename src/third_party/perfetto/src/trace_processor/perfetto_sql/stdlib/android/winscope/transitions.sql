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

-- Android transition participants (from com.android.wm.shell.transition data source).
CREATE PERFETTO VIEW android_window_manager_shell_transition_participants (
  -- Transition id
  transition_id LONG,
  -- Layer participant
  layer_id LONG,
  -- Window participant
  window_id LONG
) AS
SELECT
  transition_id,
  layer_id,
  window_id
FROM __intrinsic_window_manager_shell_transition_participants;

-- Android transition protos (from com.android.wm.shell.transition data source).
CREATE PERFETTO VIEW android_window_manager_shell_transition_protos (
  -- Transition id
  transition_id LONG,
  -- Base64 proto id
  base64_proto_id LONG
) AS
SELECT
  transition_id,
  base64_proto_id
FROM __intrinsic_window_manager_shell_transition_protos;
