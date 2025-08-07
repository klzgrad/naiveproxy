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
--

-- Previous runnable slice on the same thread.
-- For each "Running" thread state finds:
-- - previous "Runnable" (or runnable preempted) state.
-- - previous uninterrupted "Runnable" state with a valid waker thread.
CREATE PERFETTO TABLE sched_previous_runnable_on_thread (
  -- Running thread state
  id JOINID(thread_state.id),
  -- Previous runnable thread state.
  prev_runnable_id JOINID(thread_state.id),
  -- Previous runnable thread state with valid waker thread.
  prev_wakeup_runnable_id JOINID(thread_state.id)
) AS
WITH
  running_and_runnable AS (
    SELECT
      id,
      state,
      max(id) FILTER(WHERE
        state != 'Running') OVER utid_part AS prev_runnable_id,
      max(id) FILTER(WHERE
        NOT waker_utid IS NULL AND (
          irq_context IS NULL OR irq_context != 1
        )) OVER utid_part AS prev_wakeup_runnable_id
    FROM thread_state
    -- Optimal operation for state IN (R, R+, Running)
    WHERE
      state GLOB 'R*' AND dur != -1
    WINDOW utid_part AS (PARTITION BY utid ORDER BY id)
  )
SELECT
  id,
  prev_runnable_id,
  prev_wakeup_runnable_id
FROM running_and_runnable
WHERE
  state = 'Running'
ORDER BY
  id;
