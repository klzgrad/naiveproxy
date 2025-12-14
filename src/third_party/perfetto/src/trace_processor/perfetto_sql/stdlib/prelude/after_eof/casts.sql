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

-- sqlformat file off
-- TODO(lalitm): this is necessary because of AS TEXT cast which gets converted
-- to AS STRING which we don't want. Relax this when we improve the formatting
-- script.

-- Casts |value| to INT.
CREATE PERFETTO MACRO cast_int(
    -- Query or subquery that will be cast.
    value Expr
) RETURNS Expr AS
CAST($value AS INT);

-- Casts |value| to DOUBLE.
CREATE PERFETTO MACRO cast_double(
    -- Query or subquery that will be cast.
    value Expr
) RETURNS Expr AS
CAST($value AS REAL);

-- Casts |value| to STRING.
CREATE PERFETTO MACRO cast_string(
    -- Query or subquery that will be cast.
    value Expr
) RETURNS Expr AS
CAST($value AS TEXT);