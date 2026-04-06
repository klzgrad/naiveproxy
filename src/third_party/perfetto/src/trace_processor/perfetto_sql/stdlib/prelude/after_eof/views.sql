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

INCLUDE PERFETTO MODULE prelude.after_eof.casts;

-- Counters are values put into tracks during parsing of the trace.
CREATE PERFETTO VIEW counter (
  -- Unique id of a counter value
  id ID,
  -- Time of fetching the counter value.
  ts TIMESTAMP,
  -- Track this counter value belongs to.
  track_id JOINID(track.id),
  -- Value.
  value DOUBLE,
  -- Additional information about the counter value.
  arg_set_id ARGSETID
) AS
SELECT
  id,
  ts,
  track_id,
  value,
  arg_set_id
FROM __intrinsic_counter;

-- Contains slices from userspace which explains what threads were doing
-- during the trace.
CREATE PERFETTO VIEW slice (
  -- The id of the slice.
  id ID,
  -- The timestamp at the start of the slice in nanoseconds. The actual value
  -- depends on the `primary_trace_clock` selected in TraceConfig. This is often
  -- the value of a monotonic counter since device boot so is only meaningful in
  -- the context of a trace.
  ts TIMESTAMP,
  -- The duration of the slice in nanoseconds.
  dur DURATION,
  -- The id of the track this slice is located on.
  track_id JOINID(track.id),
  -- The "category" of the slice. If this slice originated with track_event,
  -- this column contains the category emitted.
  -- Otherwise, it is likely to be null (with limited exceptions).
  category STRING,
  -- The name of the slice. The name describes what was happening during the
  -- slice.
  name STRING,
  -- The depth of the slice in the current stack of slices.
  depth LONG,
  -- The id of the parent (i.e. immediate ancestor) slice for this slice.
  parent_id JOINID(slice.id),
  -- The id of the argument set associated with this slice.
  arg_set_id ARGSETID,
  -- The thread timestamp at the start of the slice. This column will only be
  -- populated if thread timestamp collection is enabled with track_event.
  thread_ts TIMESTAMP,
  -- The thread time used by this slice. This column will only be populated if
  -- thread timestamp collection is enabled with track_event.
  thread_dur DURATION,
  -- The value of the CPU instruction counter at the start of the slice. This
  -- column will only be populated if thread instruction collection is enabled
  -- with track_event.
  thread_instruction_count LONG,
  -- The change in value of the CPU instruction counter between the start and
  -- end of the slice. This column will only be populated if thread instruction
  -- collection is enabled with track_event.
  thread_instruction_delta LONG,
  -- Alias of `category`.
  cat STRING,
  -- Alias of `id`.
  slice_id JOINID(slice.id)
) AS
SELECT
  *,
  category AS cat,
  id AS slice_id
FROM __intrinsic_slice;

-- Contains instant events from userspace which indicates what happened at a
-- single moment in time.
CREATE PERFETTO VIEW instant (
  -- The timestamp of the instant.
  ts TIMESTAMP,
  -- The id of the track this instant is located on.
  track_id JOINID(track.id),
  -- The name of the instant. The name describes what happened during the
  -- instant.
  name STRING,
  -- The id of the argument set associated with this instant.
  arg_set_id ARGSETID
) AS
SELECT
  ts,
  track_id,
  name,
  arg_set_id
FROM slice
WHERE
  dur = 0;

-- Alternative alias of table `slice`.
CREATE PERFETTO VIEW slices (
  -- Alias of `slice.id`.
  id JOINID(slice.id),
  -- Alias of `slice.ts`.
  ts TIMESTAMP,
  -- Alias of `slice.dur`.
  dur DURATION,
  -- Alias of `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias of `slice.category`.
  category STRING,
  -- Alias of `slice.name`.
  name STRING,
  -- Alias of `slice.depth`.
  depth LONG,
  -- Alias of `slice.parent_id`.
  parent_id JOINID(slice.id),
  -- Alias of `slice.arg_set_id`.
  arg_set_id ARGSETID,
  -- Alias of `slice.thread_ts`.
  thread_ts TIMESTAMP,
  -- Alias of `slice.thread_dur`.
  thread_dur DURATION,
  -- Alias of `slice.thread_instruction_count`.
  thread_instruction_count LONG,
  -- Alias of `slice.thread_instruction_delta`.
  thread_instruction_delta LONG,
  -- Alias of `slice.cat`.
  cat STRING,
  -- Alias of `slice.slice_id`.
  slice_id JOINID(slice.id)
) AS
SELECT
  *
FROM slice;

-- Contains information of threads seen during the trace.
CREATE PERFETTO VIEW thread (
  -- The id of the thread. Prefer using `utid` instead.
  id ID,
  -- Unique thread id. This is != the OS tid. This is a monotonic number
  -- associated to each thread. The OS thread id (tid) cannot be used as primary
  -- key because tids and pids are recycled by most kernels.
  utid ID,
  -- The OS id for this thread. Note: this is *not* unique over the lifetime of
  -- the trace so cannot be used as a primary key. Use |utid| instead.
  tid LONG,
  -- The name of the thread. Can be populated from many sources (e.g. ftrace,
  -- /proc scraping, track event etc).
  name STRING,
  -- The start timestamp of this thread (if known). Is null in most cases unless
  -- a thread creation event is enabled (e.g. task_newtask ftrace event on
  -- Linux/Android).
  start_ts TIMESTAMP,
  -- The end timestamp of this thread (if known). Is null in most cases unless
  -- a thread destruction event is enabled (e.g. sched_process_free ftrace event
  -- on Linux/Android).
  end_ts TIMESTAMP,
  -- The process hosting this thread.
  upid JOINID(process.id),
  -- Boolean indicating if this thread is the main thread in the process.
  is_main_thread BOOL,
  -- Boolean indicating if this thread is a kernel idle thread.
  is_idle BOOL,
  -- Machine identifier
  machine_id JOINID(machine.id),
  -- Extra args for this thread.
  arg_set_id ARGSETID
) AS
SELECT
  id AS utid,
  *
FROM __intrinsic_thread;

-- Contains information of processes seen during the trace.
CREATE PERFETTO VIEW process (
  -- The id of the process. Prefer using `upid` instead.
  id ID,
  -- Unique process id. This is != the OS pid. This is a monotonic number
  -- associated to each process. The OS process id (pid) cannot be used as
  -- primary key because tids and pids are recycled by most kernels.
  upid JOINID(process.id),
  -- The OS id for this process. Note: this is *not* unique over the lifetime of
  -- the trace so cannot be used as a primary key. Use |upid| instead.
  pid LONG,
  -- The name of the process. Can be populated from many sources (e.g. ftrace,
  -- /proc scraping, track event etc).
  name STRING,
  -- The start timestamp of this process (if known). Is null in most cases
  -- unless a process creation event is enabled (e.g. task_newtask ftrace event
  -- on Linux/Android).
  start_ts TIMESTAMP,
  -- The end timestamp of this process (if known). Is null in most cases unless
  -- a process destruction event is enabled (e.g. sched_process_free ftrace
  -- event on Linux/Android).
  end_ts TIMESTAMP,
  -- The upid of the process which caused this process to be spawned.
  parent_upid JOINID(process.id),
  -- The Unix user id of the process.
  uid LONG,
  -- Android appid of this process.
  android_appid LONG,
  -- Android user id of this process.
  android_user_id LONG,
  -- /proc/cmdline for this process.
  cmdline STRING,
  -- Extra args for this process.
  arg_set_id ARGSETID,
  -- Machine identifier
  machine_id JOINID(machine.id)
) AS
SELECT
  id AS upid,
  *
FROM __intrinsic_process;

-- Arbitrary key-value pairs which allow adding metadata to other, strongly
-- typed tables.
-- Note: for a given row, only one of |int_value|, |string_value|, |real_value|
-- will be non-null.
CREATE PERFETTO VIEW args (
  -- The id of the arg.
  id ID,
  -- The id for a single set of arguments.
  arg_set_id ARGSETID,
  -- The "flat key" of the arg: this is the key without any array indexes.
  flat_key STRING,
  -- The key for the arg.
  key STRING,
  -- The integer value of the arg.
  int_value LONG,
  -- The string value of the arg.
  string_value STRING,
  -- The double value of the arg.
  real_value DOUBLE,
  -- The type of the value of the arg. Will be one of 'int', 'uint', 'string',
  -- 'real', 'pointer', 'bool' or 'json'.
  value_type STRING,
  -- The human-readable formatted value of the arg.
  display_value STRING
) AS
SELECT
  *,
  -- This should be kept in sync with GlobalArgsTracker::AddArgSet.
  CASE value_type
    WHEN 'int'
    THEN cast_string!(int_value)
    WHEN 'uint'
    THEN printf('%u', int_value)
    WHEN 'string'
    THEN string_value
    WHEN 'real'
    THEN cast_string!(real_value)
    WHEN 'pointer'
    THEN printf('0x%x', int_value)
    WHEN 'bool'
    THEN (
      CASE WHEN int_value != 0 THEN 'true' ELSE 'false' END
    )
    WHEN 'json'
    THEN string_value
    ELSE NULL
  END AS display_value
FROM __intrinsic_args;

-- Contains the Linux perf sessions in the trace.
CREATE PERFETTO VIEW perf_session (
  -- The id of the perf session. Prefer using `perf_session_id` instead.
  id LONG,
  -- The id of the perf session.
  perf_session_id LONG,
  -- Command line used to collect the data.
  cmdline STRING
) AS
SELECT
  *,
  id AS perf_session_id
FROM __intrinsic_perf_session;

-- Log entries from Android logcat.
--
-- NOTE: this table is not sorted by timestamp.
CREATE PERFETTO VIEW android_logs (
  -- Which row in the table the log corresponds to.
  id ID,
  -- Timestamp of log entry.
  ts TIMESTAMP,
  -- Thread writing the log entry.
  utid JOINID(thread.id),
  -- Priority of the log. 3=DEBUG, 4=INFO, 5=WARN, 6=ERROR.
  prio LONG,
  -- Tag of the log entry.
  tag STRING,
  -- Content of the log entry
  msg STRING
) AS
SELECT
  id,
  ts,
  utid,
  prio,
  tag,
  msg
FROM __intrinsic_android_logs;

-- Contains flow events linking slices.
CREATE PERFETTO VIEW flow (
  -- The id of the flow.
  id ID,
  -- Id of the slice that this flow flows out of.
  slice_out JOINID(slice.id),
  -- Id of the slice that this flow flows into.
  slice_in JOINID(slice.id),
  -- Trace id of the flow.
  trace_id LONG,
  -- Args for this flow.
  arg_set_id ARGSETID
) AS
SELECT
  id,
  slice_out,
  slice_in,
  trace_id,
  arg_set_id
FROM __intrinsic_flow;

-- A table presenting all game modes and interventions of games installed on
-- the system.
CREATE PERFETTO VIEW android_game_intervention_list (
  -- The id of the row.
  id ID,
  -- Name of the package.
  package_name STRING,
  -- UID processes of this package runs as.
  uid LONG,
  -- Current game mode the game is running at.
  current_mode LONG,
  -- Bool whether standard mode is supported.
  standard_mode_supported LONG,
  -- Resolution downscaling factor of standard mode.
  standard_mode_downscale DOUBLE,
  -- Bool whether ANGLE is used in standard mode.
  standard_mode_use_angle LONG,
  -- Frame rate that the game is throttled at in standard mode.
  standard_mode_fps DOUBLE,
  -- Bool whether performance mode is supported.
  perf_mode_supported LONG,
  -- Resolution downscaling factor of performance mode.
  perf_mode_downscale DOUBLE,
  -- Bool whether ANGLE is used in performance mode.
  perf_mode_use_angle LONG,
  -- Frame rate that the game is throttled at in performance mode.
  perf_mode_fps DOUBLE,
  -- Bool whether battery mode is supported.
  battery_mode_supported LONG,
  -- Resolution downscaling factor of battery mode.
  battery_mode_downscale DOUBLE,
  -- Bool whether ANGLE is used in battery mode.
  battery_mode_use_angle LONG,
  -- Frame rate that the game is throttled at in battery mode.
  battery_mode_fps DOUBLE
) AS
SELECT
  *
FROM __intrinsic_android_game_intervention_list;

-- Dumpsys entries from Android dumpstate.
CREATE PERFETTO VIEW android_dumpstate (
  -- The id of the row.
  id ID,
  -- Name of the dumpstate section.
  section STRING,
  -- Name of the dumpsys service.
  service STRING,
  -- Line-by-line contents of the section/service.
  line STRING
) AS
SELECT
  *
FROM __intrinsic_android_dumpstate;

-- The profiler smaps contains the memory stats for virtual memory ranges.
CREATE PERFETTO VIEW profiler_smaps (
  -- The id of the row.
  id ID,
  -- The unique PID of the process.
  upid LONG,
  -- Timestamp of the snapshot.
  ts TIMESTAMP,
  -- The mmaped file, as per /proc/pid/smaps.
  path STRING,
  -- Total size of the mapping.
  size_kb LONG,
  -- KB of this mapping that are private dirty RSS.
  private_dirty_kb LONG,
  -- KB of this mapping that are in swap.
  swap_kb LONG,
  -- File name.
  file_name STRING,
  -- Start address.
  start_address LONG,
  -- Module timestamp.
  module_timestamp LONG,
  -- Module debug id.
  module_debugid STRING,
  -- Module debug path.
  module_debug_path STRING,
  -- Protection flags.
  protection_flags LONG,
  -- Private clean resident KB.
  private_clean_resident_kb LONG,
  -- Shared dirty resident KB.
  shared_dirty_resident_kb LONG,
  -- Shared clean resident KB.
  shared_clean_resident_kb LONG,
  -- Locked KB.
  locked_kb LONG,
  -- Proportional resident KB.
  proportional_resident_kb LONG
) AS
SELECT
  *
FROM __intrinsic_profiler_smaps;

-- Metadata about packages installed on the system.
CREATE PERFETTO VIEW package_list (
  -- The id of the row.
  id ID,
  -- Name of the package.
  package_name STRING,
  -- UID processes of this package run as.
  uid LONG,
  -- Bool whether this app is debuggable.
  debuggable LONG,
  -- Bool whether this app is profileable.
  profileable_from_shell LONG,
  -- versionCode from the APK.
  version_code LONG
) AS
SELECT
  *
FROM __intrinsic_package_list;

-- A mapping (binary / library) in a process.
CREATE PERFETTO VIEW stack_profile_mapping (
  -- The id of the row.
  id ID,
  -- Hex-encoded Build ID of the binary / library.
  build_id STRING,
  -- Exact offset.
  exact_offset LONG,
  -- Start offset.
  start_offset LONG,
  -- Start of the mapping in the process' address space.
  start LONG,
  -- End of the mapping in the process' address space.
  end LONG,
  -- Load bias.
  load_bias LONG,
  -- Filename of the binary / library.
  name STRING
) AS
SELECT
  *
FROM __intrinsic_stack_profile_mapping;

-- A frame on the callstack. This is a location in a program.
CREATE PERFETTO VIEW stack_profile_frame (
  -- The id of the row.
  id ID,
  -- Name of the function this location is in.
  name STRING,
  -- The mapping (library / binary) this location is in.
  mapping JOINID(stack_profile_mapping.id),
  -- The program counter relative to the start of the mapping.
  rel_pc LONG,
  -- If the profile was offline symbolized, the offline symbol information.
  symbol_set_id LONG,
  -- Deobfuscated name of the function this location is in.
  deobfuscated_name STRING
) AS
SELECT
  *
FROM __intrinsic_stack_profile_frame;

-- A callsite. This is a list of frames that were on the stack.
CREATE PERFETTO VIEW stack_profile_callsite (
  -- The id of the row.
  id ID,
  -- Distance from the bottom-most frame of the callstack.
  depth LONG,
  -- Parent frame on the callstack. NULL for the bottom-most.
  parent_id JOINID(stack_profile_callsite.id),
  -- Frame at this position in the callstack.
  frame_id JOINID(stack_profile_frame.id)
) AS
SELECT
  *
FROM __intrinsic_stack_profile_callsite;

-- Table containing stack samples from CPU profiling.
CREATE PERFETTO VIEW cpu_profile_stack_sample (
  -- The id of the row.
  id ID,
  -- Timestamp of the sample.
  ts TIMESTAMP,
  -- Unwound callstack.
  callsite_id JOINID(stack_profile_callsite.id),
  -- Thread that was active when the sample was taken.
  utid LONG,
  -- Process priority.
  process_priority LONG
) AS
SELECT
  *
FROM __intrinsic_cpu_profile_stack_sample;

-- Samples from MacOS Instruments.
CREATE PERFETTO VIEW instruments_sample (
  -- The id of the row.
  id ID,
  -- Timestamp of the sample.
  ts TIMESTAMP,
  -- Sampled thread.
  utid LONG,
  -- If set, unwound callstack of the sampled thread.
  callsite_id JOINID(stack_profile_callsite.id),
  -- Core the sampled thread was running on.
  cpu LONG
) AS
SELECT
  *
FROM __intrinsic_instruments_sample;

-- Symbolization data for a frame.
CREATE PERFETTO VIEW stack_profile_symbol (
  -- The id of the row.
  id ID,
  -- Symbol set id.
  symbol_set_id LONG,
  -- Name of the function.
  name STRING,
  -- Name of the source file containing the function.
  source_file STRING,
  -- Line number of the frame in the source file.
  line_number LONG,
  -- Whether this function was inlined.
  inlined LONG
) AS
SELECT
  *
FROM __intrinsic_stack_profile_symbol;

-- Allocations that happened at a callsite.
CREATE PERFETTO VIEW heap_profile_allocation (
  -- The id of the row.
  id ID,
  -- The timestamp the allocations happened at.
  ts TIMESTAMP,
  -- The unique PID of the allocating process.
  upid LONG,
  -- Heap name.
  heap_name STRING,
  -- The callsite the allocation happened at.
  callsite_id JOINID(stack_profile_callsite.id),
  -- Number of allocations (positive) or frees (negative).
  count LONG,
  -- Size of allocations (positive) or frees (negative).
  size LONG
) AS
SELECT
  *
FROM __intrinsic_heap_profile_allocation;

-- Vulkan memory allocations.
CREATE PERFETTO VIEW vulkan_memory_allocations (
  -- The id of the row.
  id ID,
  -- Args.
  arg_set_id ARGSETID,
  -- Source.
  source STRING,
  -- Operation.
  operation STRING,
  -- Timestamp.
  timestamp LONG,
  -- Unique process id.
  upid LONG,
  -- Device.
  device LONG,
  -- Device memory.
  device_memory LONG,
  -- Memory type.
  memory_type LONG,
  -- Heap.
  heap LONG,
  -- Function name.
  function_name STRING,
  -- Object handle.
  object_handle LONG,
  -- Memory address.
  memory_address LONG,
  -- Memory size.
  memory_size LONG,
  -- Scope.
  scope STRING
) AS
SELECT
  *
FROM __intrinsic_vulkan_memory_allocations;

-- GPU counter group.
CREATE PERFETTO VIEW gpu_counter_group (
  -- The id of the row.
  id ID,
  -- Group id.
  group_id LONG,
  -- Track id.
  track_id JOINID(track.id)
) AS
SELECT
  *
FROM __intrinsic_gpu_counter_group;

-- Spurious scheduling wakeups.
CREATE PERFETTO VIEW spurious_sched_wakeup (
  -- The id of the row.
  id ID,
  -- The timestamp of the wakeup.
  ts TIMESTAMP,
  -- The id of the row in the thread_state table.
  thread_state_id LONG,
  -- Whether the wakeup was from interrupt context.
  irq_context LONG,
  -- The thread's unique id in the trace.
  utid LONG,
  -- The unique thread id of the waker thread.
  waker_utid LONG
) AS
SELECT
  *
FROM __intrinsic_spurious_sched_wakeup;

-- Contains raw machine_id of trace packets emitted from remote machines.
CREATE PERFETTO VIEW machine (
  -- The id of the machine.
  id ID,
  -- Raw machine identifier in the trace packet.
  raw_id LONG,
  -- The name of the operating system.
  sysname STRING,
  -- The current release of the operating system.
  release STRING,
  -- The current version of the operating system.
  version STRING,
  -- Hardware architecture of the machine.
  arch STRING,
  -- Number of cpus available to the machine.
  num_cpus LONG,
  -- Android build fingerprint.
  android_build_fingerprint STRING,
  -- Android device manufacturer.
  android_device_manufacturer STRING,
  -- Android SDK version.
  android_sdk_version LONG,
  -- Total system RAM in bytes.
  system_ram_bytes LONG,
  -- Total system RAM in gigabytes (rounded).
  system_ram_gb LONG
) AS
SELECT
  *
FROM __intrinsic_machine;

-- Contains information of filedescriptors collected during the trace.
CREATE PERFETTO VIEW filedescriptor (
  -- The id of the row.
  id ID,
  -- Unique fd.
  ufd LONG,
  -- The OS fd.
  fd LONG,
  -- The timestamp for when the fd was collected.
  ts TIMESTAMP,
  -- The upid of the process which opened the filedescriptor.
  upid LONG,
  -- The path to the file or device backing the fd.
  path STRING
) AS
SELECT
  *
FROM __intrinsic_filedescriptor;

-- Experimental table for missing Chrome processes.
CREATE PERFETTO VIEW experimental_missing_chrome_processes (
  -- The id of the row.
  id ID,
  -- Unique process id.
  upid LONG,
  -- Reliable from timestamp.
  reliable_from LONG
) AS
SELECT
  *
FROM __intrinsic_experimental_missing_chrome_processes;

-- Contains all the mapping between clock snapshots and trace time.
CREATE PERFETTO VIEW clock_snapshot (
  -- The id of the row.
  id ID,
  -- Timestamp of the snapshot in trace time.
  ts TIMESTAMP,
  -- Id of the clock.
  clock_id LONG,
  -- The name of the clock for builtin clocks or null otherwise.
  clock_name STRING,
  -- Timestamp of the snapshot in clock time.
  clock_value LONG,
  -- The index of this snapshot.
  snapshot_id LONG,
  -- Machine identifier.
  machine_id JOINID(machine.id)
) AS
SELECT
  *
FROM __intrinsic_clock_snapshot;

-- SurfaceFlinger layers snapshot.
CREATE PERFETTO VIEW surfaceflinger_layers_snapshot (
  -- The id of the row.
  id ID,
  -- Timestamp of the snapshot.
  ts TIMESTAMP,
  -- Extra args parsed from the proto message.
  arg_set_id ARGSETID,
  -- String id for raw proto message.
  base64_proto_id LONG,
  -- Sequence id of the trace packet.
  sequence_id LONG,
  -- Whether snapshot was recorded without elapsed timestamp.
  has_invalid_elapsed_ts LONG
) AS
SELECT
  *
FROM __intrinsic_surfaceflinger_layers_snapshot;

-- SurfaceFlinger layer.
CREATE PERFETTO VIEW surfaceflinger_layer (
  -- The id of the row.
  id ID,
  -- The snapshot that generated this layer.
  snapshot_id JOINID(surfaceflinger_layers_snapshot.id),
  -- Extra args parsed from the proto message.
  arg_set_id ARGSETID,
  -- String id for raw proto message.
  base64_proto_id LONG,
  -- Layer id.
  layer_id LONG,
  -- Layer name.
  layer_name STRING,
  -- Computed layer visibility.
  is_visible LONG,
  -- Parent layer id.
  parent LONG,
  -- Layer corner radius top left.
  corner_radius_tl DOUBLE,
  -- Layer corner radius top right.
  corner_radius_tr DOUBLE,
  -- Layer corner radius bottom left.
  corner_radius_bl DOUBLE,
  -- Layer corner radius bottom right.
  corner_radius_br DOUBLE,
  -- Hwc composition type.
  hwc_composition_type LONG,
  -- Is hidden by policy.
  is_hidden_by_policy LONG,
  -- Z parent.
  z_order_relative_of LONG,
  -- Is Z parent missing.
  is_missing_z_parent LONG,
  -- Layer rect id.
  layer_rect_id LONG,
  -- Input rect id.
  input_rect_id LONG
) AS
SELECT
  *
FROM __intrinsic_surfaceflinger_layer;

-- SurfaceFlinger transactions.
CREATE PERFETTO VIEW surfaceflinger_transactions (
  -- The id of the row.
  id ID,
  -- Timestamp of the transactions commit.
  ts TIMESTAMP,
  -- Extra args parsed from the proto message.
  arg_set_id ARGSETID,
  -- String id for raw proto message.
  base64_proto_id LONG,
  -- Vsync id.
  vsync_id LONG
) AS
SELECT
  *
FROM __intrinsic_surfaceflinger_transactions;

-- Window Manager Shell Transitions.
CREATE PERFETTO VIEW window_manager_shell_transitions (
  -- The id of the row.
  id ID,
  -- The timestamp the transition started playing.
  ts TIMESTAMP,
  -- The id of the transition.
  transition_id LONG,
  -- Extra args parsed from the proto message.
  arg_set_id ARGSETID,
  -- The type of the transition.
  transition_type LONG,
  -- Transition send time.
  send_time_ns LONG,
  -- Transition dispatch time.
  dispatch_time_ns LONG,
  -- Transition duration.
  duration_ns LONG,
  -- Transition finish time.
  finish_time_ns LONG,
  -- Transition shell abort time.
  shell_abort_time_ns LONG,
  -- Transition wm abort time.
  wm_abort_time_ns LONG,
  -- Transition merge time.
  merge_time_ns LONG,
  -- Transition create time.
  create_time_ns LONG,
  -- Handler id.
  handler LONG,
  -- Transition status.
  status STRING,
  -- Transition flags.
  flags LONG,
  -- Start transaction id.
  start_transaction_id LONG,
  -- Finish transaction id.
  finish_transaction_id LONG
) AS
SELECT
  *
FROM __intrinsic_window_manager_shell_transitions;

-- Window Manager Shell Transition Handlers.
CREATE PERFETTO VIEW window_manager_shell_transition_handlers (
  -- The id of the row.
  id ID,
  -- The id of the handler.
  handler_id LONG,
  -- The name of the handler.
  handler_name STRING,
  -- String id for raw proto message.
  base64_proto_id LONG
) AS
SELECT
  *
FROM __intrinsic_window_manager_shell_transition_handlers;

-- Protolog entries.
CREATE PERFETTO VIEW protolog (
  -- The id of the row.
  id ID,
  -- The timestamp the log message was sent.
  ts TIMESTAMP,
  -- The log level of the protolog message.
  level STRING,
  -- The log tag of the protolog message.
  tag STRING,
  -- The protolog message.
  message STRING,
  -- Stacktrace captured at the message's logpoint.
  stacktrace STRING,
  -- The location of the logpoint.
  location STRING
) AS
SELECT
  *
FROM __intrinsic_protolog;

-- Materialized mapping from stat key to severity and name.
CREATE PERFETTO TABLE _stat_key_to_severity_and_name AS
SELECT DISTINCT
  key,
  severity,
  name
FROM stats
ORDER BY
  key;

-- Contains logs of errors and warnings that occurred during trace import.
CREATE PERFETTO VIEW _trace_import_logs (
  -- The id of the log entry.
  id ID,
  -- The id of the trace file this log belongs to.
  trace_id LONG,
  -- The timestamp when the error occurred (if available).
  ts TIMESTAMP,
  -- The byte offset in the trace file where the error occurred (if available).
  byte_offset LONG,
  -- The severity of the log entry ('info', 'data_loss', or 'error').
  severity STRING,
  -- The name of the stat/error type.
  name STRING,
  -- The id of the argument set associated with this log entry.
  arg_set_id ARGSETID
) AS
SELECT
  l.id,
  l.trace_id,
  l.ts,
  l.byte_offset,
  s.severity,
  s.name,
  l.arg_set_id
FROM __intrinsic_trace_import_logs AS l
JOIN _stat_key_to_severity_and_name AS s
  ON l.stat_key = s.key;
