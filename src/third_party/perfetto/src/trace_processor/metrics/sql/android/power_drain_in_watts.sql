--
-- Copyright 2020 The Android Open Source Project
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
--

-- This is a mapping from counter names on different devices
-- to what subsystems they are measuring.
DROP TABLE IF EXISTS power_counters;
CREATE TABLE power_counters (name TEXT UNIQUE, subsystem TEXT);

INSERT INTO power_counters
VALUES ('power.VPH_PWR_S5C_S6C_uws', 'cpu_big'),
('power.VPH_PWR_S4C_uws', 'cpu_little'),
('power.VPH_PWR_S2C_S3C_uws', 'soc'),
('power.VPH_PWR_OLED_uws', 'display'),
('power.PPVAR_VPH_PWR_S1A_S9A_S10A_uws', 'soc'),
('power.PPVAR_VPH_PWR_S2A_S3A_uws', 'cpu_big'),
('power.PPVAR_VPH_PWR_S1C_uws', 'cpu_little'),
('power.WCN3998_VDD13 [from PP1304_L2C]_uws', 'wifi'),
('power.PPVAR_VPH_PWR_WLAN_uws', 'wifi'),
('power.PPVAR_VPH_PWR_OLED_uws', 'display'),
('power.PPVAR_VPH_PWR_QTM525_uws', 'cellular'),
('power.PPVAR_VPH_PWR_RF_uws', 'cellular'),
('power.rails.aoc.logic', 'aoc'),
('power.rails.aoc.memory', 'aoc'),
('power.rails.cpu.big', 'cpu_big'),
('power.rails.cpu.little', 'cpu_little'),
('power.rails.cpu.mid', 'cpu_mid'),
('power.rails.ddr.a', 'mem'),
('power.rails.ddr.b', 'mem'),
('power.rails.ddr.c', 'mem'),
('power.rails.gpu', 'gpu'),
('power.rails.display', 'display'),
('power.rails.gps', 'gps'),
('power.rails.memory.interface', 'mem'),
('power.rails.modem', 'cellular'),
('power.rails.radio.frontend', 'cellular'),
('power.rails.system.fabric', 'soc'),
('power.rails.wifi.bt', 'wifi');

-- Convert power counter data into table of events, where each event has
-- start timestamp, duration and the average power drain during its duration
-- in Watts.
-- Note that power counters wrap around at different values on different
-- devices. When that happens, we ignore the value before overflow, and only
-- take into account the value after it. This underestimates the actual power
-- drain between those counters.
DROP VIEW IF EXISTS drain_in_watts;
CREATE PERFETTO VIEW drain_in_watts AS
SELECT name,
  ts,
  LEAD(ts) OVER (
    PARTITION BY track_id
    ORDER BY ts
  ) - ts AS dur,
  CASE
    WHEN LEAD(value) OVER (
      PARTITION BY track_id
      ORDER BY ts
    ) >= value THEN (
      LEAD(value) OVER (
        PARTITION BY track_id
        ORDER BY ts
      ) - value
    )
    ELSE LEAD(value) OVER (
      PARTITION BY track_id
      ORDER BY ts
    )
  END / (
    LEAD(ts) OVER (
      PARTITION BY track_id
      ORDER BY ts
    ) - ts
  ) * 1e3 AS drain_w
FROM counter
JOIN counter_track ON (counter.track_id = counter_track.id)
WHERE name GLOB "power.*";
