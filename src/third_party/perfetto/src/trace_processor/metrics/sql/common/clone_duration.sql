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

INCLUDE PERFETTO MODULE traced.stats;

DROP VIEW IF EXISTS clone_duration_output;
CREATE PERFETTO VIEW clone_duration_output AS
SELECT
  CloneDuration(
    'by_buffer', (
      SELECT
        RepeatedField(CloneDuration_ByBuffer(
          'buffer',
          buffer_id,
          'duration_ns',
          duration_ns
        ))
      FROM traced_clone_flush_latency
    )
  );
