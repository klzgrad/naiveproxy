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
--

-- Count packages by package UID.
CREATE PERFETTO TABLE _uid_package_count AS
SELECT
  uid,
  count(1) AS cnt
FROM package_list
GROUP BY
  1;

CREATE PERFETTO FUNCTION _android_package_for_process(
    uid LONG,
    uid_count LONG,
    process_name STRING
)
RETURNS TABLE (
  package_name STRING,
  version_code LONG,
  debuggable BOOL
) AS
WITH
  min_distance AS (
    SELECT
      -- SQLite allows omitting the group-by for the MIN: the other columns
      -- will match the row with the minimum value.
      min(length($process_name) - length(package_name)),
      package_name,
      version_code,
      debuggable
    FROM package_list
    WHERE
      (
        (
          $uid = uid
          AND (
            -- Either a unique match or process name is a prefix the package name
            $uid_count = 1
            OR $process_name GLOB package_name || '*'
          )
        )
        OR (
          -- isolated processes can only be matched based on the name
          $uid >= 90000
          AND $uid < 100000
          AND str_split($process_name, ':', 0) GLOB package_name || '*'
        )
      )
  )
SELECT
  package_name,
  version_code,
  debuggable
FROM min_distance;

-- Table containing the upids of all kernel tasks.
CREATE PERFETTO TABLE _android_kernel_tasks (
  -- upid of the kernel task
  upid LONG
) AS
SELECT
  a.upid
FROM process AS a
LEFT JOIN process AS b
  ON a.parent_upid = b.upid
WHERE
  b.name = 'kthreadd' OR a.pid = 0 OR b.pid = 0
ORDER BY
  1;

-- Returns true if the process is a kernel task.
CREATE PERFETTO FUNCTION android_is_kernel_task(
    -- Queried process
    upid LONG
)
-- True for kernel tasks
RETURNS BOOL AS
SELECT
  EXISTS(
    SELECT
      TRUE
    FROM _android_kernel_tasks
    WHERE
      upid = $upid
  );

-- Data about packages running on the process.
CREATE PERFETTO TABLE android_process_metadata (
  -- Process upid.
  upid JOINID(process.id),
  -- Process pid.
  pid LONG,
  -- Process name.
  process_name STRING,
  -- Android app UID.
  uid LONG,
  -- Whether the UID is shared by multiple packages.
  shared_uid BOOL,
  -- Android user id for multi-user devices
  user_id LONG,
  -- Name of the packages running in this process.
  package_name STRING,
  -- Package version code.
  version_code LONG,
  -- Whether package is debuggable.
  debuggable LONG,
  -- Whether the task is kernel or not
  is_kernel_task BOOL
) AS
SELECT
  process.upid,
  process.pid,
  -- workaround for b/169226092: the bug has been fixed it Android T, but
  -- we support ingesting traces from older Android versions.
  CASE
    -- cmdline gets rewritten after fork, if these are still there we must
    -- have seen a racy capture.
    WHEN length(process.name) = 15
    AND (
      process.cmdline IN ('zygote', 'zygote64', '<pre-initialized>')
      OR process.cmdline GLOB '*' || process.name
    )
    THEN process.cmdline
    ELSE process.name
  END AS process_name,
  process.android_appid AS uid,
  process.android_user_id AS user_id,
  CASE WHEN _uid_package_count.cnt > 1 THEN TRUE ELSE NULL END AS shared_uid,
  plist.package_name,
  plist.version_code,
  plist.debuggable,
  android_is_kernel_task(process.upid) AS is_kernel_task
FROM process
LEFT JOIN _uid_package_count
  ON process.android_appid = _uid_package_count.uid
LEFT JOIN _android_package_for_process(process.android_appid, _uid_package_count.cnt, process.name) AS plist
ORDER BY
  upid;
