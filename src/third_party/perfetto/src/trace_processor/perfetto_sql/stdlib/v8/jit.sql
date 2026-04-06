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

-- These are the tables for the V8 jit data source
-- (protos/perfetto/trace/chrome/v8.proto).
--
-- All events are associated to a V8 isolate instance. There can be multiple
-- instances associated to a given thread, although this is rare.
--
-- Generated code in V8 is allocated in the V8 heap (in a special executeable
-- section), this means that code can be garbage collected (when no longer used)
-- or can be moved around (e.g. during heap compactation). This means that a
-- given callsite might correspond to function `A` at one point in time and to
-- function `B` later on.
-- In addition V8 code has various levels of optimization, so a function might
-- have multiple associated code snippets.
--
-- V8 does not track code deletion, so we have to indirectly infer it by
-- detecting code overlaps, if a newer code creation event overlaps with older
-- code we need to asume that the old code was deleted. Code moves are logged,
-- and there is an event to track those.

-- A V8 Isolate instance. A V8 Isolate represents an isolated instance of the V8
-- engine.
CREATE PERFETTO VIEW v8_isolate (
  -- Unique V8 isolate id.
  v8_isolate_id LONG,
  -- Process the isolate was created in.
  upid JOINID(process.id),
  -- Internal id used by the v8 engine. Unique in a process.
  internal_isolate_id LONG,
  -- Absolute start address of the embedded code blob.
  embedded_blob_code_start_address LONG,
  -- Size in bytes of the embedded code blob.
  embedded_blob_code_size LONG,
  -- Base address of the code range if the isolate defines one.
  code_range_base_address LONG,
  -- Size of a code range if the isolate defines one.
  code_range_size LONG,
  -- Whether the code range for this Isolate is shared with others in the same
  -- process. There is at max one such shared code range per process.
  shared_code_range LONG,
  -- Used when short builtin calls are enabled, where embedded builtins are
  -- copied into the CodeRange so calls can be nearer.
  embedded_blob_code_copy_start_address LONG
) AS
SELECT
  id AS v8_isolate_id,
  upid,
  internal_isolate_id,
  embedded_blob_code_start_address,
  embedded_blob_code_size,
  code_range_base_address,
  code_range_size,
  shared_code_range,
  embedded_blob_code_copy_start_address
FROM __intrinsic_v8_isolate;

-- Represents a script that was compiled to generate code. Some V8 code is
-- generated out of scripts and will reference a V8Script other types of code
-- will not (e.g. builtins).
CREATE PERFETTO VIEW v8_js_script (
  -- Unique V8 JS script id.
  v8_js_script_id LONG,
  -- V8 isolate this script belongs to (joinable with
  -- `v8_isolate.v8_isolate_id`).
  v8_isolate_id LONG,
  -- Script id used by the V8 engine.
  internal_script_id LONG,
  -- Script type.
  script_type STRING,
  -- Script name.
  name STRING,
  -- Actual contents of the script.
  source STRING
) AS
SELECT
  id AS v8_js_script_id,
  v8_isolate_id,
  internal_script_id,
  script_type,
  name,
  source
FROM __intrinsic_v8_js_script;

-- Represents one WASM script.
CREATE PERFETTO VIEW v8_wasm_script (
  -- Unique V8 WASM script id.
  v8_wasm_script_id LONG,
  -- V8 Isolate this script belongs to (joinable with
  -- `v8_isolate.v8_isolate_id`).
  v8_isolate_id LONG,
  -- Script id used by the V8 engine.
  internal_script_id LONG,
  -- URL of the source.
  url STRING,
  -- Raw wire bytes of the script.
  wire_bytes BYTES,
  -- Actual source code of the script.
  source STRING
) AS
SELECT
  id AS v8_wasm_script_id,
  v8_isolate_id,
  internal_script_id,
  url,
  base64_decode(wire_bytes_base64) AS wire_bytes,
  source
FROM __intrinsic_v8_wasm_script;

-- Represents a v8 Javascript function.
CREATE PERFETTO VIEW v8_js_function (
  -- Unique V8 JS function id.
  v8_js_function_id LONG,
  -- Function name.
  name STRING,
  -- Script where the function is defined (joinable with
  -- `v8_js_script.v8_js_script_id`).
  v8_js_script_id LONG,
  -- Whether this function represents the top level script.
  is_toplevel BOOL,
  -- Function kind (e.g. regular function or constructor).
  kind STRING,
  -- Line in script where function is defined. Starts at 1.
  line LONG,
  -- Column in script where function is defined. Starts at 1.
  col LONG
) AS
SELECT
  id AS v8_js_function_id,
  name,
  v8_js_script_id,
  is_toplevel,
  kind,
  line,
  col
FROM __intrinsic_v8_js_function;

-- This index is crucial for the performance of the callstack profiling standard
-- library (`callstacks.stack_profile`). The library performs a join with the
-- `_v8_js_code` view on `jit_code_id`. Without this index on the underlying
-- table, the join becomes a major performance bottleneck, causing long module
-- import times on traces with V8 data.
CREATE PERFETTO INDEX _intrinsic_v8_js_code_jit_code_id_index ON __intrinsic_v8_js_code(jit_code_id);

-- Represents a v8 code snippet for a Javascript function. A given function can
-- have multiple code snippets (e.g. for different compilation tiers, or as the
-- function moves around the heap).
-- TODO(carlscab): Make public once `_jit_code` is public too
CREATE PERFETTO VIEW _v8_js_code (
  -- Unique id
  id LONG,
  -- Associated jit code. Set for all tiers except IGNITION. Joinable with
  -- `_jit_code.jit_code_id`.
  jit_code_id LONG,
  -- JS function for this snippet. Joinable with
  -- `v8_js_function.v8_js_function_id`.
  v8_js_function_id LONG,
  -- Compilation tier
  tier STRING,
  -- V8 VM bytecode. Set only for the IGNITION tier.
  bytecode BYTES
) AS
SELECT
  id,
  jit_code_id,
  v8_js_function_id,
  tier,
  base64_decode(bytecode_base64) AS bytecode
FROM __intrinsic_v8_js_code;

-- Represents a v8 code snippet for a v8 internal function.
-- TODO(carlscab): Make public once `_jit_code` is public too
CREATE PERFETTO VIEW _v8_internal_code (
  -- Unique id
  id LONG,
  -- Associated jit code. Joinable with `_jit_code.jit_code_id`.
  jit_code_id LONG,
  -- V8 Isolate this code was created in. Joinable with
  -- `v8_isolate.v8_isolate_id`.
  v8_isolate_id LONG,
  -- Function name.
  function_name STRING,
  -- Type of internal code.
  code_type STRING
) AS
SELECT
  id,
  jit_code_id,
  v8_isolate_id,
  function_name,
  code_type
FROM __intrinsic_v8_internal_code;

-- Represents the code associated to a WASM function.
-- TODO(carlscab): Make public once `_jit_code` is public too
CREATE PERFETTO VIEW _v8_wasm_code (
  -- Unique id
  id LONG,
  -- Associated jit code. Joinable with `_jit_code.jit_code_id`.
  jit_code_id LONG,
  -- V8 Isolate this code was created in. Joinable with
  -- `v8_isolate.v8_isolate_id`.
  v8_isolate_id LONG,
  -- Script where the function is defined. Joinable with
  -- `v8_wasm_script.v8_wasm_script_id`.
  v8_wasm_script_id LONG,
  -- Function name.
  function_name STRING,
  -- Compilation tier.
  tier STRING,
  -- Offset into the WASM module where the function starts.
  code_offset_in_module LONG
) AS
SELECT
  id,
  jit_code_id,
  v8_isolate_id,
  v8_wasm_script_id,
  function_name,
  tier,
  code_offset_in_module
FROM __intrinsic_v8_wasm_code;

-- Represents the code associated to a regular expression
-- TODO(carlscab): Make public once `_jit_code` is public too
CREATE PERFETTO VIEW _v8_regexp_code (
  -- Unique id
  id LONG,
  -- Associated jit code. Joinable with `_jit_code.jit_code_id`.
  jit_code_id LONG,
  -- V8 Isolate this code was created in. Joinable with
  -- `v8_isolate.v8_isolate_id`.
  v8_isolate_id LONG,
  -- The pattern the this regular expression was compiled from.
  pattern STRING
) AS
SELECT
  id,
  jit_code_id,
  v8_isolate_id,
  pattern
FROM __intrinsic_v8_regexp_code;
