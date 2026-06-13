--
-- Copyright 2026 The Android Open Source Project
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

-- Maps GPU graphics context IDs to process and API type.
--
-- Each row represents a unique graphics context seen in the trace,
-- populated from InternedGraphicsContext data attached to
-- GpuRenderStageEvent packets.
CREATE PERFETTO VIEW gpu_context (
  -- The id of the row.
  id ID,
  -- The graphics context handle (GL context, VkDevice, CUDA context, etc.).
  context_id LONG,
  -- Process ID associated with this context.
  pid LONG,
  -- Graphics API type (OPEN_GL, VULKAN, OPEN_CL, CUDA, HIP, or UNDEFINED).
  api STRING
) AS
SELECT
  *
FROM __intrinsic_gpu_context;
