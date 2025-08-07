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

SELECT RUN_METRIC('android/power_profile_data.sql');

DROP TABLE IF EXISTS cluster_core_type;
CREATE PERFETTO TABLE cluster_core_type AS
SELECT 0 AS cluster, 'little' AS core_type
UNION ALL
SELECT 1, 'big'
UNION ALL
SELECT 2, 'bigger';

DROP VIEW IF EXISTS device_power_profile;
CREATE PERFETTO VIEW device_power_profile AS
SELECT cpu, cluster, freq, power
FROM power_profile pp
WHERE EXISTS (
  SELECT 1 FROM metadata
  WHERE name = 'android_build_fingerprint' AND str_value GLOB '*' || pp.device || '*');

DROP VIEW IF EXISTS core_cluster_per_cpu;
CREATE PERFETTO VIEW core_cluster_per_cpu AS
SELECT DISTINCT cpu, cluster
FROM device_power_profile;

DROP VIEW IF EXISTS core_type_per_cpu;
CREATE PERFETTO VIEW core_type_per_cpu AS
SELECT
  cpu,
  core_type
FROM core_cluster_per_cpu JOIN cluster_core_type USING(cluster);

DROP VIEW IF EXISTS cpu_cluster_power;
CREATE PERFETTO VIEW cpu_cluster_power AS
SELECT DISTINCT core_type, freq, power
FROM device_power_profile pp JOIN cluster_core_type USING(cluster);
