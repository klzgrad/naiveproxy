-- Copyright 2025 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.suspend;

-- Table of parsed wakeup / suspend failure events with suspend backoff.
--
-- Certain wakeup events may have multiple causes. When this occurs we
-- split those causes into multiple rows in this table with the same ts
-- and raw_wakeup values.
CREATE PERFETTO TABLE android_wakeups (
  -- Timestamp.
  ts TIMESTAMP,
  -- Duration for which we blame the wakeup for wakefulness. This is the
  -- suspend backoff duration if one exists, or the lesser of (5 seconds,
  -- time to next suspend event).
  dur DURATION,
  -- Original wakeup string from the kernel.
  raw_wakeup STRING,
  -- Wakeup attribution, as determined on device. May be absent.
  on_device_attribution STRING,
  -- One of 'normal' (device woke from sleep), 'abort_pending' (suspend failed
  -- due to a wakeup that was scheduled by a device during the suspend
  -- process), 'abort_last_active' (suspend failed, listing the last active
  -- device) or 'abort_other' (suspend failed for another reason).
  type STRING,
  -- Individual wakeup cause. Usually the name of the device that cause the
  -- wakeup, or the raw message in the 'abort_other' case.
  item STRING,
  -- 'good' or 'bad'. 'bad' means failed or short such that suspend backoff
  -- is triggered.
  suspend_quality STRING,
  -- 'new', 'continue' or NULL. Set if suspend backoff is triggered.
  backoff_state STRING,
  -- 'short', 'failed' or NULL. Set if suspend backoff is triggered.
  backoff_reason STRING,
  -- Number of times suspend backoff has occurred, or NULL. Set if suspend
  -- backoff is triggered.
  backoff_count LONG,
  -- Next suspend backoff duration, or NULL. Set if suspend backoff is
  -- triggered.
  backoff_millis LONG
) AS
WITH
  wakeup_reason AS (
    -- wakeup_reason is recorded ASAP after wakeup; its packet timestamp is reliable. It also contains
    -- a millisecond timestamp which we can use to associate the wakeup_reason with a later
    -- wakeup_attribution.
    SELECT
      ts,
      substr(i.name, 0, instr(i.name, ' ')) AS id_timestamp,
      substr(i.name, instr(i.name, ' ') + 1) AS raw_wakeup
    FROM track AS t
    JOIN instant AS i
      ON t.id = i.track_id
    WHERE
      t.name = 'wakeup_reason'
  ),
  wakeup_attribution AS (
    -- wakeup_attribution is recorded at some later time after wakeup and after batterystat has decided
    -- how to attribute it. We therefore associate it with the original wakeup via the embedded
    -- millisecond timestamp.
    SELECT
      substr(i.name, 0, instr(i.name, ' ')) AS id_timestamp,
      substr(i.name, instr(i.name, ' ') + 1) AS on_device_attribution
    FROM track AS t
    JOIN instant AS i
      ON t.id = i.track_id
    WHERE
      t.name = 'wakeup_attribution'
  ),
  step1 AS (
    -- Join reason, attribution, and backoff. reason and attribution contain a timestamp that can be
    -- used as an ID for joining. backoff does not but we know if it occurs at all it will always
    -- occur just before the reason. We therefore "join" with a union+lag() to be efficient.
    SELECT
      ts,
      raw_wakeup,
      on_device_attribution,
      NULL AS raw_backoff
    FROM wakeup_reason AS r
    LEFT OUTER JOIN wakeup_attribution
      USING (id_timestamp)
    UNION ALL
    SELECT
      ts,
      NULL AS raw_wakeup,
      NULL AS on_device_attribution,
      i.name AS raw_backoff
    FROM track AS t
    JOIN instant AS i
      ON t.id = i.track_id
    WHERE
      t.name = 'suspend_backoff'
  ),
  step2 AS (
    SELECT
      ts,
      raw_wakeup,
      on_device_attribution,
      lag(raw_backoff) OVER (ORDER BY ts) AS raw_backoff
    FROM step1
  ),
  step3 AS (
    SELECT
      ts,
      raw_wakeup,
      on_device_attribution,
      str_split(raw_backoff, ' ', 0) AS suspend_quality,
      str_split(raw_backoff, ' ', 1) AS backoff_state,
      str_split(raw_backoff, ' ', 2) AS backoff_reason,
      CAST(str_split(raw_backoff, ' ', 3) AS INTEGER) AS backoff_count,
      CAST(str_split(raw_backoff, ' ', 4) AS INTEGER) AS backoff_millis,
      FALSE AS suspend_end
    FROM step2
    WHERE
      raw_wakeup IS NOT NULL
    UNION ALL
    SELECT
      ts,
      NULL AS raw_wakeup,
      NULL AS on_device_attribution,
      NULL AS suspend_quality,
      NULL AS backoff_state,
      NULL AS backoff_reason,
      NULL AS backoff_count,
      NULL AS backoff_millis,
      TRUE AS suspend_end
    FROM android_suspend_state
    WHERE
      power_state = 'suspended'
  ),
  -- If the device is in a failure-to-suspend loop it will back off and take
  -- up to ~1 minute to suspend. We should allow ourselves to apportion that time
  -- to the wakeup (unless there was another wakeup or suspend following it).
  -- NB a failure to suspend loop can also manifest as actually suspending and then
  -- waking up after a few milliseconds so we don't attempt to filter by aborted
  -- suspends here.
  step4 AS (
    SELECT
      ts,
      CASE suspend_quality
        WHEN 'good'
        THEN min(lead(ts, 1, ts + 5e9) OVER (ORDER BY ts) - ts, 5e9)
        WHEN 'bad'
        THEN backoff_millis * 1000000
        ELSE 0
      END AS dur,
      raw_wakeup,
      on_device_attribution,
      suspend_quality,
      backoff_state,
      backoff_reason,
      backoff_count,
      backoff_millis,
      suspend_end
    FROM step3
  ),
  step5 AS (
    SELECT
      ts,
      dur,
      raw_wakeup,
      on_device_attribution,
      suspend_quality,
      backoff_state,
      backoff_reason,
      backoff_count,
      backoff_millis
    FROM step4
    WHERE
      NOT suspend_end
  ),
  -- Each wakeup can contain multiple reasons. We need to parse the wakeup in order to get them out.
  -- This is made more annoying by the fact it can be in multiple formats:
  -- If the wakeup represents an aborted attempt at suspending, it will be prefixed with one of a
  -- couple of variations on "Abort:" and may (in the pending wakeup case) contain multiple reasons,
  -- separated by spaces
  --
  -- example: "Abort: Device 0001:01:00.0 failed to suspend: error -16"
  -- example: "Abort: Pending Wakeup Sources: wlan_oob_irq_wake wlan_txfl_wake wlan_rx_wake"
  -- example: "Abort: Last active Wakeup Source: wlan_oob_irq_wake"
  --
  -- For a normal wakeup there may be multiple reasons separated by ":" and each preceded by a numeric
  -- IRQ. Essentially the text is a looked-up explanation of the IRQ number.
  --
  -- example: "170 176a0000.mbox:440 dhdpcie_host_wake"
  -- example: "374 max_fg_irq:440 dhdpcie_host_wake"
  --
  step6 AS (
    SELECT
      ts,
      dur,
      raw_wakeup,
      on_device_attribution,
      suspend_quality,
      backoff_state,
      backoff_reason,
      backoff_count,
      backoff_millis,
      CASE
        WHEN raw_wakeup GLOB 'Abort: Pending Wakeup Sources: *'
        THEN 'abort_pending'
        WHEN raw_wakeup GLOB 'Abort: Last active Wakeup Source: *'
        THEN 'abort_last_active'
        WHEN raw_wakeup GLOB 'Abort: *'
        THEN 'abort_other'
        ELSE 'normal'
      END AS type,
      CASE
        WHEN raw_wakeup GLOB 'Abort: Pending Wakeup Sources: *'
        THEN substr(raw_wakeup, 32)
        WHEN raw_wakeup GLOB 'Abort: Last active Wakeup Source: *'
        THEN substr(raw_wakeup, 35)
        WHEN raw_wakeup GLOB 'Abort: *'
        THEN substr(raw_wakeup, 8)
        ELSE raw_wakeup
      END AS main,
      CASE
        WHEN raw_wakeup GLOB 'Abort: Pending Wakeup Sources: *'
        THEN ' '
        WHEN raw_wakeup GLOB 'Abort: *'
        THEN 'no delimiter needed'
        ELSE ':'
      END AS delimiter
    FROM step5
  ),
  step7 AS (
    SELECT
      ts,
      dur,
      raw_wakeup,
      on_device_attribution,
      suspend_quality,
      backoff_state,
      backoff_reason,
      backoff_count,
      backoff_millis,
      type,
      str_split(main, delimiter, 0) AS item_0,
      str_split(main, delimiter, 1) AS item_1,
      str_split(main, delimiter, 2) AS item_2,
      str_split(main, delimiter, 3) AS item_3
    FROM step6
  ),
  step8 AS (
    SELECT
      ts,
      dur,
      raw_wakeup,
      on_device_attribution,
      suspend_quality,
      backoff_state,
      backoff_reason,
      backoff_count,
      backoff_millis,
      type,
      item_0 AS item
    FROM step7
    UNION ALL
    SELECT
      ts,
      dur,
      raw_wakeup,
      on_device_attribution,
      suspend_quality,
      backoff_state,
      backoff_reason,
      backoff_count,
      backoff_millis,
      type,
      item_1 AS item
    FROM step7
    WHERE
      item_1 IS NOT NULL
    UNION ALL
    SELECT
      ts,
      dur,
      raw_wakeup,
      on_device_attribution,
      suspend_quality,
      backoff_state,
      backoff_reason,
      backoff_count,
      backoff_millis,
      type,
      item_2 AS item
    FROM step7
    WHERE
      item_2 IS NOT NULL
    UNION ALL
    SELECT
      ts,
      dur,
      raw_wakeup,
      on_device_attribution,
      suspend_quality,
      backoff_state,
      backoff_reason,
      backoff_count,
      backoff_millis,
      type,
      item_3 AS item
    FROM step7
    WHERE
      item_3 IS NOT NULL
  )
SELECT
  ts,
  cast_int!(dur) AS dur,
  raw_wakeup,
  on_device_attribution,
  type,
  -- Remove the numeric IRQ, it duplicates the text and is less comprehensible.
  CASE WHEN type = 'normal' THEN coalesce(str_split(item, ' ', 1), item) ELSE item END AS item,
  suspend_quality,
  backoff_state,
  coalesce(backoff_reason, 'none') AS backoff_reason,
  backoff_count,
  backoff_millis
FROM step8;
