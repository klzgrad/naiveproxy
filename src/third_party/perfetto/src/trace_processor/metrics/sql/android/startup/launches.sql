--
-- Copyright 2019 The Android Open Source Project
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

-- This metric will be deprecated soon. All of the tables have been
-- migrated to SQL standard library and can be imported from:
INCLUDE PERFETTO MODULE android.startup.startups;


DROP VIEW IF EXISTS launches;
CREATE PERFETTO VIEW launches AS
SELECT startup_id AS launch_id, *, startup_type as launch_type FROM android_startups;

DROP VIEW IF EXISTS launch_processes;
CREATE PERFETTO VIEW launch_processes AS
SELECT startup_id AS launch_id, * FROM android_startup_processes;

DROP VIEW IF EXISTS launch_threads;
CREATE PERFETTO VIEW launch_threads AS
SELECT startup_id AS launch_id, * FROM android_startup_threads;

DROP VIEW IF EXISTS launching_events;
CREATE PERFETTO VIEW launching_events AS
SELECT * FROM _startup_events;
