--
-- Copyright 2019 The Android Open Source Project
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

INCLUDE PERFETTO MODULE stacks.symbolization_candidates;

-- Keeping this for backward compatibility for the time being.
DROP VIEW IF EXISTS mangled_stack_profile_mapping;
CREATE PERFETTO VIEW mangled_stack_profile_mapping AS
SELECT
  id,
  name,
  build_id,
  COALESCE(_to_breakpad_module(name, build_id), build_id) AS google_lookup_id
FROM stack_profile_mapping;

DROP VIEW IF EXISTS unsymbolized_frames_view;
CREATE PERFETTO VIEW unsymbolized_frames_view AS
SELECT UnsymbolizedFrames_Frame(
    'module', module,
    'build_id', build_id,
    'address', rel_pc,
    'google_lookup_id', google_lookup_id
) AS frame_proto
FROM (
  SELECT DISTINCT module, build_id, rel_pc, COALESCE(breakpad_module_id, build_id) AS google_lookup_id
  FROM _stacks_symbolization_candidates
);

DROP VIEW IF EXISTS unsymbolized_frames_output;
CREATE PERFETTO VIEW unsymbolized_frames_output AS
SELECT UnsymbolizedFrames(
  'frames',
  (SELECT RepeatedField(frame_proto) FROM unsymbolized_frames_view)
);
