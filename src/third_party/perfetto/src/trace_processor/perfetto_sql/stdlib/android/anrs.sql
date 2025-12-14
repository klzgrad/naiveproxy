--
-- Copyright 2023 The Android Open Source Project
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

CREATE PERFETTO FUNCTION _extract_anr_type(
    subject STRING
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $subject IS NULL
    THEN NULL
    WHEN $subject GLOB 'Broadcast of*'
    THEN 'BROADCAST_OF_INTENT'
    WHEN $subject GLOB 'Input dispatching timed out*does not have a focused window*'
    THEN 'INPUT_DISPATCHING_TIMEOUT_NO_FOCUSED_WINDOW'
    WHEN $subject GLOB 'Input dispatching timed out*Waiting because no window has focus but there is a focused application*'
    THEN 'INPUT_DISPATCHING_TIMEOUT_NO_FOCUSED_WINDOW'
    WHEN $subject GLOB 'Input dispatching timed out*'
    THEN 'INPUT_DISPATCHING_TIMEOUT'
    WHEN $subject GLOB 'Context.startForegroundService() did not then call Service.startForeground()*'
    THEN 'START_FOREGROUND_SERVICE'
    WHEN $subject GLOB 'executing service*'
    THEN 'EXECUTING_SERVICE'
    WHEN $subject GLOB 'ContentProvider not responding*'
    THEN 'CONTENT_PROVIDER_NOT_RESPONDING'
    WHEN $subject GLOB 'App requested: Buffer processing hung up due to stuck fence. Indicates GPU hang'
    THEN 'GPU_HANG'
    WHEN $subject GLOB 'No response to onStartJob*'
    THEN 'JOB_SERVICE_START'
    WHEN $subject GLOB 'No response to onStopJob*'
    THEN 'JOB_SERVICE_STOP'
    WHEN $subject GLOB 'Timed out while trying to bind*'
    THEN 'JOB_SERVICE_BIND'
    WHEN $subject GLOB 'Process ProcessRecord{*} failed to complete startup'
    THEN 'BIND_APPLICATION'
    WHEN $subject GLOB 'A foreground service of FOREGROUND_SERVICE_TYPE_SHORT_SERVICE did not stop within a timeout*'
    THEN 'FOREGROUND_SHORT_SERVICE_TIMEOUT'
    WHEN $subject GLOB 'A foreground service of type*'
    THEN 'FOREGROUND_SERVICE_TIMEOUT'
    WHEN $subject GLOB 'App requested: *'
    THEN 'APP_TRIGGERED'
    WHEN $subject GLOB 'required notification not provided*'
    THEN 'JOB_SERVICE_NOTIFICATION_NOT_PROVIDED'
    ELSE 'UNKNOWN_ANR_TYPE'
  END;

CREATE PERFETTO FUNCTION _get_broadcast_flag(
    subject STRING
)
RETURNS LONG AS
SELECT
  CASE
    WHEN $subject IS NULL OR NOT $subject GLOB 'Broadcast of Intent *flg=*'
    THEN NULL
    ELSE unhex(str_split(substr($subject, instr($subject, ' flg=') + 5), ' ', 0))
  END AS flag;

-- Function to get the default ANR duration in milliseconds based on ANR type.
-- Note: These are common defaults. Actual timeouts can vary by OEM, Android version, and FG/BG status.
CREATE PERFETTO FUNCTION _default_anr_dur(
    anr_type STRING,
    subject STRING
)
RETURNS LONG AS
SELECT
  CASE
    WHEN $anr_type IS NULL
    THEN NULL
    WHEN $anr_type = 'BROADCAST_OF_INTENT'
    AND (
      _get_broadcast_flag($subject) & unhex('0x10000000')
    ) = 0
    THEN 60000
    WHEN $anr_type = 'BROADCAST_OF_INTENT'
    AND (
      _get_broadcast_flag($subject) IS NULL OR (
        _get_broadcast_flag($subject) & unhex('0x10000000')
      ) != 0
    )
    THEN 10000
    WHEN $anr_type = 'INPUT_DISPATCHING_TIMEOUT'
    THEN 5000
    WHEN $anr_type = 'INPUT_DISPATCHING_TIMEOUT_NO_FOCUSED_WINDOW'
    THEN 5000
    WHEN $anr_type = 'START_FOREGROUND_SERVICE'
    THEN 30000
    WHEN $anr_type = 'EXECUTING_SERVICE'
    THEN 20000
    WHEN $anr_type = 'JOB_SERVICE_START'
    THEN 8000
    WHEN $anr_type = 'JOB_SERVICE_STOP'
    THEN 8000
    WHEN $anr_type = 'JOB_SERVICE_BIND'
    THEN 8000
    WHEN $anr_type = 'JOB_SERVICE_NOTIFICATION_NOT_PROVIDED'
    THEN 8000
    WHEN $anr_type = 'BIND_APPLICATION'
    THEN 15000
    WHEN $anr_type = 'CONTENT_PROVIDER_NOT_RESPONDING'
    THEN NULL
    WHEN $anr_type = 'GPU_HANG'
    THEN NULL
    WHEN $anr_type = 'APP_TRIGGERED'
    THEN NULL
    WHEN $anr_type = 'FOREGROUND_SHORT_SERVICE_TIMEOUT'
    THEN 180000
    WHEN $anr_type = 'FOREGROUND_SERVICE_TIMEOUT'
    THEN 30000
    ELSE NULL
  END;

-- Some of the anr timer events don't use the standard anr types and we have to convert them (temporal solution).
-- For 'JobScheduler' there's not a 1:1 mapping to a standard type.
CREATE PERFETTO FUNCTION _platform_to_standard_anr_type(
    platform STRING
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $platform GLOB 'BROADCAST_TIMEOUT'
    THEN 'BROADCAST_OF_INTENT'
    WHEN $platform GLOB 'SERVICE_TIMEOUT'
    THEN 'EXECUTING_SERVICE'
    WHEN $platform GLOB 'SHORT_FGS_TIMEOUT'
    THEN 'FOREGROUND_SHORT_SERVICE_TIMEOUT'
    WHEN $platform GLOB 'SERVICE_FOREGROUND_TIMEOUT'
    THEN 'FOREGROUND_SERVICE_TIMEOUT'
    WHEN $platform GLOB 'JobScheduler'
    THEN NULL
    ELSE $platform
  END;

-- List of all ANRs that occurred in the trace (one row per ANR).
CREATE PERFETTO TABLE android_anrs (
  -- Name of the process that triggered the ANR.
  process_name STRING,
  -- PID of the process that triggered the ANR.
  pid LONG,
  -- UPID of the process that triggered the ANR.
  upid JOINID(process.id),
  -- UUID of the ANR (generated on the platform).
  error_id STRING,
  -- Timestamp of the ANR.
  ts TIMESTAMP,
  -- Subject line of the ANR.
  subject STRING,
  -- The duration between the timer expiration event and the anr counter event
  timer_delay LONG,
  -- The standard type of ANR.
  anr_type STRING,
  -- Duration of the ANR, computed from the timer expiration event.
  anr_dur_ms LONG,
  -- Default duration of the ANR, based on the anr_type (default means in AOSP/Pixel).
  -- Note: Other OEMs may have customized these timeout values, so the defaults
  -- provided here might not be accurate for all devices.
  default_anr_dur_ms LONG
) AS
-- Process and PID that ANRed.
WITH
  anr AS (
    SELECT
      -- Counter formats:
      -- v1: "ErrorId:<process_name>#<UUID>"
      -- v2: "ErrorId:<process_name> <pid>#<UUID>"
      str_split(substr(str_split(process_counter_track.name, '#', 0), 9), ' ', 0) AS process_name,
      cast_int!(STR_SPLIT(SUBSTR(STR_SPLIT(process_counter_track.name, '#', 0), 9), ' ', 1)) AS pid,
      str_split(process_counter_track.name, '#', 1) AS error_id,
      counter.ts
    FROM process_counter_track
    JOIN process
      USING (upid)
    JOIN counter
      ON (
        counter.track_id = process_counter_track.id
      )
    WHERE
      process_counter_track.name GLOB 'ErrorId:*' AND process.name = 'system_server'
  ),
  -- ANR subject line.
  subject AS (
    --- Counter format:
    --- "Subject(for ErrorId <UUID>):<subject>"
    SELECT
      substr(str_split(process_counter_track.name, ')', 0), 21) AS error_id,
      substr(
        process_counter_track.name,
        length(str_split(process_counter_track.name, ')', 0)) + 3
      ) AS subject
    FROM process_counter_track
    JOIN process
      USING (upid)
    WHERE
      process_counter_track.name GLOB 'Subject(for ErrorId *'
      AND process.name = 'system_server'
  ),
  -- ANR Timer Track
  anr_timer AS (
    WITH
      x AS (
        SELECT
          ts AS timer_ts,
          trim(substr(name, length('expired(') + 1), ')') AS params
        FROM slices
        WHERE
          name GLOB 'expired(*,*,*,*,*)'
      )
    SELECT
      timer_ts,
      cast_int!(STR_SPLIT(params, ',', 0)) AS timer_id,
      cast_int!(STR_SPLIT(params, ',', 1)) AS pid,
      cast_int!(STR_SPLIT(params, ',', 2)) AS uid,
      str_split(params, ',', 3) AS anr_type,
      cast_int!(STR_SPLIT(params, ',', 4)) AS anr_dur_ms
    FROM x
  ),
  -- Matching error_id with anr timers
  anr_potential_timers AS (
    SELECT
      *,
      (
        a.ts - at.timer_ts
      ) AS time_diff
    FROM anr AS a
    LEFT JOIN anr_timer AS at
      USING (pid)
    WHERE
      at.pid IS NULL OR a.ts >= at.timer_ts
  ),
  -- for each error_id, we choose the closest matching timer
  anr_ranked_timers AS (
    SELECT
      *,
      row_number() OVER (PARTITION BY error_id ORDER BY CASE WHEN timer_ts IS NULL THEN 1 ELSE 0 END ASC, time_diff ASC) AS rn
    FROM anr_potential_timers
  ),
  anr_best_timer AS (
    SELECT
      *
    FROM anr_ranked_timers
    WHERE
      rn = 1
  )
SELECT
  anr.process_name,
  anr.pid,
  process.upid,
  anr.error_id,
  anr.ts,
  s.subject,
  (
    anr.ts - abt.timer_ts
  ) AS timer_delay,
  coalesce(_platform_to_standard_anr_type(abt.anr_type), _extract_anr_type(s.subject)) AS anr_type,
  abt.anr_dur_ms,
  _default_anr_dur(_extract_anr_type(s.subject), s.subject) AS default_anr_dur_ms
FROM anr
LEFT JOIN subject AS s
  USING (error_id)
LEFT JOIN anr_best_timer AS abt
  USING (error_id)
LEFT JOIN process
  ON (
    process.pid = anr.pid
  );
