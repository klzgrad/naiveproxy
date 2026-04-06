--
-- Copyright 2024 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.frames.timeline;

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE slices.with_context;

CREATE PERFETTO TABLE _input_message_sent AS
SELECT
  str_split(str_split(slice.name, '=', 3), ')', 0) AS event_type,
  str_split(str_split(slice.name, '=', 2), ',', 0) AS event_seq,
  str_split(str_split(slice.name, '=', 1), ',', 0) AS event_channel,
  thread.tid,
  thread.name AS thread_name,
  process.upid,
  process.pid,
  process.name AS process_name,
  slice.ts,
  slice.dur,
  slice.track_id
FROM slice
JOIN thread_track
  ON thread_track.id = slice.track_id
JOIN thread
  USING (utid)
JOIN process
  USING (upid)
WHERE
  slice.name GLOB 'sendMessage(*'
ORDER BY
  event_seq;

CREATE PERFETTO TABLE _input_message_received AS
SELECT
  str_split(str_split(slice.name, '=', 3), ')', 0) AS event_type,
  str_split(str_split(slice.name, '=', 2), ',', 0) AS event_seq,
  str_split(str_split(slice.name, '=', 1), ',', 0) AS event_channel,
  thread.tid,
  thread.name AS thread_name,
  process.upid,
  process.pid,
  process.name AS process_name,
  slice.ts,
  slice.dur,
  slice.track_id
FROM slice
JOIN thread_track
  ON thread_track.id = slice.track_id
JOIN thread
  USING (utid)
JOIN process
  USING (upid)
WHERE
  slice.name GLOB 'receiveMessage(*'
ORDER BY
  event_seq;

CREATE PERFETTO TABLE _input_read_time AS
SELECT
  name,
  str_split(str_split(name, '=', 1), ')', 0) AS input_event_id,
  ts AS read_time
FROM slice
WHERE
  name GLOB 'UnwantedInteractionBlocker::notifyMotion*';

CREATE PERFETTO TABLE _event_seq_to_input_event_id AS
WITH
  _send_message_events AS (
    SELECT
      send_message_slice.name,
      enqueue_slice.name AS enqueue_name,
      thread_slice.utid,
      thread_slice.thread_name,
      str_split(str_split(send_message_slice.name, '=', 1), ',', 0) AS event_channel
    FROM slice AS send_message_slice
    JOIN slice AS publish_slice
      ON send_message_slice.parent_id = publish_slice.id
    JOIN slice AS start_dispatch_slice
      ON publish_slice.parent_id = start_dispatch_slice.id
    JOIN slice AS enqueue_slice
      ON start_dispatch_slice.parent_id = enqueue_slice.id
    JOIN thread_slice
      ON send_message_slice.id = thread_slice.id
    WHERE
      send_message_slice.name GLOB 'sendMessage(*'
      AND thread_slice.thread_name = 'InputDispatcher'
  )
SELECT
  str_split(str_split(name, '=', 2), ',', 0) AS event_seq,
  event_channel,
  str_split(str_split(event_channel, ' ', 1), '/', 0) AS process_name,
  str_split(str_split(enqueue_name, '=', 2), ')', 0) AS input_event_id,
  utid,
  thread_name
FROM _send_message_events;

CREATE PERFETTO TABLE _clean_android_frames AS
SELECT
  f.ts,
  f.dur,
  do_frame_slice.id AS do_frame_id,
  do_frame_slice.ts AS do_frame_ts,
  do_frame_slice.dur AS do_frame_dur,
  cast_int!(ui_thread_utid) AS utid,
  frame_id
FROM android_frames AS f
JOIN slice AS do_frame_slice
  ON f.do_frame_id = do_frame_slice.id;

CREATE PERFETTO TABLE _clean_deliver_events AS
SELECT
  s.id,
  s.ts,
  s.dur,
  cast_int!(t.utid) AS utid,
  t.process_name,
  str_split(s.name, '=', 3) AS extracted_input_event_id,
  str_split(str_split(parent.name, '_', 1), ' ', 0) AS event_action,
  parent.ts AS consume_time,
  parent.ts + parent.dur AS finish_time
FROM slice AS s
JOIN thread_slice AS t
  USING (id)
JOIN slice AS parent
  ON s.parent_id = parent.id
WHERE
  s.name GLOB 'deliverInputEvent src=*';

-- Exact Match: Find the Choreographer#doFrame that directly interval intersects
-- with the deliver event if it exists.
CREATE PERFETTO TABLE _input_event_frame_intersections AS
SELECT
  ii.id_0 AS do_frame_id_key,
  ii.id_1 AS event_id_key,
  0 AS is_speculative_match
FROM _interval_intersect!(
  (
    (
      SELECT 
        do_frame_id AS id, 
        do_frame_ts AS ts, 
        do_frame_dur AS dur,
        *
      FROM _clean_android_frames 
      WHERE do_frame_dur > 0
    ), 
    (SELECT 
      * 
      FROM _clean_deliver_events 
      WHERE dur > 0
    )
  ),
  (utid)
) AS ii;

CREATE PERFETTO TABLE _input_events_pending_frame_match AS
SELECT
  *
FROM _clean_deliver_events
WHERE
  NOT id IN (
    SELECT
      event_id_key
    FROM _input_event_frame_intersections
  );

-- Speculative Match: Find the immediate next frame for non-vsync-aligned events
-- (e.g. unbatched events)
CREATE PERFETTO TABLE _input_event_frame_speculative_matches AS
WITH
  _ordered_future_frames AS (
    SELECT
      e.id AS event_id_key,
      f.do_frame_id AS do_frame_id_key,
      row_number() OVER (PARTITION BY e.id ORDER BY f.do_frame_ts ASC) AS rn
    FROM _input_events_pending_frame_match AS e
    JOIN _clean_android_frames AS f
      ON e.utid = f.utid AND f.do_frame_ts >= e.ts
  )
SELECT
  do_frame_id_key,
  event_id_key,
  1 AS is_speculative_match
FROM _ordered_future_frames
WHERE
  rn = 1;

CREATE PERFETTO TABLE _input_event_frame_association AS
SELECT
  *
FROM _input_event_frame_intersections
UNION ALL
SELECT
  *
FROM _input_event_frame_speculative_matches;

CREATE PERFETTO TABLE _input_event_id_to_android_frame AS
SELECT
  dev.extracted_input_event_id AS input_event_id,
  dev.event_action,
  dev.consume_time,
  dev.finish_time,
  dev.utid,
  dev.process_name,
  af.frame_id,
  af.ts AS frame_ts,
  map.event_channel,
  CAST(assoc.is_speculative_match AS BOOL) AS is_speculative_match
FROM _input_event_frame_association AS assoc
JOIN _clean_android_frames AS af
  ON assoc.do_frame_id_key = af.do_frame_id
JOIN _clean_deliver_events AS dev
  ON assoc.event_id_key = dev.id
JOIN _event_seq_to_input_event_id AS map
  ON dev.extracted_input_event_id = map.input_event_id
  AND map.process_name = dev.process_name;

CREATE PERFETTO TABLE _app_frame_to_surface_flinger_frame AS
SELECT
  app.surface_frame_token AS app_surface_frame_token,
  surface_flinger.ts AS surface_flinger_ts,
  surface_flinger.dur AS surface_flinger_dur,
  app.ts AS app_ts,
  app.present_type,
  app.upid
FROM actual_frame_timeline_slice AS surface_flinger
JOIN actual_frame_timeline_slice AS app
  ON surface_flinger.display_frame_token = app.display_frame_token
  AND surface_flinger.id != app.id
WHERE
  surface_flinger.surface_frame_token IS NULL AND app.present_type != 'Dropped Frame';

CREATE PERFETTO TABLE _first_non_dropped_frame_after_input AS
SELECT
  _input_read_time.input_event_id,
  _input_read_time.read_time,
  (
    SELECT
      surface_flinger_ts + surface_flinger_dur
    FROM _app_frame_to_surface_flinger_frame AS sf_frames
    WHERE
      sf_frames.app_ts >= _input_event_id_to_android_frame.frame_ts
    LIMIT 1
  ) AS present_time,
  (
    SELECT
      app_surface_frame_token
    FROM _app_frame_to_surface_flinger_frame AS sf_frames
    WHERE
      sf_frames.app_ts >= _input_event_id_to_android_frame.frame_ts
    LIMIT 1
  ) AS frame_id,
  event_seq,
  event_action,
  _input_event_id_to_android_frame.is_speculative_match
FROM _input_event_id_to_android_frame
RIGHT JOIN _event_seq_to_input_event_id
  ON _input_event_id_to_android_frame.input_event_id = _event_seq_to_input_event_id.input_event_id
  AND _input_event_id_to_android_frame.event_channel = _event_seq_to_input_event_id.event_channel
JOIN _input_read_time
  ON _input_read_time.input_event_id = _event_seq_to_input_event_id.input_event_id;

-- TODO: consider all cases
CREATE PERFETTO FUNCTION _normalize_event_channel(
    event_channel STRING
)
RETURNS STRING AS
SELECT
  CASE
    -- '[Gesture Monitor] swipe-up' -> '[Gesture Monitor] swipe-up'
    WHEN $event_channel GLOB '[[]*] *'
    THEN $event_channel
    -- 'ccf6448 PopupWindow:b20fb4d' -> 'PopupWindow'
    WHEN $event_channel GLOB '* *:*'
    THEN trim(substr(str_split($event_channel, ':', 0), instr($event_channel, ' ') + 1))
    -- 'b3407d8 com.android.settings/com.android.settings.Settings$UserAspectRatioAppActivity' -> 'com.android.settings/com.android.settings.Settings$UserAspectRatioAppActivity'
    WHEN $event_channel GLOB '* *'
    THEN trim(substr($event_channel, instr($event_channel, ' ') + 1))
    -- 'PointerEventDispatcher23' -> 'PointerEventDispatcher'
    WHEN $event_channel GLOB '*[0-9]'
    THEN regexp_extract($event_channel, '^(.*[a-zA-Z])')
    ELSE $event_channel
  END;

-- All input events with round trip latency breakdown. Input delivery is socket based and every
-- input event sent from the OS needs to be ACK'ed by the app. This gives us 4 subevents to measure
-- latencies between:
-- 1. Input dispatch event sent from OS.
-- 2. Input dispatch event received in app.
-- 3. Input ACK event sent from app.
-- 4. Input ACK event received in OS.
CREATE PERFETTO TABLE android_input_events (
  -- Duration from input dispatch to input received.
  dispatch_latency_dur DURATION,
  -- Duration from input received to input ACK sent.
  handling_latency_dur DURATION,
  -- Duration from input ACK sent to input ACK received.
  ack_latency_dur DURATION,
  -- Duration from input dispatch to input event ACK received.
  total_latency_dur DURATION,
  -- Duration from input read to frame present time. Null if an input event has no associated frame event.
  end_to_end_latency_dur DURATION,
  -- Tid of thread receiving the input event.
  tid LONG,
  -- Name of thread receiving the input event.
  thread_name STRING,
  -- Upid of process receiving the input event.
  upid JOINID(process.upid),
  -- Pid of process receiving the input event.
  pid LONG,
  -- Name of process receiving the input event.
  process_name STRING,
  -- Input event type. See InputTransport.h: InputMessage#Type
  event_type STRING,
  -- Input event action.
  event_action STRING,
  -- Input event sequence number, monotonically increasing for an event channel and pid.
  event_seq STRING,
  -- Input event channel name.
  event_channel STRING,
  -- Normalized input event channel name.
  normalized_event_channel STRING,
  -- Unique identifier for the input event.
  input_event_id STRING,
  -- Timestamp input event was read by InputReader.
  read_time LONG,
  -- Thread track id of input event dispatching thread.
  dispatch_track_id JOINID(track.id),
  -- Timestamp input event was dispatched.
  dispatch_ts TIMESTAMP,
  -- Duration of input event dispatch.
  dispatch_dur DURATION,
  -- Thread track id of input event receiving thread.
  receive_track_id JOINID(track.id),
  -- Timestamp input event was received.
  receive_ts TIMESTAMP,
  -- Duration of input event receipt.
  receive_dur DURATION,
  -- Vsync Id associated with the input. Null if an input event has no associated frame event.
  frame_id LONG,
  -- Indicates if the frame association was speculative rather than exact based on id match.
  is_speculative_frame BOOL
) AS
WITH
  dispatch AS (
    SELECT
      *
    FROM _input_message_sent
    WHERE
      thread_name = 'InputDispatcher'
    ORDER BY
      event_seq,
      event_channel
  ),
  receive AS (
    SELECT
      *,
      replace(event_channel, '(client)', '(server)') AS dispatch_event_channel
    FROM _input_message_received
    WHERE
      NOT event_type IN ('0x2', 'FINISHED')
    ORDER BY
      event_seq,
      dispatch_event_channel
  ),
  finish AS (
    SELECT
      *,
      replace(event_channel, '(client)', '(server)') AS dispatch_event_channel
    FROM _input_message_sent
    WHERE
      thread_name != 'InputDispatcher'
    ORDER BY
      event_seq,
      dispatch_event_channel
  ),
  finish_ack AS (
    SELECT
      *
    FROM _input_message_received
    WHERE
      event_type IN ('0x2', 'FINISHED')
    ORDER BY
      event_seq,
      event_channel
  )
SELECT
  receive.ts - dispatch.ts AS dispatch_latency_dur,
  finish.ts - receive.ts AS handling_latency_dur,
  finish_ack.ts - finish.ts AS ack_latency_dur,
  finish_ack.ts - dispatch.ts AS total_latency_dur,
  frame.present_time - frame.read_time AS end_to_end_latency_dur,
  finish.tid AS tid,
  finish.thread_name AS thread_name,
  finish.upid AS upid,
  finish.pid AS pid,
  finish.process_name AS process_name,
  dispatch.event_type,
  frame.event_action,
  dispatch.event_seq,
  dispatch.event_channel,
  _normalize_event_channel(dispatch.event_channel) AS normalized_event_channel,
  frame.input_event_id,
  frame.read_time,
  dispatch.track_id AS dispatch_track_id,
  dispatch.ts AS dispatch_ts,
  dispatch.dur AS dispatch_dur,
  receive.ts AS receive_ts,
  receive.dur AS receive_dur,
  receive.track_id AS receive_track_id,
  frame.frame_id,
  frame.is_speculative_match AS is_speculative_frame
FROM dispatch
JOIN receive
  ON receive.dispatch_event_channel = dispatch.event_channel
  AND dispatch.event_seq = receive.event_seq
JOIN finish
  ON finish.dispatch_event_channel = dispatch.event_channel
  AND dispatch.event_seq = finish.event_seq
JOIN finish_ack
  ON finish_ack.event_channel = dispatch.event_channel
  AND dispatch.event_seq = finish_ack.event_seq
LEFT JOIN _first_non_dropped_frame_after_input AS frame
  ON frame.event_seq = dispatch.event_seq;

-- Key events processed by the Android framework (from android.input.inputevent data source).
CREATE PERFETTO VIEW android_key_events (
  -- ID of the trace entry
  id LONG,
  -- The randomly-generated ID associated with each input event processed
  -- by Android Framework, used to track the event through the input pipeline
  event_id LONG,
  -- The timestamp of when the input event was processed by the system
  ts TIMESTAMP,
  -- Details of the input event parsed from the proto message
  arg_set_id ARGSETID,
  -- Event source e.g. touchscreen, keyboard
  source LONG,
  -- Action e.g. down, move
  action LONG,
  -- Device id
  device_id LONG,
  -- Display id
  display_id LONG,
  -- Key code
  key_code LONG
) AS
SELECT
  id,
  event_id,
  ts,
  arg_set_id,
  source,
  action,
  device_id,
  display_id,
  key_code
FROM __intrinsic_android_key_events;

-- Motion events processed by the Android framework (from android.input.inputevent data source).
CREATE PERFETTO VIEW android_motion_events (
  -- ID of the trace entry
  id LONG,
  -- The randomly-generated ID associated with each input event processed
  -- by Android Framework, used to track the event through the input pipeline
  event_id LONG,
  -- The timestamp of when the input event was processed by the system
  ts TIMESTAMP,
  -- Details of the input event parsed from the proto message
  arg_set_id ARGSETID,
  -- Event source e.g. touchscreen, keyboard
  source LONG,
  -- Action e.g. down, move
  action LONG,
  -- Device id
  device_id LONG,
  -- Display id
  display_id LONG
) AS
SELECT
  id,
  event_id,
  ts,
  arg_set_id,
  source,
  action,
  device_id,
  display_id
FROM __intrinsic_android_motion_events;

-- Input event dispatching information in Android (from android.input.inputevent data source).
CREATE PERFETTO VIEW android_input_event_dispatch (
  -- ID of the trace entry
  id LONG,
  -- Event ID of the input event that was dispatched
  event_id LONG,
  -- Details of the input event parsed from the proto message
  arg_set_id ARGSETID,
  -- Vsync ID that identifies the state of the windows during which the dispatch decision was made
  vsync_id LONG,
  -- Window ID of the window receiving the event
  window_id LONG
) AS
SELECT
  id,
  event_id,
  arg_set_id,
  vsync_id,
  window_id
FROM __intrinsic_android_input_event_dispatch;

CREATE PERFETTO TABLE _input_consumers_lookup AS
SELECT
  id,
  track_id,
  ts,
  dur,
  printf('0x%x', extract_arg(arg_set_id, 'cookie')) AS cookie
FROM slice
WHERE
  name GLOB 'InputConsumer processing on*';

CREATE PERFETTO INDEX _input_consumers_lookup_idx ON _input_consumers_lookup(cookie);

CREATE PERFETTO TABLE _frame_choreographer_lookup AS
SELECT
  id,
  track_id,
  ts,
  dur,
  cast_int!(str_split(name, ' ', 1)) AS frame_id
FROM slice
WHERE
  name GLOB 'Choreographer#doFrame*';

CREATE PERFETTO INDEX _frame_choreographer_lookup_idx ON _frame_choreographer_lookup(frame_id);

-- Retrieves the full lifecycle of an Android input event (Read -> Dispatch -> Receive -> Consume -> Frame)
-- by matching a given slice ID from any stage of the pipeline.
CREATE PERFETTO FUNCTION _android_input_lifecycle_by_slice_id(
    -- The Slice ID any slice in the input lifecycle.
    slice_id LONG
)
RETURNS TABLE (
  -- The unique integer identifier of the input event.
  input_id STRING,
  -- The name of the input channel.
  channel STRING,
  -- Total duration from input read to end of frame.
  total_latency LONG,
  -- Timestamps
  ts_reader LONG,
  ts_dispatch LONG,
  ts_receive LONG,
  ts_consume LONG,
  ts_frame LONG,
  -- InputReader Stage
  id_reader LONG,
  track_reader LONG,
  dur_reader LONG,
  -- InputDispatcher Stage
  id_dispatch LONG,
  track_dispatch LONG,
  dur_dispatch LONG,
  -- App Receiver Stage
  id_receive LONG,
  track_receive LONG,
  dur_receive LONG,
  -- InputConsumer Stage
  id_consume LONG,
  track_consume LONG,
  dur_consume LONG,
  -- Choreographer Frame Stage
  id_frame LONG,
  track_frame LONG,
  dur_frame LONG,
  is_speculative_frame BOOL
) AS
SELECT
  e.input_event_id AS input_id,
  e.event_channel AS channel,
  e.end_to_end_latency_dur AS total_latency,
  e.read_time AS ts_reader,
  e.dispatch_ts AS ts_dispatch,
  e.receive_ts AS ts_receive,
  s_cons.ts AS ts_consume,
  s_frame.ts AS ts_frame,
  s_read.id AS id_reader,
  s_read.track_id AS track_reader,
  s_read.dur AS dur_reader,
  s_disp.id AS id_dispatch,
  e.dispatch_track_id AS track_dispatch,
  s_disp.dur AS dur_dispatch,
  s_recv.id AS id_receive,
  e.receive_track_id AS track_receive,
  s_recv.dur AS dur_receive,
  s_cons.id AS id_consume,
  s_cons.track_id AS track_consume,
  s_cons.dur AS dur_consume,
  s_frame.id AS id_frame,
  s_frame.track_id AS track_frame,
  s_frame.dur AS dur_frame,
  e.is_speculative_frame
FROM android_input_events AS e
LEFT JOIN slice AS s_read
  ON s_read.ts = e.read_time AND s_read.track_id != 0
LEFT JOIN slice AS s_disp
  ON s_disp.ts = e.dispatch_ts AND s_disp.track_id = e.dispatch_track_id
LEFT JOIN slice AS s_recv
  ON s_recv.ts = e.receive_ts AND s_recv.track_id = e.receive_track_id
LEFT JOIN _input_consumers_lookup AS s_cons
  ON s_cons.cookie = e.event_seq
LEFT JOIN _frame_choreographer_lookup AS s_frame
  ON s_frame.frame_id = CAST(e.frame_id AS LONG)
WHERE
  $slice_id IN (s_read.id, s_disp.id, s_recv.id, s_cons.id, s_frame.id);
