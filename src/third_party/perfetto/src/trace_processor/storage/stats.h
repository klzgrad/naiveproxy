/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_STORAGE_STATS_H_
#define SRC_TRACE_PROCESSOR_STORAGE_STATS_H_

#include <cstddef>

namespace perfetto::trace_processor::stats {

// Compile time list of parsing and processing stats.
// clang-format off
#define PERFETTO_TP_STATS(F)                                                   \
  F(android_aflags_errors,               kSingle,  kError,     kTrace, Scope::kMachineAndTrace,         \
       "Errors occurred during the collection of Android aconfig flags by the "\
       "android.aflags data source. This typically happens if the aflags tool "\
       "fails or its output is malformed."),                                   \
  F(android_br_parse_errors,              kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(android_log_num_failed,               kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(android_log_format_invalid,           kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(android_log_num_skipped,              kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(android_log_num_total,                kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(android_video_size_cap_hit,           kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "android.display.video producer hit max_stream_size_bytes; stream torn " \
      "down. See trace_import_logs for the affected display."),                \
  F(android_video_codec_error,            kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "android.display.video MediaCodec error. See trace_import_logs for the " \
      "affected display."),                                                    \
  F(android_video_display_gone,           kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "android.display.video source display removed mid-session. See "         \
      "trace_import_logs for the affected display."),                          \
  F(android_video_no_encoder,             kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "android.display.video has no encoder for the requested format. See "    \
      "trace_import_logs for the affected display."),                          \
  F(android_video_display_not_found,      kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "android.display.video display not found at session start. See "         \
      "trace_import_logs for the affected display."),                          \
  F(android_video_encoder_setup_failed,   kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "android.display.video encoder setup failed. See trace_import_logs for " \
      "the affected display."),                                                \
  F(android_video_virtual_display_failed, kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "android.display.video createVirtualDisplay failed. See "                \
      "trace_import_logs for the affected display."),                          \
  F(android_video_parse_size_cap_hit,     kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "android.display.video stream exceeded the parse-time size cap; frames " \
      "dropped. See the trace_import_logs table for the affected display."),   \
  F(deobfuscate_location_parse_error,     kSingle,  kError,    kAnalysis, Scope::kGlobal,          ""), \
  F(energy_breakdown_missing_values,      kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(energy_descriptor_invalid,            kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(entity_state_descriptor_invalid,      kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(entity_state_residency_invalid,       kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(entity_state_residency_lookup_failed, kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(energy_uid_breakdown_missing_values,  kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(frame_timeline_event_parser_errors,   kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(frame_timeline_unpaired_end_event,    kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(ftrace_bundle_tokenizer_errors,       kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(ftrace_cpu_bytes_begin,               kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_bytes_end,                 kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_bytes_delta,               kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_commit_overrun_begin,      kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_commit_overrun_end,        kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_commit_overrun_delta,      kIndexed, kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_dropped_events_begin,      kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_dropped_events_end,        kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_dropped_events_delta,      kIndexed, kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_entries_begin,             kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_entries_end,               kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_entries_delta,             kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_now_ts_begin,              kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_now_ts_end,                kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_oldest_event_ts_begin,     kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_oldest_event_ts_end,       kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_overrun_begin,             kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_overrun_end,               kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_overrun_delta,             kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_read_events_begin,         kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_read_events_end,           kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_read_events_delta,         kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(ftrace_cpu_has_data_loss,             kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
       "Ftrace data for the given cpu has data losses and is therefore "       \
       "unreliable. The kernel buffer overwrote events between our reads "     \
       "in userspace. Try re-recording the trace with a bigger buffer "        \
       "(ftrace_config.buffer_size_kb), or with fewer enabled ftrace events."),\
  F(ftrace_kprobe_hits_begin,             kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "The number of kretprobe hits at the beginning of the trace."),         \
  F(ftrace_kprobe_hits_end,               kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "The number of kretprobe hits at the end of the trace."),               \
  F(ftrace_kprobe_hits_delta,             kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "The number of kprobe hits encountered during the collection of the"    \
       "trace."),                                                              \
  F(ftrace_kprobe_misses_begin,           kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "The number of kretprobe missed events at the beginning of the trace."),\
  F(ftrace_kprobe_misses_end,             kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "The number of kretprobe missed events at the end of the trace."),      \
  F(ftrace_kprobe_misses_delta,           kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
       "The number of kretprobe missed events encountered during the "         \
       "collection of the trace. A value greater than zero is due to the "     \
       "maxactive parameter for the kretprobe being too small"),               \
  F(ftrace_setup_errors,                  kSingle,  kNotice,   kTrace, Scope::kMachineAndTrace,         \
       "One or more atrace/ftrace categories were not found or failed to "     \
       "enable. See the trace import logs for the specific categories."),      \
  F(ftrace_abi_errors_skipped_zero_data_length,                                \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(ftrace_generic_descriptor_errors,     kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
       "The config is setting denser_generic_event_encoding, but there are "   \
       "issues with parsing or matching up the in-trace proto descriptors."),  \
  F(ftrace_thermal_exynos_acpm_unknown_tz_id,                                  \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(fuchsia_non_numeric_counters,         kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(fuchsia_timestamp_overflow,           kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(fuchsia_record_read_error,            kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(fuchsia_invalid_event,                kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(fuchsia_invalid_event_arg_type,       kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(fuchsia_invalid_event_arg_name,       kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(fuchsia_unknown_event_arg,            kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(fuchsia_invalid_string_ref,           kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(generic_task_state_invalid_order,     kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
       "Invalid order of generic task state events. Should never happen."),    \
  F(gpu_counters_invalid_spec,            kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(gpu_counters_missing_spec,            kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(gpu_counters_missing_timestamp,       kSingle,  kError,    kTrace, Scope::kMachineAndTrace,           \
      "A GpuCounterEvent packet was received without a timestamp on the "      \
      "containing TracePacket. Without a timestamp the counter samples cannot "\
      "be placed on the trace timeline, so the packet is dropped. This "       \
      "indicates a bug in the trace producer."),                              \
  F(gpu_render_stage_parser_errors,       kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(graphics_frame_event_parser_errors,   kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(guess_trace_type_duration_ns,         kSingle,  kInfo,     kAnalysis, Scope::kGlobal, ""), \
  F(instruments_row_missing_thread,       kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "An instruments trace row referenced an unknown thread id; the "         \
      "row was skipped."),                                                     \
  F(instruments_row_missing_process,      kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "An instruments trace row's thread referenced an unknown "               \
      "process id; the row was skipped."),                                     \
  F(instruments_row_missing_backtrace,    kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "An instruments trace row referenced an unknown backtrace id; "          \
      "the row was skipped."),                                                 \
  F(instruments_row_missing_frame,        kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "An instruments trace backtrace referenced an unknown frame id; "        \
      "the frame was skipped."),                                               \
  F(interned_data_tokenizer_errors,       kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(invalid_clock_snapshots,              kSingle,  kError,    kAnalysis, Scope::kGlobal, ""), \
  F(invalid_cpu_times,                    kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(kernel_wakelock_reused_id,            kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
       "Duplicated interning ID seen. Should never happen."),                  \
  F(kernel_wakelock_unknown_id,           kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
       "Interning ID not found. Should never happen."),                        \
  F(kernel_wakelock_zero_value_reported,  kSingle,  kInfo, kTrace, Scope::kMachineAndTrace,             \
       "Zero value received from SuspendControlService. Indicates a transient "\
       "error in SuspendControlService."),                                     \
  F(kernel_wakelock_non_monotonic_value_reported,                              \
                                          kSingle,  kInfo, kTrace, Scope::kMachineAndTrace,             \
       "Decreased value received from SuspendControlService. Indicates a "     \
       "transient error in SuspendControlService."),                           \
  F(kernel_wakelock_implausibly_large_value_reported,                          \
                                          kSingle,  kInfo, kTrace, Scope::kMachineAndTrace,             \
       "Implausibly large increment to value received from "                   \
       "SuspendControlService. Indicates a transient error in "                \
       "SuspendControlService."),                                              \
  F(kernel_trackevent_format_error,       kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
       "Ftrace event payloads did not match the format file while being "      \
       "parsed as kernel track events."),                                      \
  F(app_wakelock_parse_error,             kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
       "Parsing packed repeated field. Should never happen."),                 \
  F(app_wakelock_unknown_id,              kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
       "Interning ID not found. Should never happen."),                        \
  F(concurrent_session_event_unknown_state,                                    \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
       "ConcurrentSessionEvent packet with an unspecified or unknown "         \
       "state, likely emitted by a newer version of the tracing service. "     \
       "The event was dropped, so the session's state track is missing a "     \
       "transition. Retry with a trace_processor version at least as new as "  \
       "the traced that recorded the trace; the raw state value is in the "    \
       "trace import logs."),                                                  \
  F(meminfo_unknown_keys,                 kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(cpu_info_unknown_cpu_features,        kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
       "CpuInfo contained CPU feature bits not known to this version of "      \
       "trace_processor. The full bitmap is still preserved in the cpu "       \
       "table args as cpu_features.raw_bitmap."),                              \
  F(missing_disk_io_event_name,           kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
       "ETW Disk IO tracker encountered an event with an opcode for which it " \
        "didn't have a name."),                                                \
  F(cpu_info_empty,                       kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
       "CpuInfo packet in the trace contained no CPUs. This usually happens "  \
       "when /proc/cpuinfo is unreadable or empty in the environment (e.g. "   \
       "sandbox or container) where tracing occurred. Metadata in the cpu "    \
       "table will be missing."),                                              \
  F(mismatched_sched_switch_tids,         kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(mm_unknown_type,                      kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(parse_trace_duration_ns,              kSingle,  kInfo,     kAnalysis, Scope::kGlobal, ""), \
  F(power_rail_empty_packet,              kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(power_rail_unknown_index,             kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(proc_stat_unknown_counters,           kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(rss_stat_unknown_keys,                kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(rss_stat_negative_size,               kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(rss_stat_unknown_thread_for_mm_id,    kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(filter_input_bytes,                   kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "Number of bytes pre-TraceFilter. The trace file would have been this " \
       "many bytes big if the TraceConfig didn't specify any TraceFilter. "    \
       "This affects the actual buffer usage, as filtering happens only "      \
       "when writing into the trace file (or over IPC)."),                     \
  F(filter_input_packets,                 kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "Number of packets pre-TraceFilter. The trace file would have had so "  \
       "many packets if the TraceConfig didn't specify any TraceFilter."),     \
  F(filter_output_bytes,                  kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "Number of bytes that made it through the TraceFilter, before the "     \
       "(optional) Zlib compression stage."),                                  \
  F(filter_time_taken_ns,                 kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "Time cumulatively spent running the TraceFilter throughout the "       \
       "tracing session by MaybeFilterPackets()."),                            \
  F(filter_errors,                        kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(flow_duplicate_id,                    kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(flow_no_enclosing_slice,              kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(flow_step_without_start,              kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(flow_end_without_start,               kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(flow_invalid_id,                      kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(flow_without_direction,               kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(stackprofile_empty_callstack,         kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "Callstack had no frames. Ignored"),                                     \
  F(stackprofile_invalid_string_id,       kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(stackprofile_invalid_mapping_id,      kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(stackprofile_invalid_frame_id,        kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(stackprofile_invalid_callstack_id,    kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(stackprofile_parser_error,            kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(smaps_parser_errors,                  kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "Count of malformed PackedSmaps packets. Data in smaps tables unreliable."),                      \
  F(systemd_journal_num_failed,           kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(systemd_journal_num_total,            kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(systrace_parse_failure,               kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(task_state_invalid,                   kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(traced_buf_abi_violations,            kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_buffer_size,               kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_bytes_overwritten,         kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_bytes_read,                kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_bytes_filtered_out,        kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "Number of bytes discarded (input - output) by the TraceFilter for "    \
       "each buffer. It is a subset of, but does not add up perfectly to, "    \
       "(filter_input_bytes - filter_output_bytes) because of the synthetic "  \
       "metadata and stats packets generated by the tracing service itself."), \
  F(traced_buf_bytes_written,             kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_clone_done_timestamp_ns,   kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,         \
    "The timestamp when the clone snapshot operation for this buffer "         \
    "finished"),                                                               \
  F(traced_buf_chunks_discarded,          kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_chunks_overwritten,        kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_chunks_read,               kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_chunks_relocated,          kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,         \
    "TraceBufferV2 only. Num. chunks that we moved to the write cursor "       \
    "instead of rewriting in place. We do this for scraped chunks that have "  \
    "been fully read, as their old position can be too close to the write "    \
    "cursor to be safe."),                                                     \
  F(traced_buf_chunks_rewritten,          kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_chunks_written,            kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_chunks_committed_out_of_order,                                  \
                                          kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_padding_bytes_cleared,     kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_padding_bytes_written,     kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_patches_failed,            kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "The tracing service potentially lost data from one of the data sources "\
      "writing into the given target_buffer. This entry can be ignored "       \
      "if you're using DISCARD buffers and traced_buf_chunks_discarded is "    \
      "nonzero, meaning that the buffer was filled."),                         \
  F(traced_buf_patches_succeeded,         kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_readaheads_failed,         kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_readaheads_succeeded,      kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_buf_trace_writer_packet_loss,  kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "The tracing service observed packet loss for this buffer during this "  \
      "tracing session. This also counts packet loss that happened before "    \
      "the RING_BUFFER start or after the DISCARD buffer end."),               \
  F(traced_buf_sequence_packet_loss,      kIndexed, kDataLoss, kAnalysis, Scope::kMachineAndTrace,      \
      "The number of groups of consecutive packets lost in each sequence for " \
      "this buffer"),                                                          \
  F(traced_buf_incremental_sequences_dropped, kIndexed, kDataLoss, kAnalysis, Scope::kMachineAndTrace,  \
      "For a given buffer, indicates the number of sequences where all the "   \
      "packets on that sequence were dropped due to lack of a valid "          \
      "incremental state (i.e. interned data). This is usually a strong sign " \
      "that either: "                                                          \
      "1) incremental state invalidation is disabled. "                        \
      "2) the incremental state invalidation interval is too low. "            \
      "In either case, see "                                                   \
      "https://perfetto.dev/docs/concepts/buffers"                             \
      "#incremental-state-in-trace-packets"),                                  \
  F(traced_buf_data_loss_read_gap,        kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Sequence data losses TraceBufferV2 attributed to a read-time ChunkID "  \
      "gap, per buffer."),                                                     \
  F(traced_buf_data_loss_chunk_corrupted, kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Sequence data losses TraceBufferV2 attributed to a chunk whose "        \
      "fragments couldn't be tokenized (malformed / out-of-bounds), per "      \
      "buffer."),                                                              \
  F(traced_buf_data_loss_orphan_continuation, kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,     \
      "Sequence data losses TraceBufferV2 attributed to a continuation/end "   \
      "fragment with no preceding begin fragment, per buffer."),               \
  F(traced_buf_data_loss_reassembly_gap,  kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Sequence data losses TraceBufferV2 attributed to a ChunkID gap in the " \
      "middle of a fragmented packet, per buffer."),                           \
  F(traced_buf_data_loss_reassembly_broken_chain, kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace, \
      "Sequence data losses TraceBufferV2 attributed to a broken fragment "    \
      "chain despite contiguous ChunkIDs, per buffer."),                       \
  F(traced_buf_data_loss_overwrite,       kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Sequence data losses TraceBufferV2 attributed to a ring-buffer wrap "   \
      "evicting unread chunks, per buffer."),                                  \
  F(traced_buf_data_loss_writer_abort,    kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Sequence data losses TraceBufferV2 attributed to a trace writer "       \
      "aborting an in-progress fragmented packet, per buffer."),               \
  F(traced_buf_data_loss_smb_full,        kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Sequence data losses the producer attributed to its shared memory "     \
      "buffer being full, per buffer."),                                       \
  F(traced_buf_write_wrap_count,          kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_clone_started_timestamp_ns,    kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
    "The timestamp when the clone snapshot operation for this trace started"), \
  F(traced_clone_trigger_timestamp_ns,    kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
    "The timestamp when trigger for the clone snapshot operation for this "    \
    "trace was received"), \
  F(traced_chunks_discarded,              kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_data_sources_registered,       kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_data_sources_seen,             kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_final_flush_failed,            kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_final_flush_succeeded,         kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_flushes_failed,                kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_flushes_requested,             kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_flushes_succeeded,             kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_patches_discarded,             kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_producers_connected,           kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_producers_seen,                kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_total_buffers,                 kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(traced_tracing_sessions,              kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(track_event_parser_errors,            kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(track_event_dropped_packets_outside_of_range_of_interest,                  \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "The number of TrackEvent packets dropped by trace processor due to "    \
      "being outside of the range of interest. This happens if a trace has a " \
      "TrackEventRangeOfInterest packet, and track event dropping is "         \
      "enabled."),                                                             \
  F(track_event_tokenizer_errors,         kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(track_hierarchy_missing_uuid,         kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Upper bound on the number of events dropped due to track hierarchy "    \
      "validation failures where a parent UUID was not found. This stat is "   \
      "incremented each time trace processor attempts to resolve a track "     \
      "with an invalid hierarchy, which can happen in multiple contexts, "     \
      "making it an upper bound rather than an exact count of dropped events. "\
      "This is a bug in the trace producer."),                                 \
  F(track_event_thread_invalid_end,       kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "The end event for a thread track does not match a track event "         \
      "begin event. This can happen on mixed atrace/track_event traces "       \
      "and is usually caused by data loss or bugs when the events are "        \
      "emitted. The outcome of this is that slices can appear to be closed "   \
      "before they were closed in reality"),                                   \
  F(tokenizer_skipped_packets,            kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(vmstat_unknown_keys,                  kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(psi_unknown_resource,                 kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(vulkan_allocations_invalid_string_id,                                      \
                                          kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(clock_sync_failure,                   kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(clock_sync_failure_unknown_source_clock, kSingle, kError, kAnalysis, Scope::kMachineAndTrace,      \
      "A packet with a timestamp could not be converted to trace time "        \
      "because the source clock ID has never appeared in any ClockSnapshot. "  \
      "This indicates the trace producer emitted packets with a clock_id but " \
      "never provided a ClockSnapshot for that clock. Check that "             \
      "ClockSnapshots are emitted before any packets using that clock_id. "    \
      "For sequence-scoped clocks (64-128), ensure each packet sequence "      \
      "emits its own ClockSnapshot."),                                         \
  F(clock_sync_failure_unknown_target_clock, kSingle, kError, kAnalysis, Scope::kMachineAndTrace,      \
      "A packet timestamp could not be converted because the target trace "    \
      "time clock has never appeared in any ClockSnapshot. This is an "        \
      "internal error that should not occur."),                                \
  F(clock_sync_failure_no_path, kSingle, kError, kAnalysis, Scope::kMachineAndTrace,                   \
      "A packet timestamp could not be converted to trace time because no "    \
      "ClockSnapshot path exists connecting the source clock to the trace "    \
      "time clock. Both clocks exist in snapshots, but never together or "     \
      "via a common intermediate clock. Ensure ClockSnapshots link all used "  \
      "clocks to the trace time clock."),                                      \
  F(clock_sync_unrelatable_clock_domains, kSingle, kError, kAnalysis, Scope::kMachineAndTrace,         \
      "A file's clock domain could not be related to the trace time clock, so "\
      "its events were dropped. The two are different real clock domains (the "\
      "args record the source and trace-time clock ids, e.g. BOOTTIME=6 "      \
      "against a REALTIME=1 trace time) with nothing connecting them, and "    \
      "trace_processor does not assume different real clocks are aligned. "    \
      "Provide a relationship - a ClockSnapshot, a remote_clock_sync, or a "   \
      "perfetto_manifest clock anchor - or attribute the file to a machine "   \
      "that shares a clock with the rest of the trace."),                      \
  F(clock_sync_mixed_clock_sources,         kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "A non-primary trace file used both the primary trace's clock "          \
      "snapshots and its own for timestamp conversion. Timestamps "            \
      "converted before the first own clock snapshot used the primary "        \
      "trace's clocks which may differ."),                                     \
  F(clock_sync_failure_undeferrable_packet_loss, kSingle, kDataLoss,          \
      kAnalysis, Scope::kMachineAndTrace,                                                               \
      "A packet had a timestamp with a clock ID that could not be resolved "   \
      "to trace time and the packet could not be deferred for later "          \
      "resolution (the trace sorter was unable to switch to full-sort mode). " \
      "The packet was dropped and its data will be missing from query "        \
      "results. This can happen when a sequence-scoped clock (64-127) is "    \
      "used before the ClockSnapshot defining it arrives, and the sorter "     \
      "has already started flushing."),                                        \
  F(clock_sync_cache_miss,                kSingle,  kInfo,     kAnalysis, Scope::kGlobal, ""), \
  F(process_tracker_errors,               kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(process_tracker_parent_pid_changed,     kSingle, kInfo,   kAnalysis, Scope::kMachineAndTrace,         \
      "A process was seen with a changed parent pid and was treated as the "   \
      "same process being reparented rather than pid reuse."),                 \
  F(namespaced_thread_missing_process,    kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "A namespaced thread association was received but the corresponding "    \
      "process association was not found. This can happen due to data losses " \
      "during trace collection. The trace will be missing namespace "          \
      "associations for some threads, which may affect analysis. To address "  \
      "this issue, address the underlying data losses."),                      \
  F(json_tokenizer_failure,               kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(json_parser_failure,                  kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(json_display_time_unit,               kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
      "The displayTimeUnit key was set in the JSON trace. In some prior "      \
      "versions of trace processor this key could effect how the trace "       \
      "processor parsed timestamps and durations. In this version the key is " \
      "ignored which more closely matches the bavahiour of catapult."),        \
  /* HeapGraphTracker is owned globally (one instance shared across all       \
     machines/traces), so it can't trivially attribute its writes to a       \
     specific (machine, trace). Until the tracker itself is reworked to be   \
     per-context, the heap_graph_* stats below are kGlobal — heap-graph      \
     errors are aggregated rather than broken out per machine. */            \
  F(heap_graph_invalid_string_id,         kIndexed, kError,    kTrace, Scope::kGlobal,             ""), \
  F(heap_graph_malformed_packet,          kIndexed, kError,    kTrace, Scope::kGlobal,             ""), \
  F(heap_graph_missing_packet,            kIndexed, kError,    kTrace, Scope::kGlobal,             ""), \
  F(heapprofd_buffer_corrupted,           kIndexed, kError,    kTrace, Scope::kMachineAndTrace,         \
      "Shared memory buffer corrupted. This is a bug or memory corruption "    \
      "in the target. Indexed by target upid."),                               \
  F(heapprofd_hit_guardrail,              kIndexed, kError,    kTrace, Scope::kMachineAndTrace,         \
      "HeapprofdConfig specified a CPU or Memory Guardrail that was hit. "     \
      "Indexed by target upid."),                                              \
  F(heapprofd_buffer_overran,             kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "The shared memory buffer between the target and heapprofd overran. "    \
      "The profile was truncated early. Indexed by target upid."),             \
  F(heapprofd_client_error,               kIndexed, kError,    kTrace, Scope::kMachineAndTrace,         \
      "The heapprofd client ran into a problem and disconnected. "             \
      "See profile_packet.proto  for error codes."),                           \
  F(heapprofd_client_disconnected,        kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(heapprofd_malformed_packet,           kIndexed, kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(heapprofd_missing_packet,             kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(heapprofd_rejected_concurrent,        kIndexed, kError,    kTrace, Scope::kMachineAndTrace,         \
      "The target was already profiled by another tracing session, so the "    \
      "profile was not taken. Indexed by target upid."),                       \
  F(heapprofd_non_finalized_profile,      kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(heapprofd_sampling_interval_adjusted,                                      \
    kIndexed, kInfo,    kTrace, Scope::kMachineAndTrace,                                                \
      "By how many byes the interval for PID was increased "                   \
      "by adaptive sampling."),                                                \
  F(heapprofd_unwind_time_us,             kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,         \
      "Time spent unwinding callstacks."),                                     \
  F(heapprofd_unwind_samples,             kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,         \
      "Number of samples unwound."),                                           \
  F(heapprofd_client_spinlock_blocked,    kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "Time (us) the heapprofd client was blocked on the spinlock."),         \
  F(heapprofd_last_profile_timestamp,     kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "The timestamp (in trace time) for the last dump for a process"),       \
  F(symbolization_tmp_build_id_not_found,     kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,  \
       "Number of file mappings in /data/local/tmp without a build id. "       \
       "Symbolization doesn't work for executables in /data/local/tmp "        \
       "because of SELinux. Please use /data/local/tests"),                    \
  F(metatrace_overruns,                   kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(packages_list_has_parse_errors,       kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(packages_list_has_read_errors,        kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(user_list_errors,       kSingle,  kError,    kTrace, Scope::kMachineAndTrace,                       \
        "Errors occurred while reading or parsing the user.list file."),       \
  F(game_intervention_has_parse_errors,   kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
       "One or more parsing errors occurred. This could result from "          \
       "unknown game more or intervention added to the file to be parsed."),   \
  F(game_intervention_has_read_errors,    kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
       "The file to be parsed can't be opened. This can happend when "         \
       "the file name is not found or no permission to access the file"),      \
  F(compact_sched_has_parse_errors,       kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(misplaced_end_event,                  kSingle,  kDataLoss, kAnalysis, Scope::kMachineAndTrace, ""), \
  F(truncated_sys_write_duration,         kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Count of sys_write slices that have a truncated duration to resolve "   \
      "nesting incompatibilities with atrace slices. Real durations "          \
      "can be recovered via the |raw| table."),                                \
  F(compact_sched_switch_skipped,         kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(compact_sched_waking_skipped,         kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(empty_chrome_metadata,                kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(ninja_parse_errors,                   kSingle,  kError,    kTrace, Scope::kMachineAndTrace,    ""), \
  F(perf_cpu_lost_records,                kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Count of perf samples lost due to kernel buffer overruns. The trace "   \
      "is missing information, but it's not known which processes are "        \
      "affected. Consider lowering the sampling frequency or raising "         \
      "the ring_buffer_pages config option."),                                 \
  F(perf_process_shard_count,             kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(perf_chosen_process_shard,            kIndexed, kInfo,     kTrace, Scope::kMachineAndTrace,    ""), \
  F(perf_guardrail_stop_ts,               kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,    ""), \
  F(perf_unknown_record_type,             kIndexed, kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(perf_record_skipped,                  kIndexed, kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(perf_samples_skipped,                 kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "Count of skipped perf samples that otherwise matched the tracing "      \
      "config. This will cause a process to be completely absent from the "    \
      "trace, but does *not* imply data loss for processes that do have "      \
      "samples in this trace."),                                               \
  F(perf_features_skipped,                kIndexed, kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(perf_samples_cpu_mode_unknown,        kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(perf_samples_skipped_dataloss,        kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Count of perf samples lost within the profiler (traced_perf), likely "  \
      "due to load shedding. This may impact any traced processes. The trace " \
      "protobuf needs to be inspected manually to confirm which processes "    \
      "are affected."),                                                        \
  F(perf_dummy_mapping_used,              kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(perf_aux_missing,                     kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Number of bytes missing in AUX data streams due to missing "            \
      "PREF_RECORD_AUX messages."),                                            \
  F(perf_aux_ignored,                     kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
       "AUX data was ignored because the proper parser is not implemented."), \
  F(perf_aux_lost,                        kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Gaps in the AUX data stream pased to the tokenizer."), \
  F(perf_aux_truncated,                   kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Data was truncated when being written to the AUX stream at the "        \
      "source."),\
  F(perf_aux_partial,                     kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "The PERF_RECORD_AUX contained partial data."), \
  F(perf_aux_collision,                   kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "The collection of a sample colliden with another. You should reduce "   \
      "the rate at which samples are collected."),                             \
  F(perf_auxtrace_missing,                kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Number of bytes missing in AUX data streams due to missing "            \
      "PREF_RECORD_AUXTRACE messages."),                                       \
  F(perf_unknown_aux_data,                kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "AUX data type encountered for which there is no known parser."),        \
  F(perf_no_tsc_data,                     kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
      "TSC data unavailable. Will be unable to translate HW clocks."),         \
  F(spe_no_timestamp,                     kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,         \
      "SPE record with no timestamp. Will try our best to assign a "           \
      "timestamp."),                                                           \
  F(etm_no_importer,                      kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "Unable to parse ETM data because TraceProcessor was not compiled to  "  \
      " support it. Make sure you enable the `enable_perfetto_etm_importer` "  \
      " GN flag."),                                                            \
  F(memory_snapshot_parser_failure,       kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(thread_time_in_state_unknown_cpu_freq,                                     \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(ftrace_packet_before_tracing_start,   kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "An ftrace packet was seen before the tracing start timestamp from "     \
      "the tracing service. This happens if the ftrace buffers were not "      \
      "cleared properly. These packets are silently dropped by trace "         \
      "processor."),                                                           \
  F(sorter_push_event_out_of_order,       kSingle, kError,     kTrace, Scope::kGlobal,         \
      "Trace events are out of order event after sorting. This can happen "    \
      "due to many factors including clock sync drift, producers emitting "    \
      "events out of order or a bug in trace processor's logic of sorting."),  \
  F(unknown_extension_fields,             kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "TraceEvent had unknown extension fields, which might result in "        \
      "missing some arguments. You may need a newer version of trace "         \
      "processor to parse them."),                                             \
  F(network_trace_intern_errors,          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(network_trace_parse_errors,           kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace, ""), \
  F(atom_timestamp_missing,               kSingle,  kError,    kTrace, Scope::kMachineAndTrace,         \
      "The corresponding timestamp_nanos entry for a StatsdAtom was "          \
      "missing. Defaulted to inaccurate packet timestamp."),                   \
  F(atom_unknown,                         kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Unknown statsd atom. Atom descriptor may need to be updated"),          \
  F(v8_intern_errors,                                                          \
                                          kSingle,  kDataLoss, kAnalysis, Scope::kMachineAndTrace,      \
      "Failed to resolve V8 interned data."),                                  \
  F(v8_isolate_has_no_code_range,                                              \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "V8 isolate had no code range. THis is currently no supported and means" \
      "we will be unable to parse JS code events for this isolate."),          \
  F(v8_no_defaults,                                                            \
                                          kSingle,  kDataLoss, kAnalysis, Scope::kMachineAndTrace,      \
      "Failed to resolve V8 default data."),                                   \
  F(v8_no_code_range,                                                          \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "V8 isolate had no code range."),                                        \
  F(v8_unknown_code_type,                 kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace, ""), \
  F(v8_code_load_missing_code_range,      kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "V8 load had no code range or an empty one. Event ignored."),            \
  F(winscope_inputmethod_clients_parse_errors,                                 \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "InputMethod clients packet has unknown fields, which results in "       \
      "some arguments missing. You may need a newer version of trace "         \
      "processor to parse them."),                                             \
  F(winscope_inputmethod_manager_service_parse_errors,                         \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "InputMethod manager service packet has unknown fields, which results "  \
      "in some arguments missing. You may need a newer version of trace "      \
      "processor to parse them."),                                             \
  F(winscope_inputmethod_service_parse_errors,                                 \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "InputMethod service packet has unknown fields, which results in "       \
      "some arguments missing. You may need a newer version of trace "         \
      "processor to parse them."),                                             \
  F(winscope_sf_layers_parse_errors,      kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "SurfaceFlinger layers snapshot has unknown fields, which results in "   \
      "some arguments missing. You may need a newer version of trace "         \
      "processor to parse them."),                                             \
  F(winscope_sf_transactions_parse_errors,                                     \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "SurfaceFlinger transactions packet has unknown fields, which results "  \
      "in some arguments missing. You may need a newer version of trace "      \
      "processor to parse them."),                                             \
  F(winscope_shell_transitions_parse_errors,                                   \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "Shell transition packet has unknown fields, which results "             \
      "in some arguments missing. You may need a newer version of trace "      \
      "processor to parse them."),                                             \
  F(winscope_protolog_invalid_interpolation_parse_errors,                      \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "ProtoLog message string has invalid interplation parameter."),          \
  F(winscope_protolog_missing_interned_arg_parse_errors,                       \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Failed to find interned ProtoLog argument."),                           \
  F(winscope_protolog_missing_interned_stacktrace_parse_errors,                \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Failed to find interned ProtoLog stacktrace."),                         \
  F(winscope_protolog_message_decoding_failed,                                 \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Failed to decode ProtoLog message."),                                   \
  F(winscope_protolog_message_collision,                                       \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Got a ProtoLog message collision!"),                                    \
  F(winscope_protolog_message_collision_resolved,                              \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Got a ProtoLog message collision resolved!"),                           \
  F(winscope_protolog_group_tag_collision,                                     \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Got a ProtoLog group tag collision!"),                                  \
  F(winscope_protolog_group_tag_missing,                                       \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Got a ProtoLog group tag missing!"),                                    \
  F(winscope_protolog_param_mismatch,                                          \
                                          kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Message had mismatching parameters!"),                                  \
  F(winscope_viewcapture_parse_errors,                                         \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "ViewCapture packet has unknown fields, which results in some "          \
      "arguments missing. You may need a newer version of trace processor "    \
      "to parse them."),                                                       \
  F(winscope_viewcapture_missing_interned_string_parse_errors,                 \
                                          kSingle,  kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "Failed to find interned ViewCapture string."),                          \
  F(winscope_windowmanager_parse_errors, kSingle,   kError,    kAnalysis, Scope::kMachineAndTrace,      \
      "WindowManager state packet has unknown fields, which results "          \
      "in some arguments missing. You may need a newer version of trace "      \
      "processor to parse them."),                                             \
  F(jit_unknown_frame,                    kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Indicates that we were unable to determine the function for a frame in "\
      "a jitted memory region"),                                               \
  F(ftrace_missing_event_id,              kSingle,  kInfo,    kAnalysis, Scope::kMachineAndTrace,       \
      "Indicates that the ftrace event was dropped because the event id was "  \
      "missing. This is an 'info' stat rather than an error stat because "     \
      "this can be legitimately missing due to proto filtering."),             \
  F(android_input_event_parse_errors,     kSingle,  kInfo,     kAnalysis, Scope::kMachineAndTrace,      \
      "Android input event packet has unknown fields, which results "          \
      "in some arguments missing. You may need a newer version of trace "      \
      "processor to parse them."),                                             \
  F(mali_unknown_mcu_state_id,            kSingle,  kError,   kAnalysis, Scope::kMachineAndTrace,       \
      "An invalid Mali GPU MCU state ID was detected."),                       \
  F(pixel_modem_negative_timestamp,       kSingle,  kError,   kAnalysis, Scope::kMachineAndTrace,       \
      "A negative timestamp was received from a Pixel modem event."),          \
  F(legacy_v8_cpu_profile_invalid_callsite, kSingle, kError,  kTrace, Scope::kMachineAndTrace,          \
      "Indicates a callsite in legacy v8 CPU profiling is invalid. This is "   \
      "a sign that the trace is malformed."),                                  \
  F(legacy_v8_cpu_profile_invalid_sample, kSingle,  kError,  kTrace, Scope::kMachineAndTrace,           \
      "Indicates a sample in legacy v8 CPU profile is invalid. This will "     \
      "cause CPU samples to be missing in the UI. This is a sign that the "    \
      "trace is malformed."),                                                  \
  F(config_write_into_file_no_flush,      kSingle,  kError,  kTrace, Scope::kMachineAndTrace,           \
      "The trace was collected with the `write_into_file` option set but "     \
      "*without* `flush_period_ms` being set. This will cause the trace to "   \
      "be fully loaded into memory and use significantly more memory than "    \
      "necessary."),                                                           \
  F(config_write_into_file_discard,        kIndexed,  kDataLoss,  kTrace, Scope::kMachineAndTrace,      \
      "The trace was collected with the `write_into_file` option set but "     \
      "uses a `DISCARD` buffer. This configuration is strongly discouraged "   \
      "and can cause mysterious data loss in the trace. Please use "           \
      "`RING_BUFFER` buffers instead."),                                       \
  F(long_trace_mode_bytes_overwritten,    kIndexed, kDataLoss, kTrace, Scope::kMachineAndTrace,         \
      "Number of bytes this buffer overwrote while the trace was collected "   \
      "in streaming mode (`write_into_file` with a periodic "                  \
      "`file_write_period_ms`). The overwritten bytes were never written "     \
      "out, so they are missing from the trace file. Consider a larger "       \
      "buffer or a shorter `file_write_period_ms`."),                          \
   F(hprof_string_counter,                 kSingle,  kInfo,   kAnalysis, Scope::kMachineAndTrace,       \
         "Number of strings encountered."),                                    \
   F(hprof_class_counter,                  kSingle,  kInfo,   kAnalysis, Scope::kMachineAndTrace,       \
         "Number of classes encountered."),                                    \
   F(hprof_heap_dump_counter,              kSingle,  kInfo,   kAnalysis, Scope::kMachineAndTrace,       \
         "Number of heap dumps encountered."),                                 \
   F(hprof_instance_counter,               kSingle,  kInfo,   kAnalysis, Scope::kMachineAndTrace,       \
         "Number of instances encountered."),                                  \
   F(hprof_object_array_counter,           kSingle,  kInfo,   kAnalysis, Scope::kMachineAndTrace,       \
         "Number of object arrays encountered."),                              \
  F(hprof_primitive_array_counter,         kSingle,  kInfo,   kAnalysis, Scope::kMachineAndTrace,       \
        "Number of primitive arrays encountered."),                            \
  F(hprof_root_counter,                    kSingle,  kInfo,   kAnalysis, Scope::kMachineAndTrace,       \
        "Number of roots encountered."),                                       \
  F(hprof_reference_counter,               kSingle,  kInfo,   kAnalysis, Scope::kMachineAndTrace,       \
        "Number of references encountered."),                                  \
  F(hprof_record_counter,                  kSingle,  kInfo,   kAnalysis, Scope::kMachineAndTrace,       \
        "Total number of records parsed."),                                    \
  F(hprof_segment_overshoot_counter,       kSingle,  kInfo,   kAnalysis, Scope::kMachineAndTrace,       \
        "Number of HEAP_DUMP_SEGMENTs whose last sub-record extended past "    \
        "the declared segment end. Benign: the sub-record's own length is "    \
        "authoritative and the parser keeps going. Reported for visibility "   \
        "into how a producer framed the dump."),                               \
  F(hprof_field_value_errors,              kSingle,  kError,   kAnalysis, Scope::kMachineAndTrace,      \
      "Number of field value parsing errors. This indicates a malformed "      \
      "hprof file. Check if the hprof opens correctly in a tool like "         \
      "AHAT. Missing values could yield incorrect native object sizes."),      \
  F(hprof_class_errors,                    kSingle,  kError,   kAnalysis, Scope::kMachineAndTrace,      \
      "Number of class parsing errors encountered. This indicates a "          \
      "malformed hprof file. Check if the hprof opens correctly in a tool "    \
      "like AHAT. Missing classes could cause missing references, thus "       \
      "affecting the overall size of the heap graph."),                    \
  F(hprof_header_errors,                   kSingle,  kError,   kAnalysis, Scope::kMachineAndTrace,      \
      "Number of header parsing errors. This indicates a malformed hprof "     \
      "file with invalid or missing header information. The file may be "      \
      "corrupted or might not be a valid hprof file. There may not be any "    \
      "heap graph data parsed."),                                              \
  F(hprof_heap_dump_errors,                kSingle,  kError,   kAnalysis, Scope::kMachineAndTrace,      \
      "Number of heap dump parsing errors. This indicates a malformed "        \
      "hprof file with corrupted heap segments. Check if the hprof opens "     \
      "correctly in a tool like AHAT. Missing heap dump sections can lead to " \
      "huge clusters of the heap graph missing, thus affecting the overall "   \
      "size of the graph"),                                                    \
  F(hprof_primitive_array_parsing_errors,  kSingle,  kError,   kAnalysis, Scope::kMachineAndTrace,      \
      "Number of primitive array parsing errors. This indicates a "            \
      "malformed hprof file. Check if the hprof opens correctly in a tool "    \
      "like AHAT. Primitive arrays like bytes[] missing can dramatically "     \
      "affect the overall size of the heap graph."),                           \
  F(hprof_reference_errors,                kSingle,  kError,   kAnalysis, Scope::kMachineAndTrace,      \
      "Number of object reference errors encountered. This indicates a "       \
      "malformed hprof file. Check if the hprof opens correctly in a tool "    \
      "like AHAT. Missing references will affect the overall size of the "     \
      "heap graph."),                                                          \
  F(trace_sorter_negative_timestamp_dropped,       kSingle,  kError,   kTrace, Scope::kGlobal, \
      "A negative timestamp was received by the TraceSorter and was dropped. " \
      "Negative timestamps are not supported by trace processor and "          \
      "the presence of one is usually a sign that something went wrong while " \
      "recording a trace. Common causes of this include incorrect "            \
      "incremental timestamps, bad clock synchronization or kernel bugs in "   \
      "drivers emitting timestamps. When merging multiple traces, a clock "    \
      "offset which moves events before the start of the trace-time clock "    \
      "also causes this."),                                                    \
  F(slice_drop_overlapping_complete_event,        kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,   \
      "A complete slice was dropped because it partially overlaps another "    \
      "slice on the same track. Overlapping duration events are out of spec "  \
      "and ambiguous: nothing in the trace says how they should nest, so the " \
      "slice cannot be placed and is discarded. If you are hitting this, "     \
      "please file a bug against the tool that produced the trace asking it "  \
      "not to emit overlapping events, and also file a bug at "                \
      "https://github.com/google/perfetto/issues with a trace that "           \
      "reproduces it so we can help"),                                         \
  F(slice_spill_overlapping_complete_event,       kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,   \
      "A complete slice (typically a JSON 'X' event) partially overlaps "      \
      "another slice on the same thread track, so it cannot nest there. "      \
      "Instead of dropping it, Perfetto moved it onto a separate overflow "    \
      "track that is merged back onto the thread at display time. No data is " \
      "lost, but because overlapping duration events are inherently "          \
      "ambiguous (nothing in the trace says how they should nest), the "       \
      "resulting layout might not match what you intended when you emitted "   \
      "these events. See https://github.com/google/perfetto/issues/4280 "      \
      "and, specifically, https://github.com/google/perfetto/issues/4280"      \
      "#issuecomment-4742119340 for a detailed explanation and how to fix "    \
      "it"),                                                                   \
  F(perf_text_importer_sample_no_frames,        kSingle,  kError,  kTrace, Scope::kMachineAndTrace,     \
      "A perf sample was encountered that has no frames. This can happen "     \
      "if the kernel is unable to unwind the stack while sampling. Check "     \
      "Linux kernel documentation for causes of this and potential fixes."),   \
  F(strace_parse_failure,                       kSingle,  kInfo,     kTrace, Scope::kMachineAndTrace,   \
      "A line in an strace trace could not be parsed as a syscall event "     \
      "and was skipped. This is expected for non-syscall lines, such as "     \
      "signal delivery or process exit banners."),                           \
  F(strace_unsupported_timestamp_format,        kSingle,  kError,    kTrace, Scope::kMachineAndTrace,   \
      "A line in an strace trace looked like a syscall event, but used "      \
      "`strace -t`/`-tt` wall-clock time-of-day timestamps rather than "      \
      "`-ttt` Unix epoch timestamps, and was skipped: `-t`/`-tt` print no "   \
      "date, so they cannot be safely treated as an absolute point in "       \
      "time. Re-run strace with `-ttt` to fix this."),                       \
  F(strace_missing_pid,                         kSingle,  kError,    kTrace, Scope::kMachineAndTrace,   \
      "A syscall line in an strace trace had no leading pid and was "         \
      "skipped: without one there is no way to attribute the event to a "     \
      "thread that stays meaningful when traces are merged. strace only "     \
      "prints pids when following processes, so re-run strace with `-f` "     \
      "to fix this."),                                                       \
  F(simpleperf_missing_file_mapping,            kSingle,  kDataLoss, kTrace, Scope::kMachineAndTrace,   \
      "One or more simpleperf samples were dropped because their callchain "   \
      "entries referenced a file_id that has no corresponding File record in " \
      "the simpleperf proto. This typically happens when the simpleperf data " \
      "is incomplete or truncated, or due to a bug in simpleperf. Try "        \
      "re-recording the profile and ensure the file is not truncated. If "     \
      "this occurs consistently, please report it to the simpleperf team."),   \
  F(slice_negative_duration,                    kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "Number of slices dropped due to negative duration. This usually "       \
      "indicates incorrect timestamps in the trace data."),                    \
  F(slice_max_depth_exceeded,                   kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A slice was dropped because the slice nesting depth exceeded the max "  \
      "supported limit."),                                                     \
  F(gpu_work_period_negative_duration,          kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "Number of GPU work period events with negative duration (end < start). "\
      "Check the GPU driver for timestamp bugs."),                             \
  F(track_descriptor_missing_uuid,              kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackDescriptor packet was received without a uuid field. The uuid "  \
      "is required to uniquely identify tracks and associate events with "     \
      "them. Without it, the track cannot be created and any events "          \
      "referencing this track will be dropped. This indicates a bug in the "   \
      "trace producer or trace conversion tool. Ensure your trace producer "   \
      "or conversion tool assigns a unique uuid to each TrackDescriptor."),    \
  F(track_descriptor_thread_missing_pid_tid,    kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A ThreadDescriptor within a TrackDescriptor packet is missing "         \
      "required pid or tid fields. Both fields are necessary to associate "    \
      "the track with a specific thread. The track will be ignored and any "   \
      "events on it will be dropped. This indicates a bug in the trace "       \
      "producer or trace conversion tool. Ensure ThreadDescriptor packets "    \
      "include both pid and tid fields."),                                     \
  F(track_descriptor_process_missing_pid,       kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A ProcessDescriptor within a TrackDescriptor packet is missing the "    \
      "required pid field. The pid is necessary to associate the track with "  \
      "a specific process. The track will be ignored and any events on it "    \
      "will be dropped. This indicates a bug in the trace producer or trace "  \
      "conversion tool. Ensure ProcessDescriptor packets include a valid "     \
      "pid field."),                                                           \
  F(track_event_missing_sequence_id,            kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackEvent packet was received without a trusted_packet_sequence_id " \
      "field. This field is required for tracking incremental state (interned "\
      "data like strings, track associations, etc.). Without it, the event "   \
      "cannot be properly decoded and will be dropped. This indicates a bug "  \
      "in the trace producer or trace conversion tool. Ensure each packet "    \
      "sequence has a unique trusted_packet_sequence_id."),                    \
  F(track_event_missing_timestamp,              kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackEvent packet was received without a valid timestamp. Events "    \
      "must have either a timestamp_delta_us, timestamp_absolute_us, or the "  \
      "containing TracePacket must have a timestamp field. Without a "         \
      "timestamp, the event cannot be placed in the trace timeline and will "  \
      "be dropped. This indicates a bug in the trace producer or trace "       \
      "conversion tool, or data corruption. Ensure all events have valid "     \
      "timestamps."),                                                          \
  F(track_event_invalid_timestamp,              kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackEvent packet resolved to a negative timestamp and was dropped. " \
      "This indicates a bug in the trace producer or trace conversion tool, " \
      "or data corruption."),                                                  \
  F(streaming_profile_invalid_timestamp,        kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A StreamingProfilePacket resolved to a negative timestamp and was "    \
      "dropped. This indicates a bug in the trace producer or data "           \
      "corruption."),                                                          \
  F(thread_descriptor_missing_sequence_id,      kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A ThreadDescriptor packet was received without a "                      \
      "trusted_packet_sequence_id field. This field is required to associate " \
      "the thread descriptor with the correct packet sequence for incremental "\
      "state tracking. Without it, delta-encoded timestamps and other "        \
      "incremental data cannot be properly decoded. The descriptor will be "   \
      "ignored. This indicates a bug in the trace producer or trace "          \
      "conversion tool. Ensure ThreadDescriptor packets include "              \
      "trusted_packet_sequence_id on all packets."),                           \
  F(incremental_state_cleared_missing_sequence_id, kSingle, kError, kAnalysis, Scope::kMachineAndTrace, \
      "A packet with incremental_state_cleared set was received without "      \
      "trusted_packet_sequence_id. This field is required to identify which "  \
      "packet sequence's incremental state should be cleared. Without it, "    \
      "incremental state cannot be properly managed and subsequent packets "   \
      "may fail to decode. This indicates a bug in the trace producer or "     \
      "trace conversion tool."),                                               \
  F(previous_packet_dropped_missing_sequence_id, kSingle, kError, kAnalysis, Scope::kMachineAndTrace,   \
      "A packet with previous_packet_dropped set was received without "        \
      "trusted_packet_sequence_id. This field is required to identify which "  \
      "packet sequence experienced packet loss. Without it, packet loss "      \
      "cannot be properly tracked and incremental state may become "           \
      "corrupted. This indicates a bug in the trace producer or trace "        \
      "conversion tool."),                                                     \
  F(trace_packet_defaults_missing_sequence_id, kSingle, kError, kAnalysis, Scope::kMachineAndTrace,     \
      "A TracePacketDefaults packet was received without "                     \
      "trusted_packet_sequence_id. This field is required to associate the "   \
      "defaults with the correct packet sequence. Without it, the defaults "   \
      "cannot be applied and subsequent packets may not decode correctly. "    \
      "This indicates a bug in the trace producer or trace conversion tool."), \
  F(interned_data_missing_sequence_id, kSingle, kError, kAnalysis, Scope::kMachineAndTrace,             \
      "An InternedData packet was received without "                           \
      "trusted_packet_sequence_id. This field is required to associate "       \
      "interned data (strings, tracks, etc.) with the correct packet "         \
      "sequence. Without it, the interned data cannot be used and subsequent " \
      "packets will fail to decode. This indicates a bug in the trace "        \
      "producer or trace conversion tool."),                                   \
  F(track_event_track_hierarchy_loop,           kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A track hierarchy loop was detected where a track is its own ancestor " \
      "through parent_uuid references. This creates a circular dependency "    \
      "that cannot be resolved. The track will not be properly associated "    \
      "with its ancestors. This indicates corrupted or malformed track "       \
      "descriptors in the trace. Check that parent_uuid references form a "    \
      "valid tree structure."),                                                \
  F(track_event_track_hierarchy_too_deep,       kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A track hierarchy exceeds the maximum depth of 100 ancestors. This "    \
      "either indicates an extremely deep (and likely incorrect) track "       \
      "structure, or a loop that wasn't detected. The track will not be "      \
      "properly resolved. This indicates corrupted or malformed track "        \
      "descriptors. Review the track parent_uuid relationships."),             \
  F(track_descriptor_default_track_with_parent, kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackDescriptor for the default track (uuid=0) specified a "          \
      "parent_uuid. The default track is the root of the track hierarchy and " \
      "cannot have a parent. The descriptor is ignored. This is a bug in the " \
      "trace producer."),                                                      \
  F(track_descriptor_conflicting_reservation,   kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackDescriptor was received that conflicts with an earlier "         \
      "descriptor for the same uuid. Track descriptors for the same uuid "     \
      "must be consistent (same type, same parent, etc.). The conflicting "    \
      "descriptor is ignored. This is a bug in the trace producer."),          \
  F(track_event_range_of_interest_missing_start_us, kSingle, kError, kAnalysis, Scope::kMachineAndTrace,\
      "A TrackEventRangeOfInterest packet was received without the required "  \
      "start_us field. The packet is ignored. This is a bug in the trace "     \
      "producer."),                                                            \
  F(track_descriptor_invalid_child_ordering,    kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackDescriptor specified an invalid or unrecognized value for "      \
      "child_tracks_ordering. The descriptor is ignored. This is a bug in "    \
      "the trace producer."),                                                  \
  F(track_event_counter_missing_track_uuid,     kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackEvent with TYPE_COUNTER was received without a track_uuid "      \
      "(neither in the event nor in TrackEventDefaults). Counter events "      \
      "require a track_uuid to identify which counter track to use. The "      \
      "event is dropped. This is a bug in the trace producer."),               \
  F(track_event_counter_missing_value,          kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackEvent with TYPE_COUNTER was received without a counter value "   \
      "(neither counter_value nor double_counter_value). Counter events "      \
      "must specify a value. The event is dropped. This is a bug in the "      \
      "trace producer."),                                                      \
  F(track_event_counter_invalid_track_uuid,     kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackEvent with TYPE_COUNTER specified a track_uuid that could not "  \
      "be resolved or converted to an absolute counter value. The event is "   \
      "dropped. This is a bug in the trace producer."),                        \
  F(track_event_extra_counter_missing_track_uuids, kSingle, kError, kAnalysis, Scope::kMachineAndTrace, \
      "A TrackEvent specified extra counter values but did not provide the "   \
      "corresponding extra_counter_track_uuids (neither in the event nor in "  \
      "TrackEventDefaults). The event is dropped. This is a bug in the trace " \
      "producer."),                                                            \
  F(track_event_state_missing_track_uuid,     kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace, \
      "A TrackEvent with TYPE_STATE was received without a track_uuid. "       \
      "State events require a track_uuid to identify which state track to "     \
      "use. The event is dropped. This is a bug in the trace producer."),       \
  F(track_event_extra_counter_track_uuid_mismatch, kSingle, kError, kAnalysis, Scope::kMachineAndTrace, \
      "A TrackEvent provided more extra counter values than "                  \
      "extra_counter_track_uuids. Arrays must have matching lengths. The "     \
      "event is dropped. This is a bug in the trace producer."),               \
  F(track_event_extra_counter_exceeds_max,      kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackEvent provided more extra counter values than the maximum "      \
      "supported (TrackEventData::kMaxNumExtraCounters). Excess counters are " \
      "ignored. This is a bug in the trace producer."),                        \
  F(track_event_extra_counter_invalid_track,    kSingle,  kError,  kAnalysis, Scope::kMachineAndTrace,  \
      "A TrackEvent specified an invalid track_uuid in "                       \
      "extra_counter_track_uuids that could not be resolved. The event is "    \
      "dropped. This is a bug in the trace producer."),                        \
  F(thread_descriptor_skipped_incremental_state_invalid, kSingle, kNotice,     \
      kAnalysis, Scope::kMachineAndTrace,                                                               \
      "A ThreadDescriptor packet was skipped because incremental state is "    \
      "invalid. ThreadDescriptors are ignored until incremental state is "     \
      "cleared to prevent incorrect delta-encoded timestamp calculations. "    \
      "Root cause: packet loss in the trace (check packet loss stats) or a "   \
      "bug in the trace producer (missing incremental_state_cleared)."),       \
  F(track_event_skipped_timestamp_delta_without_valid_state, kSingle, kNotice, \
      kAnalysis, Scope::kMachineAndTrace,                                                               \
      "A TrackEvent with timestamp_delta_us was skipped because no valid "     \
      "ThreadDescriptor baseline was available. Delta timestamps require a "   \
      "ThreadDescriptor to establish a baseline. Root cause: packet loss in "  \
      "the trace (check packet loss stats) or a bug in the trace producer "    \
      "(missing ThreadDescriptor)."),                                          \
  F(track_event_skipped_thread_time_delta_without_valid_state, kSingle,        \
      kNotice, kAnalysis, Scope::kMachineAndTrace,                                                      \
      "A TrackEvent with thread_time_delta_us was skipped because no valid "   \
      "ThreadDescriptor baseline was available. Delta timestamps require a "   \
      "ThreadDescriptor to establish a baseline. Root cause: packet loss in "  \
      "the trace (check packet loss stats) or a bug in the trace producer "    \
      "(missing ThreadDescriptor)."),                                          \
  F(track_event_skipped_thread_instruction_delta_without_valid_state, kSingle, \
      kNotice, kAnalysis, Scope::kMachineAndTrace,                                                      \
      "A TrackEvent with thread_instruction_count_delta was skipped because "  \
      "no valid ThreadDescriptor baseline was available. Delta counts "        \
      "require a ThreadDescriptor to establish a baseline. Root cause: "       \
      "packet loss in the trace (check packet loss stats) or a bug in the "    \
      "trace producer (missing ThreadDescriptor)."),                           \
  F(packet_skipped_seq_needs_incremental_state_invalid, kSingle, kNotice,      \
      kAnalysis, Scope::kMachineAndTrace,                                                               \
      "A packet with SEQ_NEEDS_INCREMENTAL_STATE flag was skipped because "    \
      "incremental state is invalid. Packets that depend on incremental "      \
      "state cannot be processed until state is reestablished. Root cause: "   \
      "packet loss in the trace (check packet loss stats) or a bug in the "    \
      "trace producer (missing incremental_state_cleared packet)."),           \
  F(interned_data_skipped_incremental_state_invalid, kSingle, kNotice,         \
      kAnalysis, Scope::kMachineAndTrace,                                                               \
      "An InternedData packet was skipped because incremental state is "       \
      "invalid. InternedData must be associated with the correct state "       \
      "generation, so it's ignored until incremental state is cleared. Root "  \
      "cause: packet loss in the trace (check packet loss stats) or a bug "    \
      "in the trace producer (missing incremental_state_cleared packet)."),    \
  F(heap_graph_non_finalized_graph,             kSingle,  kDataLoss, kTrace, Scope::kGlobal,            \
      "Heap graph profile was not finalized before the trace ended. Packet "   \
      "loss was detected (missing packet indices) which means the profile is " \
      "incomplete. Some objects and references may be missing from the heap "  \
      "graph. This typically occurs when the profiled process crashes or is "  \
      "killed before completing the profile. To get complete profiles, ensure "\
      "the process finishes cleanly or increase the profiling timeout."),      \
  F(primes_unknown_edge_type, kSingle, kDataLoss, kAnalysis, Scope::kMachineAndTrace,                   \
    "A Primes TraceEdge was received which did not contain a known slice type "\
    "(Begin, End, Mark)."),                                                    \
  F(primes_executor_not_found, kSingle, kDataLoss, kAnalysis, Scope::kMachineAndTrace,                  \
    "A valid executor_id was not found for the given edge's parent_id, so the "\
    "slice was dropped."),                                                     \
  F(primes_end_without_matching_begin, kSingle, kInfo, kAnalysis, Scope::kMachineAndTrace,              \
    "A SliceEnd event was seen without its corresponding SliceBegin."),        \
  F(primes_missing_entity_details, kSingle, kInfo, kAnalysis, Scope::kMachineAndTrace,                  \
    "The entity_details field was missing from an edge that requires it."),    \
  F(primes_missing_parent_id, kSingle, kInfo, kAnalysis, Scope::kMachineAndTrace,                       \
    "The parent_id field was missing from an edge that requires it."),         \
  F(primes_malformed_timestamp, kSingle, kDataLoss, kAnalysis, Scope::kMachineAndTrace,                 \
    "The timestamp for an edge or trace was not able to be parsed"),           \
  F(protovm_abort, kSingle,  kError, kAnalysis, Scope::kMachineAndTrace,                                \
      "A ProtoVM instance aborted the execution while applying the patch. "    \
      "This might be due to inconsistencies between VM program logic and "     \
      "actual patch format."),                                                 \
  F(protovm_registration_error, kSingle,  kError, kAnalysis, Scope::kMachineAndTrace,                   \
    "Failed to find the sequence IDs corresponding to a ProtoVM's producer "   \
    "ID. Such mapping should be provided by the TraceProvenance packet."),     \
  F(extra_parsing_descriptors_error, kSingle,  kError, kAnalysis, Scope::kGlobal,                       \
    "Failed to parse a FileDescriptorSet passed as TraceProcessor "            \
    "configuration parameter")
// clang-format on

enum Type {
  kSingle,  // Single-value property, one value per key.
  kIndexed  // Indexed property, multiple value per key (e.g. cpu_stats[1]).
};

enum Severity {
  kInfo,      // Diagnostic counters
  kNotice,    // Normal but noteworthy condition worth surfacing to the user,
              // e.g. requested ftrace categories that failed to enable. Not a
              // bug and not data loss.
  kDataLoss,  // Correct operation that still resulted in data loss
  kError,     // If any kError counter is > 0 trace_processor_shell will
              // raise an error. This is also surfaced in the web UI.
  kNumSeverities,
};

enum Source {
  // The counter is collected when recording the trace on-device and is just
  // being reflected in the stats table.
  kTrace,

  // The counter is generated when importing / processing the trace in the trace
  // processor.
  kAnalysis,

  kNumSources,
};

// Scope of a stat defines its association within a potentially
// multi-machine/multi-trace merged session.
//
// Classification Reasoning:
// - kGlobal: Properties intrinsic to the TP process (e.g. parse timing).
// - kTrace: Properties of a recording session or trace file, not tied to a
//   specific machine (e.g. traced service stats, buffer stats).
// - kMachine: Static properties of a device. Currently unused for stats.
// - kMachineAndTrace: Per-machine execution data within a specific session
//   (e.g. ftrace stats, parser errors, clock sync).
#define PERFETTO_TP_STATS_SCOPES(F)                                 \
  F(kGlobal, "global"), F(kTrace, "trace"), F(kMachine, "machine"), \
      F(kMachineAndTrace, "machine_and_trace")

#define PERFETTO_TP_STATS_SCOPE_ENUM(varname, name) varname
enum class Scope : size_t {
  PERFETTO_TP_STATS_SCOPES(PERFETTO_TP_STATS_SCOPE_ENUM),
  kNumScopes,
};

#define PERFETTO_TP_STATS_SCOPE_NAME(varname, name) name
constexpr char const* kScopeNames[] = {
    PERFETTO_TP_STATS_SCOPES(PERFETTO_TP_STATS_SCOPE_NAME)};

#if defined(__GNUC__) || defined(__clang__)
#if defined(__clang__)
#pragma clang diagnostic push
// Fix 'error: #pragma system_header ignored in main file' for clang in Google3.
#pragma clang diagnostic ignored "-Wpragma-system-header-outside-header"
#endif

// Ignore GCC warning about a missing argument for a variadic macro parameter.
#pragma GCC system_header

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

// Declares an enum of literals (one for each stat). The enum values of each
// literal corresponds to the string index in the arrays below.
#define PERFETTO_TP_STATS_ENUM(name, ...) name
enum KeyIDs : size_t { PERFETTO_TP_STATS(PERFETTO_TP_STATS_ENUM), kNumKeys };

// The code below declares an array for each property (name, type, ...).

#define PERFETTO_TP_STATS_NAME(name, ...) #name
constexpr char const* kNames[] = {PERFETTO_TP_STATS(PERFETTO_TP_STATS_NAME)};

#define PERFETTO_TP_STATS_TYPE(_, type, ...) type
constexpr Type kTypes[] = {PERFETTO_TP_STATS(PERFETTO_TP_STATS_TYPE)};

#define PERFETTO_TP_STATS_SEVERITY(_, __, severity, ...) severity
constexpr Severity kSeverities[] = {
    PERFETTO_TP_STATS(PERFETTO_TP_STATS_SEVERITY)};

#define PERFETTO_TP_STATS_SOURCE(_, __, ___, source, ...) source
constexpr Source kSources[] = {PERFETTO_TP_STATS(PERFETTO_TP_STATS_SOURCE)};

#define PERFETTO_TP_STATS_SCOPE(_, __, ___, ____, scope, ...) scope
constexpr Scope kScopes[] = {PERFETTO_TP_STATS(PERFETTO_TP_STATS_SCOPE)};

#define PERFETTO_TP_STATS_DESCRIPTION(_, __, ___, ____, _____, descr, ...) descr
constexpr char const* kDescriptions[] = {
    PERFETTO_TP_STATS(PERFETTO_TP_STATS_DESCRIPTION)};

}  // namespace perfetto::trace_processor::stats

#endif  // SRC_TRACE_PROCESSOR_STORAGE_STATS_H_
