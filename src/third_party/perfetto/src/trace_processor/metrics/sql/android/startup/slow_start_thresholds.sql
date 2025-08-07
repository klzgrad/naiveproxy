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
--

INCLUDE PERFETTO MODULE android.startup.startups;

DROP VIEW IF EXISTS slow_start_thresholds;
CREATE PERFETTO VIEW slow_start_thresholds AS
SELECT
  15 AS runnable_percentage,
  2900000000 AS interruptible_sleep_ns,
  450000000 AS blocking_io_ns,
  20 AS open_dex_files_from_oat_percentage,
  1250000000 AS bind_application_ns,
  450000000 AS view_inflation_ns,
  130000000 AS resources_manager_get_resources_ns,
  15 AS verify_classes_percentage,
  100000000 AS potential_cpu_contention_ns,
  100000000 AS jit_activity_ns,
  20 AS lock_contention_percentage,
  15 AS monitor_contention_percentage,
  65 AS jit_compiled_methods_count,
  15 AS broadcast_dispatched_count,
  50 AS broadcast_received_count;

CREATE OR REPLACE PERFETTO FUNCTION threshold_runnable_percentage()
RETURNS INT AS
  SELECT runnable_percentage 
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_interruptible_sleep_ns()
RETURNS INT AS
  SELECT interruptible_sleep_ns
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_blocking_io_ns()
RETURNS INT AS
  SELECT blocking_io_ns
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_open_dex_files_from_oat_percentage()
RETURNS INT AS
  SELECT open_dex_files_from_oat_percentage
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_bind_application_ns()
RETURNS INT AS
  SELECT bind_application_ns
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_view_inflation_ns()
RETURNS INT AS
  SELECT view_inflation_ns
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_resources_manager_get_resources_ns()
RETURNS INT AS
  SELECT resources_manager_get_resources_ns
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_verify_classes_percentage()
RETURNS INT AS
  SELECT verify_classes_percentage
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_potential_cpu_contention_ns()
RETURNS INT AS
  SELECT potential_cpu_contention_ns
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_jit_activity_ns()
RETURNS INT AS
  SELECT jit_activity_ns
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_lock_contention_percentage()
RETURNS INT AS
  SELECT lock_contention_percentage
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_monitor_contention_percentage()
RETURNS INT AS
  SELECT monitor_contention_percentage
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_jit_compiled_methods_count()
RETURNS INT AS
  SELECT jit_compiled_methods_count
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_broadcast_dispatched_count()
RETURNS INT AS
  SELECT broadcast_dispatched_count
  FROM slow_start_thresholds;

CREATE OR REPLACE PERFETTO FUNCTION threshold_broadcast_received_count()
RETURNS INT AS
  SELECT broadcast_received_count
  FROM slow_start_thresholds;
