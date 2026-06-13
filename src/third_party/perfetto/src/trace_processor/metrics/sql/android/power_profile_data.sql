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

DROP TABLE IF EXISTS power_profile;
CREATE TABLE power_profile(
  device STRING,
  cpu INT,
  cluster INT,
  freq INT,
  power DOUBLE,
  UNIQUE(device, cpu, cluster, freq)
);

SELECT RUN_METRIC('android/power_profile_data/marlin.sql');
SELECT RUN_METRIC('android/power_profile_data/walleye.sql');
SELECT RUN_METRIC('android/power_profile_data/taimen.sql');
SELECT RUN_METRIC('android/power_profile_data/blueline.sql');
SELECT RUN_METRIC('android/power_profile_data/crosshatch.sql');
SELECT RUN_METRIC('android/power_profile_data/bonito.sql');
SELECT RUN_METRIC('android/power_profile_data/sargo.sql');
SELECT RUN_METRIC('android/power_profile_data/flame.sql');
SELECT RUN_METRIC('android/power_profile_data/coral.sql');
SELECT RUN_METRIC('android/power_profile_data/sunfish.sql');
SELECT RUN_METRIC('android/power_profile_data/bramble.sql');
SELECT RUN_METRIC('android/power_profile_data/redfin.sql');
SELECT RUN_METRIC('android/power_profile_data/barbet.sql');
SELECT RUN_METRIC('android/power_profile_data/oriole.sql');
SELECT RUN_METRIC('android/power_profile_data/raven.sql');
SELECT RUN_METRIC('android/power_profile_data/bluejay.sql');
SELECT RUN_METRIC('android/power_profile_data/shusky.sql');

