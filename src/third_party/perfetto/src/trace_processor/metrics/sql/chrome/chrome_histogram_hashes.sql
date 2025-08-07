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

DROP VIEW IF EXISTS chrome_histogram_hashes_output;

CREATE PERFETTO VIEW chrome_histogram_hashes_output AS
SELECT ChromeHistogramHashes(
  'hash', (
    SELECT RepeatedField(int_value)
    FROM args
    WHERE key = 'chrome_histogram_sample.name_hash'
    ORDER BY int_value
  )
);
