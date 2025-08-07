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

-- On Linux/Android perfetto profilers (heapprofd, traced_perf) record the
-- .note.gnu.build-id as build_id. Within Google, this can be used as a lookup
-- key for most cases, but not for Chrome/Webview. Chrome is special and stores
-- symbols indexed by "breadkpad module ID". Breakpad module ID can be derived
-- with the following formula:
--      base::StrCat({module_id->substr(6, 2), module_id->substr(4, 2),
--                    module_id->substr(2, 2), module_id->substr(0, 2),
--                    module_id->substr(10, 2), module_id->substr(8, 2),
--                    module_id->substr(14, 2), module_id->substr(12, 2),
--                    module_id->substr(16, 16), "0"});
-- See also https://source.chromium.org/chromium/chromium/src/+/main:services/tracing/public/cpp/stack_sampling/tracing_sampler_profiler.cc;l=603;drc=cba00174ca338153b9c4f0c31ddbabaac7dd38c7
-- Note that in SQL SUBSTR() indexes are 1-based, not 0 based.
DROP VIEW IF EXISTS mangled_stack_profile_mapping;
CREATE PERFETTO VIEW mangled_stack_profile_mapping AS
SELECT
  id,
  name,
  build_id,
  CASE ((name GLOB '*libmonochrome_64.so'
    OR name GLOB '*libchrome.so'
    OR name GLOB '*libmonochrome.so'
    OR name GLOB '*libwebviewchromium.so'
    OR name GLOB '*libchromium_android_linker.so'
  ) AND length(build_id) >= 40)
  WHEN 0 THEN build_id
  ELSE (
    SUBSTR(build_id, 7, 2)
    || SUBSTR(build_id, 5, 2)
    || SUBSTR(build_id, 3, 2)
    || SUBSTR(build_id, 1, 2)
    || SUBSTR(build_id, 11, 2)
    || SUBSTR(build_id, 9, 2)
    || SUBSTR(build_id, 15, 2)
    || SUBSTR(build_id, 13, 2)
    || SUBSTR(build_id, 17, 16)
    || '0')
  END AS google_lookup_id
FROM stack_profile_mapping;

DROP VIEW IF EXISTS unsymbolized_frames_view;
CREATE PERFETTO VIEW unsymbolized_frames_view AS
SELECT UnsymbolizedFrames_Frame(
    'module', spm.name,
    'build_id', spm.build_id,
    'address', spf.rel_pc,
    'google_lookup_id', spm.google_lookup_id
) AS frame_proto
FROM stack_profile_frame spf
JOIN mangled_stack_profile_mapping spm
  ON spf.mapping = spm.id
WHERE spm.build_id != ''
  AND spf.symbol_set_id IS NULL;

DROP VIEW IF EXISTS unsymbolized_frames_output;
CREATE PERFETTO VIEW unsymbolized_frames_output AS
SELECT UnsymbolizedFrames(
  'frames',
  (SELECT RepeatedField(frame_proto) FROM unsymbolized_frames_view)
);
