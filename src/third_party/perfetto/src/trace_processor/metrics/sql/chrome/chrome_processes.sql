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

-- Table to map any of the various chrome process names to a type (e.g. Browser,
-- Renderer, GPU Process, etc).
DROP VIEW IF EXISTS all_chrome_processes;
CREATE PERFETTO VIEW all_chrome_processes AS
SELECT upid, IFNULL(pt.string_value, '') AS process_type
FROM process
-- A process is a Chrome process if it has a chrome.process_type arg.
-- The value of the arg may be NULL.
-- All Chromium producers emit chrome_process field in their process track
-- descriptor when Chromium track event data source is enabled.
-- So this returns all processes in Chrome traces, and a subset of processes
-- in system traces.
JOIN
  (SELECT arg_set_id, string_value FROM args WHERE key = 'chrome.process_type')
  pt
  ON process.arg_set_id = pt.arg_set_id;

-- A view of all Chrome threads.
DROP VIEW IF EXISTS all_chrome_threads;
CREATE PERFETTO VIEW all_chrome_threads AS
SELECT utid, thread.upid, thread.name
FROM thread, all_chrome_processes
WHERE thread.upid = all_chrome_processes.upid;

-- For sandboxed and privileged processes (found in Android system traces), use
-- the main thread name to type of process.
DROP VIEW IF EXISTS chrome_subprocess_types;
CREATE PERFETTO VIEW chrome_subprocess_types AS
-- Sometimes you can get multiple threads in a trace marked main_thread, but
-- they appear to have the same name so just use one of them.
SELECT DISTINCT p.upid,
  SUBSTR(t.name, 3, LENGTH(t.name) - 6) AS sandbox_type
FROM all_chrome_processes p
JOIN all_chrome_threads t ON p.upid = t.upid
WHERE process_type IN ("Sandboxed", "Privileged")
  AND t.name GLOB "Cr*Main";

-- Contains all the chrome processes from process with an extra column,
-- process_type.
DROP VIEW IF EXISTS chrome_process;
CREATE PERFETTO VIEW chrome_process AS
SELECT PROCESS.*,
  IIF(sandbox_type IS NULL, process_type, sandbox_type) AS process_type
FROM PROCESS
JOIN (
    SELECT a.upid,
      sandbox_type,
      process_type
    FROM all_chrome_processes a
    LEFT JOIN chrome_subprocess_types s ON a.upid = s.upid
  ) c ON PROCESS.upid = c.upid;

-- Contains all the chrome threads from thread with an extra column,
-- canonical_name, that should contain a thread that's the same in both chrome
-- and system traces.
DROP VIEW IF EXISTS chrome_thread;

CREATE PERFETTO VIEW chrome_thread AS
SELECT thread.*,
  CASE
    WHEN thread.name GLOB "Cr*Main" THEN "CrProcessMain"
    WHEN thread.name IS NULL THEN "Unknown"
    ELSE thread.name
  END AS canonical_name
FROM (
    SELECT t.utid,
      p.*
    FROM all_chrome_threads t
    JOIN chrome_process p ON t.upid = p.upid
  ) c
JOIN thread ON thread.utid = c.utid;
