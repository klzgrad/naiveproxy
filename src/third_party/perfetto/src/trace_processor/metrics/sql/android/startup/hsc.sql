--
-- Copyright 2020 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.startup.startups;

-- Must be invoked after populating launches table in android_startup.
DROP VIEW IF EXISTS functions;
CREATE PERFETTO VIEW functions AS
SELECT
  slices.ts AS ts,
  slices.dur AS dur,
  process.name AS process_name,
  thread.name AS thread_name,
  slices.name AS function_name
FROM slices
JOIN thread_track ON slices.track_id = thread_track.id
JOIN thread USING(utid)
JOIN process USING(upid);

-- Animators don't occur on threads, so add them here.
DROP VIEW IF EXISTS animators;
CREATE PERFETTO VIEW animators AS
SELECT
  slices.ts AS ts,
  slices.dur AS dur,
  thread.name AS process_name,
  slices.name AS animator_name
FROM slices
JOIN process_track ON slices.track_id = process_track.id
JOIN thread USING(upid)
WHERE slices.name GLOB "animator*";

DROP VIEW IF EXISTS android_frame_times;
CREATE PERFETTO VIEW android_frame_times AS
SELECT
  functions.ts AS ts,
  functions.ts + functions.dur AS ts_end,
  launches.package AS name,
  launches.startup_id,
  ROW_NUMBER() OVER(PARTITION BY launches.startup_id ORDER BY functions.ts ASC) AS number
FROM functions
JOIN android_startups launches ON launches.package GLOB '*' || functions.process_name || '*'
WHERE functions.function_name GLOB "Choreographer#doFrame*" AND functions.ts > launches.ts;

DROP VIEW IF EXISTS android_render_frame_times;
CREATE PERFETTO VIEW android_render_frame_times AS
SELECT
  functions.ts AS ts,
  functions.ts + functions.dur AS ts_end,
  launches.package AS name,
  launches.startup_id,
  ROW_NUMBER() OVER(PARTITION BY launches.startup_id ORDER BY functions.ts ASC) AS number
FROM functions
JOIN android_startups launches ON launches.package GLOB '*' || functions.process_name || '*'
WHERE functions.function_name GLOB "DrawFrame*" AND functions.ts > launches.ts;

DROP VIEW IF EXISTS frame_times;
CREATE PERFETTO VIEW frame_times AS
SELECT startup_id AS launch_id, * FROM android_frame_times;

DROP TABLE IF EXISTS hsc_based_startup_times;
CREATE TABLE hsc_based_startup_times(package STRING, id INT, ts_total INT);

-- Calculator
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.number = 2 AND android_frame_times.name GLOB "*roid.calcul*" AND android_frame_times.startup_id = launches.startup_id;

-- Calendar
-- Using the DrawFrame slice from the render thread due to Calendar delaying its rendering
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_render_frame_times.ts_end - launches.ts AS ts_total
FROM android_render_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_render_frame_times.name || '*'
WHERE android_render_frame_times.number = 5 AND android_render_frame_times.name GLOB "*id.calendar*" AND android_render_frame_times.startup_id = launches.startup_id;

-- Camera
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.number = 2 AND android_frame_times.name GLOB "*GoogleCamera*" AND android_frame_times.startup_id = launches.startup_id;

-- Chrome
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.number = 4 AND android_frame_times.name GLOB "*chrome*" AND android_frame_times.startup_id = launches.startup_id;

-- Clock
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts > (SELECT ts + dur FROM animators WHERE animator_name = "animator:translationZ" AND process_name GLOB "*id.deskclock" ORDER BY (ts + dur) DESC LIMIT 1) AND android_frame_times.name GLOB "*id.deskclock" AND android_frame_times.startup_id = launches.startup_id
ORDER BY ts_total LIMIT 1;

-- Contacts
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.number = 3 AND android_frame_times.name GLOB "*id.contacts" AND android_frame_times.startup_id = launches.startup_id;

-- Dialer
-- Dialer only runs one animation at startup, use the last animation frame to indicate startup.
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts > (SELECT ts + dur FROM animators WHERE process_name GLOB "*id.dialer" AND animator_name GLOB "*animator*" ORDER BY (ts + dur) DESC LIMIT 1) AND android_frame_times.name GLOB "*id.dialer" AND android_frame_times.startup_id = launches.startup_id LIMIT 1;

-- Facebook
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts > (SELECT ts + dur FROM slices WHERE slices.name GLOB "fb_startup_complete" ORDER BY ts LIMIT 1) AND android_frame_times.name GLOB "*ok.katana" AND android_frame_times.startup_id = launches.startup_id
ORDER BY ts_total LIMIT 1;

-- Facebook Messenger
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts > (SELECT ts + dur FROM slices WHERE slices.name GLOB "msgr_cold_start_to_cached_content" ORDER BY ts LIMIT 1) AND android_frame_times.name GLOB "*book.orca" AND android_frame_times.startup_id = launches.startup_id
ORDER BY ts_total LIMIT 1;

-- Gmail
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts > (SELECT ts + dur FROM animators WHERE animator_name = "animator:elevation" AND process_name GLOB "*android.gm" ORDER BY (ts + dur) DESC LIMIT 1) AND android_frame_times.name GLOB "*android.gm" AND android_frame_times.startup_id = launches.startup_id
ORDER BY ts_total LIMIT 1;

-- Instagram
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts > (SELECT ts + dur FROM slices WHERE slices.name GLOB "ig_cold_start_to_cached_content" ORDER BY ts LIMIT 1) AND android_frame_times.name GLOB "*gram.android" AND android_frame_times.startup_id = launches.startup_id
ORDER BY ts_total LIMIT 1;

-- Maps
-- Use the 8th choreographer frame to indicate startup.
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.number = 8 AND android_frame_times.name GLOB "*maps*" AND android_frame_times.startup_id = launches.startup_id;

-- Messages
-- Use the first choreographer frame that is emitted after all animator:translationZ slices end.
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts_end > (SELECT ts + dur FROM animators WHERE animator_name = "animator:translationZ" AND process_name GLOB "*apps.messaging*" ORDER BY (ts + dur) DESC LIMIT 1) AND android_frame_times.name GLOB "*apps.messaging*" AND android_frame_times.startup_id = launches.startup_id
ORDER BY ts_total LIMIT 1;

-- Netflix
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts < (SELECT ts FROM animators WHERE animator_name GLOB "animator*" AND process_name GLOB "*lix.mediaclient" ORDER BY ts LIMIT 1) AND android_frame_times.name GLOB "*lix.mediaclient*" AND android_frame_times.startup_id = launches.startup_id
ORDER BY ts_total DESC LIMIT 1;

-- Photos
-- Use the animator:translationZ slice as startup indicator.
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts > (SELECT ts + dur FROM animators WHERE process_name GLOB "*apps.photos" AND animator_name GLOB "animator:translationZ" ORDER BY (ts + dur) DESC LIMIT 1) AND android_frame_times.name GLOB "*apps.photos*" AND android_frame_times.startup_id = launches.startup_id LIMIT 1;

-- Settings was deprecated in favor of reportFullyDrawn b/169694037.

-- Snapchat
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.number = 1 AND android_frame_times.name GLOB "*napchat.android" AND android_frame_times.startup_id = launches.startup_id;

-- Twitter
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts_end > (SELECT ts FROM animators WHERE animator_name = "animator" AND process_name GLOB "*tter.android" ORDER BY ts LIMIT 1) AND android_frame_times.name GLOB "*tter.android" AND android_frame_times.startup_id = launches.startup_id
ORDER BY ts_total LIMIT 1;

-- WhatsApp
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_frame_times.ts_end - launches.ts AS ts_total
FROM android_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_frame_times.name || '*'
WHERE android_frame_times.ts > (SELECT ts + dur FROM slices WHERE slices.name GLOB "wa_startup_complete" ORDER BY ts LIMIT 1) AND android_frame_times.name GLOB "*om.whatsapp" AND android_frame_times.startup_id = launches.startup_id
ORDER BY ts_total LIMIT 1;

-- Youtube
-- Use the 10th frame that is rendered
INSERT INTO hsc_based_startup_times
SELECT
  launches.package AS package,
  launches.startup_id AS id,
  android_render_frame_times.ts_end - launches.ts AS ts_total
FROM android_render_frame_times
JOIN android_startups launches ON launches.package GLOB '*' || android_render_frame_times.name || '*'
WHERE android_render_frame_times.number = 10 AND android_render_frame_times.name GLOB "*id.youtube" AND android_render_frame_times.startup_id = launches.startup_id;
