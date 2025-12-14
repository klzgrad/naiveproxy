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

-- @module prelude.after_eof.cpu_scheduling
-- CPU scheduling and thread state analysis.
--
-- This module provides tables and views for analyzing CPU scheduling behavior,
-- including scheduling slices, thread states, and CPU information.

-- Contains information about the CPUs on the device this trace was taken on.
CREATE PERFETTO VIEW cpu (
  -- Unique identifier for this CPU. Identical to |ucpu|, prefer using |ucpu|
  -- instead.
  id ID,
  -- Unique identifier for this CPU. Isn't equal to |cpu| for remote machines
  -- and is equal to |cpu| for the host machine.
  ucpu ID,
  -- The 0-based CPU core identifier.
  cpu LONG,
  -- The cluster id is shared by CPUs in the same cluster.
  cluster_id LONG,
  -- A string describing this core.
  processor STRING,
  -- Machine identifier, non-null for CPUs on a remote machine.
  machine_id LONG,
  -- Capacity of a CPU of a device, a metric which indicates the
  -- relative performance of a CPU on a device
  -- For details see:
  -- https://www.kernel.org/doc/Documentation/devicetree/bindings/arm/cpu-capacity.txt
  capacity LONG,
  -- Extra key/value pairs associated with this cpu.
  arg_set_id ARGSETID
) AS
SELECT
  id,
  id AS ucpu,
  cpu,
  cluster_id,
  processor,
  machine_id,
  capacity,
  arg_set_id
FROM __intrinsic_cpu
WHERE
  cpu IS NOT NULL;

-- Contains the frequency values that the CPUs on the device are capable of
-- running at.
CREATE PERFETTO VIEW cpu_available_frequencies (
  -- Unique identifier for this cpu frequency.
  id ID,
  -- The CPU for this frequency, meaningful only in single machine traces.
  -- For multi-machine, join with the `cpu` table on `ucpu` to get the CPU
  -- identifier of each machine.
  cpu LONG,
  -- CPU frequency in KHz.
  freq LONG,
  -- The CPU that the slice executed on (meaningful only in single machine
  -- traces). For multi-machine, join with the `cpu` table on `ucpu` to get the
  -- CPU identifier of each machine.
  ucpu LONG
) AS
SELECT
  id,
  ucpu AS cpu,
  freq,
  ucpu
FROM __intrinsic_cpu_freq;

-- This table holds slices with kernel thread scheduling information. These
-- slices are collected when the Linux "ftrace" data source is used with the
-- "sched/switch" and "sched/wakeup*" events enabled.
--
-- The rows in this table will always have a matching row in the |thread_state|
-- table with |thread_state.state| = 'Running'
CREATE PERFETTO VIEW sched_slice (
  --  Unique identifier for this scheduling slice.
  id ID,
  -- The timestamp at the start of the slice.
  ts TIMESTAMP,
  -- The duration of the slice.
  dur DURATION,
  -- The CPU that the slice executed on (meaningful only in single machine
  -- traces). For multi-machine, join with the `cpu` table on `ucpu` to get the
  -- CPU identifier of each machine.
  cpu LONG,
  -- The thread's unique id in the trace.
  utid JOINID(thread.id),
  -- A string representing the scheduling state of the kernel
  -- thread at the end of the slice.  The individual characters in
  -- the string mean the following: R (runnable), S (awaiting a
  -- wakeup), D (in an uninterruptible sleep), T (suspended),
  -- t (being traced), X (exiting), P (parked), W (waking),
  -- I (idle), N (not contributing to the load average),
  -- K (wakeable on fatal signals) and Z (zombie, awaiting
  -- cleanup).
  end_state STRING,
  -- The kernel priority that the thread ran at.
  priority LONG,
  -- The unique CPU identifier that the slice executed on.
  ucpu LONG
) AS
SELECT
  id,
  ts,
  dur,
  ucpu AS cpu,
  utid,
  end_state,
  priority,
  ucpu
FROM __intrinsic_sched_slice;

-- Shorter alias for table `sched_slice`.
CREATE PERFETTO VIEW sched (
  -- Alias for `sched_slice.id`.
  id ID,
  -- Alias for `sched_slice.ts`.
  ts TIMESTAMP,
  -- Alias for `sched_slice.dur`.
  dur DURATION,
  -- Alias for `sched_slice.cpu`.
  cpu LONG,
  -- Alias for `sched_slice.utid`.
  utid JOINID(thread.id),
  -- Alias for `sched_slice.end_state`.
  end_state STRING,
  -- Alias for `sched_slice.priority`.
  priority LONG,
  -- Alias for `sched_slice.ucpu`.
  ucpu LONG,
  -- Legacy column, should no longer be used.
  ts_end LONG
) AS
SELECT
  *,
  ts + dur AS ts_end
FROM sched_slice;

-- This table contains the scheduling state of every thread on the system during
-- the trace.
--
-- The rows in this table which have |state| = 'Running', will have a
-- corresponding row in the |sched_slice| table.
CREATE PERFETTO VIEW thread_state (
  -- Unique identifier for this thread state.
  id ID,
  -- The timestamp at the start of the slice.
  ts TIMESTAMP,
  -- The duration of the slice.
  dur DURATION,
  -- The CPU that the thread executed on (meaningful only in single machine
  -- traces). For multi-machine, join with the `cpu` table on `ucpu` to get the
  -- CPU identifier of each machine.
  cpu LONG,
  -- The thread's unique id in the trace.
  utid JOINID(thread.id),
  -- The scheduling state of the thread. Can be "Running" or any of the states
  -- described in |sched_slice.end_state|.
  state STRING,
  -- Indicates whether this thread was blocked on IO.
  io_wait LONG,
  -- The function in the kernel this thread was blocked on.
  blocked_function STRING,
  -- The unique thread id of the thread which caused a wakeup of this thread.
  waker_utid JOINID(thread.id),
  -- The unique thread state id which caused a wakeup of this thread.
  waker_id JOINID(thread_state.id),
  -- Whether the wakeup was from interrupt context or process context.
  irq_context LONG,
  -- The unique CPU identifier that the thread executed on.
  ucpu LONG
) AS
SELECT
  id,
  ts,
  dur,
  ucpu AS cpu,
  utid,
  state,
  io_wait,
  blocked_function,
  waker_utid,
  waker_id,
  irq_context,
  ucpu
FROM __intrinsic_thread_state;
