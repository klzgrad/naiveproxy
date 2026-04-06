--
-- Copyright 2026 The Android Open Source Project
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

-- sqlformat file off

-- Replaces all occurrences of a regular expression with a constant 
-- replacement string.
-- Note that there is no way to substitute matching groups into the 
-- replacement, and all matching is case-sensitive.
CREATE PERFETTO FUNCTION regexp_replace_simple(
    -- The input string to match against.
    input STRING,
    -- The matching regexp.
    regex STRING,
    -- The replacement string to substitute in.
    replacement STRING
)
-- The result.
RETURNS STRING DELEGATES TO __intrinsic_regexp_replace_simple;

-- Reverses a string.
CREATE PERFETTO FUNCTION reverse(
    -- The string to reverse.
    input STRING
)
-- The reversed string.
RETURNS STRING DELEGATES TO __intrinsic_reverse;

-- Encodes bytes into a base64 string.
CREATE PERFETTO FUNCTION base64_encode(
    -- The bytes to encode.
    input BYTES
)
-- The base64-encoded string.
RETURNS STRING DELEGATES TO __intrinsic_base64_encode;

-- Decodes a base64-encoded string into bytes.
CREATE PERFETTO FUNCTION base64_decode(
    -- The base64-encoded string to decode.
    input STRING
)
-- The decoded bytes.
RETURNS BYTES DELEGATES TO __intrinsic_base64_decode;

-- Demangles a C++ mangled symbol name.
CREATE PERFETTO FUNCTION demangle(
    -- The mangled symbol name.
    input STRING
)
-- The demangled symbol name, or NULL if demangling fails.
RETURNS STRING DELEGATES TO __intrinsic_demangle;

-- Extracts the first match of a regular expression from a string.
CREATE PERFETTO FUNCTION regexp_extract(
    -- The input string to match against.
    input STRING,
    -- The regular expression pattern.
    regex STRING
)
-- The matched substring, or NULL if no match.
RETURNS STRING DELEGATES TO __intrinsic_regexp_extract;

-- Converts a hex string (with optional 0x prefix) to an integer.
CREATE PERFETTO FUNCTION UNHEX(
    -- The hex string to convert.
    input STRING
)
-- The integer value.
RETURNS INT DELEGATES TO __intrinsic_unhex;

-- Converts a trace timestamp to an absolute ISO 8601 time string.
CREATE PERFETTO FUNCTION abs_time_str(
    -- The trace timestamp (in nanoseconds).
    ts INT
)
-- The ISO 8601 formatted time string, or NULL if conversion fails.
RETURNS STRING DELEGATES TO __intrinsic_abs_time_str;

-- Converts a trace timestamp to monotonic clock time.
CREATE PERFETTO FUNCTION to_monotonic(
    -- The trace timestamp (in nanoseconds).
    ts INT
)
-- The monotonic clock timestamp, or NULL if conversion fails.
RETURNS INT DELEGATES TO __intrinsic_to_monotonic;

-- Converts a trace timestamp to realtime clock time.
CREATE PERFETTO FUNCTION to_realtime(
    -- The trace timestamp (in nanoseconds).
    ts INT
)
-- The realtime clock timestamp, or NULL if conversion fails.
RETURNS INT DELEGATES TO __intrinsic_to_realtime;

-- Converts a trace timestamp to a human-readable timecode
-- (HH:MM:SS mmm uuu nnn).
CREATE PERFETTO FUNCTION to_timecode(
    -- The trace timestamp (in nanoseconds).
    ts INT
)
-- The formatted timecode string.
RETURNS STRING DELEGATES TO __intrinsic_to_timecode;

-- Computes the natural logarithm of a number.
CREATE PERFETTO FUNCTION ln(
    -- The input value.
    input DOUBLE
)
-- The natural logarithm, or NULL if input is non-positive or NULL.
RETURNS DOUBLE DELEGATES TO __intrinsic_ln;

-- Computes e raised to the power of a number.
CREATE PERFETTO FUNCTION exp(
    -- The exponent value.
    input DOUBLE
)
-- The result of e^input, or NULL if input is NULL.
RETURNS DOUBLE DELEGATES TO __intrinsic_exp;

-- Computes the square root of a number.
CREATE PERFETTO FUNCTION sqrt(
    -- The input value.
    input DOUBLE
)
-- The square root, or NULL if input is NULL.
RETURNS DOUBLE DELEGATES TO __intrinsic_sqrt;

-- sqlformat file on
