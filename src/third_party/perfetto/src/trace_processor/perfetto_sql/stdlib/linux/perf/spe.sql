--
-- Copyright 2024 The Android Open Source Project
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

-- Contains ARM Statistical Profiling Extension records
CREATE PERFETTO VIEW linux_perf_spe_record (
  -- Timestap when the operation was sampled
  ts TIMESTAMP,
  -- Thread the operation executed in
  utid JOINID(thread.id),
  -- Exception level the instruction was executed in
  exception_level STRING,
  -- Instruction virtual address
  instruction_frame_id LONG,
  -- Type of operation sampled
  operation STRING,
  -- The virtual address accessed by the operation (0 if no memory access was
  -- performed)
  data_virtual_address LONG,
  -- The physical address accessed by the operation (0 if no memory access was
  -- performed)
  data_physical_address LONG,
  -- Cycle count from the operation being dispatched for issue to the operation
  -- being complete.
  total_latency LONG,
  -- Cycle count from the operation being dispatched for issue to the operation
  -- being issued for execution.
  issue_latency LONG,
  -- Cycle count from a virtual address being passed to the MMU for translation
  -- to the result of the translation being available.
  translation_latency LONG,
  -- Where the data returned for a load operation was sourced
  data_source STRING,
  -- Operation generated an exception
  exception_gen BOOL,
  -- Operation architecturally retired
  retired BOOL,
  -- Operation caused a level 1 data cache access
  l1d_access BOOL,
  -- Operation caused a level 1 data cache refill
  l1d_refill BOOL,
  -- Operation caused a TLB access
  tlb_access BOOL,
  -- Operation caused a TLB refill involving at least one translation table walk
  tlb_refill BOOL,
  -- Conditional instruction failed its condition code check
  not_taken BOOL,
  -- Whether a branch caused a correction to the predicted program flow
  mispred BOOL,
  -- Operation caused a last level data or unified cache access
  llc_access BOOL,
  -- Whether the operation could not be completed by the last level data cache
  -- (or any above)
  llc_refill BOOL,
  -- Operation caused an access to another socket in a multi-socket system
  remote_access BOOL,
  -- Operation that incurred additional latency due to the alignment of the
  -- address and the size of the data being accessed
  alignment BOOL,
  -- Whether the operation executed in transactional state
  tme_transaction BOOL,
  -- SVE or SME operation with at least one false element in the governing
  -- predicate(s)
  sve_partial_pred BOOL,
  -- SVE or SME operation with no true element in the governing predicate(s)
  sve_empty_pred BOOL,
  -- Whether a load operation caused a cache access to at least the level 2 data
  -- or unified cache
  l2d_access BOOL,
  -- Whether a load operation accessed and missed the level 2 data or unified
  -- cache. Not set for accesses that are satisfied from refilling data of a
  -- previous miss
  l2d_hit BOOL,
  -- Whether a load operation accessed modified data in a cache
  cache_data_modified BOOL,
  -- Wheter a load operation hit a recently fetched line in a cache
  recenty_fetched BOOL,
  -- Whether a load operation snooped data from a cache outside the cache
  -- hierarchy of this core
  data_snooped BOOL
) AS
SELECT
  ts,
  utid,
  exception_level,
  instruction_frame_id,
  operation,
  data_virtual_address,
  data_physical_address,
  total_latency,
  issue_latency,
  translation_latency,
  data_source,
  (
    events_bitmask & (
      1 << 0
    )
  ) != 0 AS exception_gen,
  (
    events_bitmask & (
      1 << 1
    )
  ) != 0 AS retired,
  (
    events_bitmask & (
      1 << 2
    )
  ) != 0 AS l1d_access,
  (
    events_bitmask & (
      1 << 3
    )
  ) != 0 AS l1d_refill,
  (
    events_bitmask & (
      1 << 4
    )
  ) != 0 AS tlb_access,
  (
    events_bitmask & (
      1 << 5
    )
  ) != 0 AS tlb_refill,
  (
    events_bitmask & (
      1 << 6
    )
  ) != 0 AS not_taken,
  (
    events_bitmask & (
      1 << 7
    )
  ) != 0 AS mispred,
  (
    events_bitmask & (
      1 << 8
    )
  ) != 0 AS llc_access,
  (
    events_bitmask & (
      1 << 9
    )
  ) != 0 AS llc_refill,
  (
    events_bitmask & (
      1 << 10
    )
  ) != 0 AS remote_access,
  (
    events_bitmask & (
      1 << 11
    )
  ) != 0 AS alignment,
  (
    events_bitmask & (
      1 << 17
    )
  ) != 0 AS tme_transaction,
  (
    events_bitmask & (
      1 << 17
    )
  ) != 0 AS sve_partial_pred,
  (
    events_bitmask & (
      1 << 18
    )
  ) != 0 AS sve_empty_pred,
  (
    events_bitmask & (
      1 << 19
    )
  ) != 0 AS l2d_access,
  (
    events_bitmask & (
      1 << 20
    )
  ) != 0 AS l2d_hit,
  (
    events_bitmask & (
      1 << 21
    )
  ) != 0 AS cache_data_modified,
  (
    events_bitmask & (
      1 << 22
    )
  ) != 0 AS recenty_fetched,
  (
    events_bitmask & (
      1 << 23
    )
  ) != 0 AS data_snooped
FROM __intrinsic_spe_record;
