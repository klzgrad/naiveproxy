--
-- Copyright 2022 The Android Open Source Project
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

-- To get mangled stack profile mappings.
SELECT RUN_METRIC('android/unsymbolized_frames.sql');

DROP VIEW IF EXISTS chrome_unsymbolized_args_view;

CREATE PERFETTO VIEW chrome_unsymbolized_args_view AS
SELECT ChromeUnsymbolizedArgs_Arg(
    'module', spm.name,
    'build_id', spm.build_id,
    'address', unsymbolized_arg.rel_pc,
    'google_lookup_id', spm.google_lookup_id
) AS arg_proto
FROM
  (
    SELECT arg_rel_pc.rel_pc AS rel_pc, arg_mapping_id.mapping_id AS mapping_id
    FROM
      (
        SELECT arg_set_id, int_value AS rel_pc
        FROM args
        WHERE key = "chrome_mojo_event_info.mojo_interface_method.native_symbol.rel_pc"
      ) arg_rel_pc
    JOIN
      (
        SELECT
          arg_set_id,
          int_value AS mapping_id
        FROM args
        WHERE key = "chrome_mojo_event_info.mojo_interface_method.native_symbol.mapping_id"
      ) arg_mapping_id
      ON arg_rel_pc.arg_set_id = arg_mapping_id.arg_set_id
  ) unsymbolized_arg
JOIN mangled_stack_profile_mapping spm
  ON unsymbolized_arg.mapping_id = spm.id
WHERE spm.build_id != '';

DROP VIEW IF EXISTS chrome_unsymbolized_args_output;

CREATE PERFETTO VIEW chrome_unsymbolized_args_output AS
SELECT ChromeUnsymbolizedArgs(
    'args',
    (SELECT RepeatedField(arg_proto) FROM chrome_unsymbolized_args_view)
);
