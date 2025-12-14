--
-- Copyright 2023 The Android Open Source Project
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

-- Events when CPU entered hypervisor.
CREATE PERFETTO VIEW pkvm_hypervisor_events (
  -- Id of the corresponding slice in slices table.
  slice_id JOINID(slice.id),
  -- CPU that entered hypervisor.
  cpu LONG,
  -- Timestamp when CPU entered hypervisor.
  ts TIMESTAMP,
  -- How much time CPU spent in hypervisor.
  dur DURATION,
  -- Reason for entering hypervisor (e.g. host_hcall, host_mem_abort), or NULL if unknown.
  reason STRING
) AS
SELECT
  slices.id AS slice_id,
  cpu_track.cpu AS cpu,
  slices.ts AS ts,
  slices.dur AS dur,
  extract_arg(slices.arg_set_id, 'hyp_enter_reason') AS reason
FROM slices
JOIN cpu_track
  ON cpu_track.id = slices.track_id
WHERE
  slices.category = 'pkvm_hyp';
