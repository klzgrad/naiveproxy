--
-- Copyright 2025 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the 'License');
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an 'AS IS' BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

-- All hard IRQs of the trace represented as slices.
CREATE PERFETTO VIEW linux_hard_irqs (
  -- Starting timestamp of this IRQ.
  ts TIMESTAMP,
  -- Duration of this IRQ.
  dur DURATION,
  -- The name of the IRQ.
  name STRING,
  -- The id of the IRQ.
  id JOINID(slice.id),
  -- The id of this IRQ's parent IRQ (i.e. the IRQ that this IRQ preempted).
  parent_id JOINID(slice.id)
) AS
SELECT
  slices.ts,
  slices.dur,
  slices.name,
  slices.id,
  slices.parent_id
FROM slices
JOIN track
  ON track.id = slices.track_id
WHERE
  track.type = 'cpu_irq';

-- All soft IRQs of the trace represented as slices.
CREATE PERFETTO VIEW linux_soft_irqs (
  -- Starting timestamp of this IRQ.
  ts TIMESTAMP,
  -- Duration of this IRQ.
  dur DURATION,
  -- The name of the IRQ.
  name STRING,
  -- The id of the IRQ.
  id JOINID(slice.id)
) AS
SELECT
  slices.ts,
  slices.dur,
  slices.name,
  slices.id
FROM slices
JOIN track
  ON track.id = slices.track_id
WHERE
  track.type = 'cpu_softirq';

-- All IRQs, including hard and soft IRQs, of the trace represented as slices.
CREATE PERFETTO VIEW linux_irqs (
  -- Starting timestamp of this IRQ.
  ts TIMESTAMP,
  -- Duration of this IRQ.
  dur DURATION,
  -- The name of the IRQ.
  name STRING,
  -- The id of the IRQ.
  id JOINID(slice.id),
  -- The id of this IRQ's parent IRQ (i.e. the IRQ that this IRQ preempted).
  parent_id JOINID(slice.id),
  -- Flag indicating if IRQ is soft IRQ
  is_soft_irq BOOL
) AS
SELECT
  ts,
  dur,
  name,
  id,
  parent_id,
  0 AS is_soft_irq
FROM linux_hard_irqs
UNION ALL
SELECT
  ts,
  dur,
  name,
  id,
  NULL AS parent_id,
  1 AS is_soft_irq
FROM linux_soft_irqs;
