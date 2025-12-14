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

INCLUDE PERFETTO MODULE prelude.after_eof.indexes;

INCLUDE PERFETTO MODULE prelude.after_eof.views;

-- Include all focused modules
INCLUDE PERFETTO MODULE prelude.after_eof.core;

INCLUDE PERFETTO MODULE prelude.after_eof.tracks;

INCLUDE PERFETTO MODULE prelude.after_eof.cpu_scheduling;

INCLUDE PERFETTO MODULE prelude.after_eof.counters;

INCLUDE PERFETTO MODULE prelude.after_eof.events;

INCLUDE PERFETTO MODULE prelude.after_eof.memory;
