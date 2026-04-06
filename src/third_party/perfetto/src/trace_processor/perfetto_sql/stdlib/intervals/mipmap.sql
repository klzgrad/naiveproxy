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

INCLUDE PERFETTO MODULE intervals.intersect;

-- ------------------------------------------------------------------
-- Mipmap Macro Definitions
-- ------------------------------------------------------------------

-- _mipmap_bucket_metadata: Calculates time range and bucket counts for each partition.
-- This macro determines the start time, end time, and the number of buckets needed
-- to cover the entire duration of events within each partition defined by _partition_column.
CREATE PERFETTO MACRO _mipmap_bucket_metadata(
    -- The source table containing ts and dur columns, and the partition column.
    _source_table TableOrSubQuery,
    -- The column name to partition the data by (e.g., unique_startup_id).
    _partition_column ColumnName,
    -- The duration of each bucket in nanoseconds
    _bucket_duration_ns Expr
)
RETURNS TableOrSubQuery AS
(
  WITH
    -- Calculate min and max timestamps for each partition.
    mm_constants AS (
      SELECT
        $_partition_column,
        $_bucket_duration_ns AS bucket_duration_ns,
        min(ts) AS trace_min_ts,
        max(ts + dur) AS trace_max_ts
      FROM $_source_table
      GROUP BY
        $_partition_column
    ),
    -- Calculate the total time span for each partition.
    mm_span AS (
      SELECT
        $_partition_column,
        trace_min_ts,
        trace_max_ts,
        bucket_duration_ns,
        (
          trace_max_ts - trace_min_ts
        ) AS total_span
      FROM mm_constants
    )
  -- Output the time range and calculated number of buckets for each partition.
  SELECT
    $_partition_column,
    trace_min_ts,
    trace_max_ts,
    bucket_duration_ns,
    (
      (
        total_span + bucket_duration_ns - 1
      ) / bucket_duration_ns
    ) AS bucket_count
  FROM mm_span
);

-- _mipmap_buckets_table: Generates a table of fixed-duration time buckets(intervals).
-- Each partition (defined by _partition_column) will have its own set of buckets
-- starting from its trace_min_ts. Buckets are assigned a globally unique id.
CREATE PERFETTO MACRO _mipmap_buckets_table(
    -- The source table containing ts, dur, and the partition column.
    _source_table TableOrSubQuery,
    -- The column name to partition the data by.
    _partition_column ColumnName,
    -- The duration of each bucket in nanoseconds.
    _bucket_duration_ns Expr
)
RETURNS TableOrSubQuery AS
(
  WITH
  RECURSIVE   bucket_meta AS (
      SELECT
        $_partition_column,
        trace_min_ts,
        trace_max_ts,
        bucket_duration_ns,
        bucket_count
      FROM _mipmap_bucket_metadata!($_source_table, $_partition_column, $_bucket_duration_ns)
    ),
    -- The RECURSIVE CTE below generates a series of fixed-size time intervals (buckets)
    -- for each partition. It starts with the first bucket at trace_min_ts and
    -- iteratively adds buckets of size _bucket_duration_ns until the entire
    -- time range (trace_max_ts) for the partition is covered.
    buckets_rec($_partition_column, bucket_index, ts, dur) AS (
      -- Base case: the first bucket for each partition.
      SELECT
        $_partition_column,
        0 AS bucket_index,
        bm.trace_min_ts AS ts,
        bm.bucket_duration_ns AS dur
      FROM bucket_meta AS bm
      UNION ALL
      -- Recursive step: generate the next bucket.
      SELECT
        b.$_partition_column,
        b.bucket_index + 1,
        b.ts + bm.bucket_duration_ns,
        bm.bucket_duration_ns
      FROM buckets_rec AS b
      JOIN bucket_meta AS bm
        ON b.$_partition_column = bm.$_partition_column
      WHERE
        b.bucket_index + 1 < bm.bucket_count
    )
  -- Select all generated buckets and assign a globally unique ID.
  SELECT
    -- Globally unique ID for buckets
    row_number() OVER (ORDER BY $_partition_column, bucket_index) AS id,
    $_partition_column,
    bucket_index,
    cast_int!(ts) AS ts,
    dur
  FROM buckets_rec
);

-- _mipmap_merged: Merges slices based on dominant overlap within time buckets.
-- This macro intersects source slices with the generated buckets, determines the
-- dominant slice group (by group_hash) in each bucket, and then merges contiguous
-- buckets that share the same dominant group.
CREATE PERFETTO MACRO _mipmap_merged(
    -- The source table containing slices with unique IDs, partition column, ts, dur, and group_hash.
    _source_slices_with_ids TableOrSubQuery,
    -- The buckets table generated by _mipmap_buckets_table.
    _buckets TableOrSubQuery,
    -- The column name used for partitioning.
    _partition_column ColumnName,
    -- The bucket duration in nanoseconds, matching the one used in _mipmap_buckets_table.
    _bucket_duration_ns Expr
)
RETURNS TableOrSubQuery AS
(
  WITH
    -- Intersect the source slices with the time buckets within each partition.
    mm_intersections AS (
      WITH
        mm_ii AS (
          SELECT
            *
          FROM _interval_intersect !(
          ($_buckets, $_source_slices_with_ids),
          ($_partition_column) -- Partition intersection by the specified colum
        )
        )
      SELECT
        ii.ts AS overlap_ts,
        ii.dur AS overlap_dur,
        b.bucket_index,
        b.ts AS bucket_ts,
        b.dur AS bucket_dur,
        s.$_partition_column,
        s.id AS source_id,
        s.group_hash,
        s.ts AS original_ts,
        s.dur AS original_dur
      FROM mm_ii AS ii
      JOIN $_buckets AS b
        ON b.id = id_0
      JOIN $_source_slices_with_ids AS s
        ON s.id = id_1
    ),
    -- Calculate the total duration of all slice overlaps within each bucket, for each partition.
    mm_bucket_aggregates AS (
      SELECT
        $_partition_column,
        bucket_index,
        bucket_ts AS ts,
        sum(overlap_dur) AS total_bucket_dur
      FROM mm_intersections
      GROUP BY
        $_partition_column,
        bucket_index,
        ts
    ),
    -- Aggregate overlap durations for each group_hash within each bucket, for each partition.
    mm_source_bucket_aggregates AS (
      SELECT
        $_partition_column,
        bucket_index,
        group_hash,
        -- Take the minimum source_id as a representative slice for this group in this bucket.
        min(source_id) AS source_id,
        sum(overlap_dur) AS total_overlap_dur,
        min(original_ts) AS min_original_ts
      FROM mm_intersections
      GROUP BY
        $_partition_column,
        bucket_index,
        group_hash
    ),
    -- Rank group_hashes within each bucket based on total overlap duration.
    -- Higher total_overlap_dur means more dominance. Tie-break by earliest start time.
    mm_ranked AS (
      SELECT
        $_partition_column,
        bucket_index,
        source_id,
        group_hash,
        total_overlap_dur,
        row_number() OVER (PARTITION BY $_partition_column, bucket_index ORDER BY total_overlap_dur DESC, min_original_ts ASC, group_hash ASC) AS rn
      FROM mm_source_bucket_aggregates
    ),
    -- Select the most dominant group_hash for each bucket.
    mm_dominant_events AS (
      SELECT
        $_partition_column,
        bucket_index,
        source_id,
        group_hash
      FROM mm_ranked
      WHERE
        rn = 1
    ),
    -- Combine bucket information with the dominant event details.
    mm_unmerged_buckets AS (
      SELECT
        $_partition_column,
        ba.bucket_index,
        ba.ts,
        ba.total_bucket_dur AS dur,
        de.source_id,
        de.group_hash
      FROM mm_bucket_aggregates AS ba
      JOIN mm_dominant_events AS de
        USING ($_partition_column, bucket_index)
    ),
    -- Detect changes in the dominant group_hash between consecutive buckets.
    mm_with_changes AS (
      SELECT
        *,
        group_hash != coalesce(lag(group_hash) OVER (PARTITION BY $_partition_column ORDER BY bucket_index), '') AS is_change
      FROM mm_unmerged_buckets
    ),
    -- Assign an island_id to group contiguous buckets with the same dominant group_hash.
    mm_with_islands AS (
      SELECT
        *,
        sum(CAST(is_change AS INTEGER)) OVER (PARTITION BY $_partition_column ORDER BY bucket_index ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS island_id
      FROM mm_with_changes
    )
  -- Final SELECT: Aggregate buckets within each island to form the mipmap slices.
  SELECT
    $_partition_column,
    -- Select the minimum source_id from the slices that form the dominant group in this island.
    min(source_id) AS id,
    cast_int!(min(ts)) AS ts,
    (
      max(bucket_index) - min(bucket_index) + 1
    ) * $_bucket_duration_ns AS dur,
    group_hash
  FROM mm_with_islands
  GROUP BY
    $_partition_column,
    island_id,
    group_hash
  ORDER BY
    ts
);
