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

-- For a given counter timeline (e.g. a single counter track), returns
-- intervals of time where the counter has the same value. For every run
-- of identical values, this macro will return a row for the first one,
-- the last one, and a row merging all other ones. This to to facilitate
-- construction of counters from delta_values.
--
-- Intervals are computed in a "forward-looking" way. That is, if a counter
-- changes value at some timestamp, it's assumed it *just* reached that
-- value and it should continue to have that value until the next
-- value change. The final value is assumed to hold until the very end of
-- the trace.
--
-- For example, suppose we have the following data:
-- ```
-- ts=0, value=10, track_id=1
-- ts=0, value=10, track_id=2
-- ts=10, value=10, track_id=1
-- ts=10, value=20, track_id=2
-- ts=20, value=30, track_id=1
-- [end of trace at ts = 40]
-- ```
--
-- Then this macro will generate the following intervals:
-- ```
-- ts=0, dur=10, value=10, track_id=1
-- ts=10, dur=10, value=10, track_id=1
-- ts=20, dur=10, value=30, track_id=1
-- ts=0, dur=10, value=10, track_id=2
-- ts=10, dur=30, value=20, track_id=2
-- ```
CREATE PERFETTO MACRO counter_leading_intervals(
    -- A table/view/subquery corresponding to a "counter-like" table.
    -- This table must have the columns "id" and "ts" and "track_id" and "value" corresponding
    -- to an id, timestamp, counter track_id and associated counter value.
    counter_table TableOrSubquery
)
-- Table with the schema:
-- id LONG,
--     As passed in
-- ts TIMESTAMP,
--     As passed in
-- dur DURATION,
--     Difference to the timestamp for the leading row.
-- track_id JOINID(track.id),
--     As passed in
-- value DOUBLE,
--     As passed in
-- next_value DOUBLE,
--     Value for the leading row.
-- delta_value DOUBLE
--     Delta to the *lagging* row - note that this is not the same thing as (next_value - value).
RETURNS TableOrSubquery AS
(
  SELECT
    c0 AS id,
    c1 AS ts,
    c2 AS dur,
    c3 AS track_id,
    c4 AS value,
    c5 AS next_value,
    c6 AS delta_value
  FROM __intrinsic_table_ptr(
    __intrinsic_counter_intervals(
      "leading",
      trace_end(),
      (
        SELECT
          __intrinsic_counter_per_track_agg(input.id, input.ts, input.track_id, input.value)
        FROM (
          SELECT
            *
          FROM $counter_table
          ORDER BY
            ts
        ) AS input
      )
    )
  )
  WHERE
    __intrinsic_table_ptr_bind(c0, 'id')
    AND __intrinsic_table_ptr_bind(c1, 'ts')
    AND __intrinsic_table_ptr_bind(c2, 'dur')
    AND __intrinsic_table_ptr_bind(c3, 'track_id')
    AND __intrinsic_table_ptr_bind(c4, 'value')
    AND __intrinsic_table_ptr_bind(c5, 'next_value')
    AND __intrinsic_table_ptr_bind(c6, 'delta_value')
);
