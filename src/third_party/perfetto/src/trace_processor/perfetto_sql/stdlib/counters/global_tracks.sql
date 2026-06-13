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

CREATE PERFETTO FUNCTION _counter_track_is_only_name_dimension(
    track_id LONG
)
RETURNS BOOL AS
SELECT
  NOT EXISTS(
    SELECT
      1
    FROM counter_track
    JOIN args
      ON counter_track.dimension_arg_set_id = args.arg_set_id
    WHERE
      counter_track.id = $track_id AND args.key != 'name'
    LIMIT 1
  );
