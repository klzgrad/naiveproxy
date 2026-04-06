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

-- Android inputmethod clients state dumps (from android.inputmethod data source).
CREATE PERFETTO VIEW android_inputmethod_clients (
  -- Dump id
  id LONG,
  -- Timestamp when the dump was triggered
  ts TIMESTAMP,
  -- Extra args parsed from the proto message
  arg_set_id ARGSETID
) AS
SELECT
  id,
  ts,
  arg_set_id
FROM __intrinsic_inputmethod_clients;

-- Android inputmethod manager service state dumps (from android.inputmethod data source).
CREATE PERFETTO VIEW android_inputmethod_manager_service (
  -- Dump id
  id LONG,
  -- Timestamp when the dump was triggered
  ts TIMESTAMP,
  -- Extra args parsed from the proto message
  arg_set_id ARGSETID
) AS
SELECT
  id,
  ts,
  arg_set_id
FROM __intrinsic_inputmethod_manager_service;

-- Android inputmethod service state dumps (from android.inputmethod data source).
CREATE PERFETTO VIEW android_inputmethod_service (
  -- Dump id
  id LONG,
  -- Timestamp when the dump was triggered
  ts TIMESTAMP,
  -- Extra args parsed from the proto message
  arg_set_id ARGSETID
) AS
SELECT
  id,
  ts,
  arg_set_id
FROM __intrinsic_inputmethod_service;
