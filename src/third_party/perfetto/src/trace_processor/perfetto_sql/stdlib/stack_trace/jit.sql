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

-- Represents a jitted code snippet.
-- TODO(carlscab): Make public
CREATE PERFETTO VIEW _jit_code (
  -- Unique jit code id.
  jit_code_id LONG,
  -- Time this code was created / allocated.
  create_ts TIMESTAMP,
  -- Time this code was destroyed / deallocated. This is a upper bound, as we
  -- can only detect deletions indirectly when new code is allocated overlapping
  -- existing one.
  estimated_delete_ts TIMESTAMP,
  -- Thread that generated the code.
  utid JOINID(thread.id),
  -- Start address for the generated code.
  start_address LONG,
  -- Size in bytes of the generated code.
  size LONG,
  -- Function name.
  function_name STRING,
  -- Jitted code (binary data).
  native_code BYTES
) AS
SELECT
  id AS jit_code_id,
  create_ts,
  estimated_delete_ts,
  utid,
  start_address,
  size,
  function_name,
  base64_decode(native_code_base64) AS native_code
FROM __intrinsic_jit_code;

-- Represents a jitted frame.
-- TODO(carlscab): Make public
CREATE PERFETTO VIEW _jit_frame (
  -- Jitted code snipped the frame is in (joins with _jit_code.jit_code_id).
  jit_code_id LONG,
  -- Jitted frame (joins with stack_profile_frame.id).
  frame_id LONG
) AS
SELECT
  jit_code_id,
  frame_id
FROM __intrinsic_jit_frame;
