--
-- Copyright 2026 The Android Open Source Project
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

-- Journald log entries from the linux.systemd_journald data source.
--
-- NOTE: this table is not sorted by timestamp.
CREATE PERFETTO VIEW linux_systemd_journald_logs(
  -- Which row in the table the log corresponds to.
  id ID,
  -- Timestamp in nanoseconds.
  ts TIMESTAMP,
  -- Thread id in the trace (nullable).
  utid JOINID(thread.id),
  -- Syslog priority (0=EMERG, 7=DEBUG).
  prio LONG,
  -- SYSLOG_IDENTIFIER (program name / tag, nullable).
  tag STRING,
  -- Log message text.
  msg STRING,
  -- User ID (nullable).
  uid LONG,
  -- Process comm name (nullable).
  comm STRING,
  -- Systemd unit name (nullable).
  systemd_unit STRING,
  -- Hostname (nullable).
  hostname STRING,
  -- Transport method (nullable).
  transport STRING
)
AS
SELECT
  l.id,
  l.ts,
  l.utid,
  l.prio,
  l.tag,
  l.msg,
  CAST(extract_arg(l.arg_set_id, 'uid') AS INTEGER) AS uid,
  extract_arg(l.arg_set_id, 'comm') AS comm,
  extract_arg(l.arg_set_id, 'systemd_unit') AS systemd_unit,
  extract_arg(l.arg_set_id, 'hostname') AS hostname,
  extract_arg(l.arg_set_id, 'transport') AS transport
FROM logs AS l
WHERE
  l.log_source = 'systemd_journald';
