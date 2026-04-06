/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_redaction/trace_redactor.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/scoped_mmap.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_redaction/add_synth_threads_to_process_trees.h"
#include "src/trace_redaction/broadphase_packet_filter.h"
#include "src/trace_redaction/collect_clocks.h"
#include "src/trace_redaction/collect_frame_cookies.h"
#include "src/trace_redaction/collect_system_info.h"
#include "src/trace_redaction/collect_timeline_events.h"
#include "src/trace_redaction/drop_empty_ftrace_events.h"
#include "src/trace_redaction/find_package_uid.h"
#include "src/trace_redaction/merge_threads.h"
#include "src/trace_redaction/populate_allow_lists.h"
#include "src/trace_redaction/prune_package_list.h"
#include "src/trace_redaction/prune_perf_events.h"
#include "src/trace_redaction/redact_ftrace_events.h"
#include "src/trace_redaction/redact_process_events.h"
#include "src/trace_redaction/reduce_threads_in_process_trees.h"
#include "src/trace_redaction/scrub_process_stats.h"
#include "src/trace_redaction/trace_redaction_framework.h"
#include "src/trace_redaction/verify_integrity.h"

#include "protos/perfetto/trace/trace.pbzero.h"

namespace perfetto::trace_redaction {

using Trace = protos::pbzero::Trace;
using TracePacket = protos::pbzero::TracePacket;

TraceRedactor::TraceRedactor() = default;

TraceRedactor::~TraceRedactor() = default;

base::Status TraceRedactor::Redact(std::string_view source_filename,
                                   std::string_view dest_filename,
                                   Context* context) const {
  const std::string source_filename_str(source_filename);
  base::ScopedMmap mapped = base::ReadMmapWholeFile(source_filename_str);
  if (!mapped.IsValid()) {
    return base::ErrStatus("TraceRedactor: failed to map pages for trace (%s)",
                           source_filename_str.c_str());
  }

  trace_processor::TraceBlobView whole_view(
      trace_processor::TraceBlob::FromMmap(std::move(mapped)));

  RETURN_IF_ERROR(Collect(context, whole_view));

  for (const auto& builder : builders_) {
    RETURN_IF_ERROR(builder->Build(context));
  }

  return Transform(*context, whole_view, std::string(dest_filename));
}

base::Status TraceRedactor::Collect(
    Context* context,
    const trace_processor::TraceBlobView& view) const {
  for (const auto& collector : collectors_) {
    RETURN_IF_ERROR(collector->Begin(context));
  }

  const Trace::Decoder trace_decoder(view.data(), view.length());

  for (auto packet_it = trace_decoder.packet(); packet_it; ++packet_it) {
    const TracePacket::Decoder packet(packet_it->as_bytes());

    for (auto& collector : collectors_) {
      RETURN_IF_ERROR(collector->Collect(packet, context));
    }
  }

  for (const auto& collector : collectors_) {
    RETURN_IF_ERROR(collector->End(context));
  }

  return base::OkStatus();
}

base::Status TraceRedactor::Transform(
    const Context& context,
    const trace_processor::TraceBlobView& view,
    const std::string& dest_file) const {
  std::ignore = context;
  const auto dest_fd = base::OpenFile(dest_file, O_RDWR | O_CREAT, 0666);

  if (dest_fd.get() == -1) {
    return base::ErrStatus(
        "Failed to open destination file; can't write redacted trace.");
  }

  const Trace::Decoder trace_decoder(view.data(), view.length());
  for (auto packet_it = trace_decoder.packet(); packet_it; ++packet_it) {
    auto packet = packet_it->as_std_string();

    for (const auto& transformer : transformers_) {
      // If the packet has been cleared, it means a transformation has removed
      // it from the trace. Stop processing it. This saves transforms from
      // having to check and handle empty packets.
      if (packet.empty()) {
        break;
      }

      RETURN_IF_ERROR(transformer->Transform(context, &packet));
    }

    // The packet has been removed from the trace. Don't write an empty packet
    // to disk.
    if (packet.empty()) {
      continue;
    }

    protozero::HeapBuffered<protos::pbzero::Trace> serializer;
    serializer->add_packet()->AppendRawProtoBytes(packet.data(), packet.size());
    packet.assign(serializer.SerializeAsString());

    if (const auto exported_data =
            base::WriteAll(dest_fd.get(), packet.data(), packet.size());
        exported_data <= 0) {
      return base::ErrStatus(
          "TraceRedactor: failed to write redacted trace to disk");
    }
  }

  return base::OkStatus();
}

std::unique_ptr<TraceRedactor> TraceRedactor::CreateInstance(
    const Config& config) {
  auto redactor = std::make_unique<TraceRedactor>();

  // VerifyIntegrity breaks the CollectPrimitive pattern. Instead of writing to
  // the context, its job is to read trace packets and return errors if any
  // packet does not look "correct". This primitive is added first in an effort
  // to detect and react to bad input before other collectors run.
  if (config.verify) {
    redactor->emplace_collect<VerifyIntegrity>();
  }

  // Add all collectors.
  redactor->emplace_collect<FindPackageUid>();
  redactor->emplace_collect<CollectTimelineEvents>();
  redactor->emplace_collect<CollectFrameCookies>();
  redactor->emplace_collect<CollectSystemInfo>();
  redactor->emplace_collect<CollectClocks>();

  // Add all builders.
  redactor->emplace_build<ReduceFrameCookies>();
  redactor->emplace_build<BuildSyntheticThreads>();

  {
    // In order for BroadphasePacketFilter to work, something needs to populate
    // the masks (i.e. PopulateAllowlists).
    redactor->emplace_build<PopulateAllowlists>();
    redactor->emplace_transform<BroadphasePacketFilter>();
  }

  {
    auto* primitive = redactor->emplace_transform<RedactFtraceEvents>();
    primitive->emplace_ftrace_filter<FilterRss>();
    primitive->emplace_post_filter_modifier<DoNothing>();
  }

  {
    auto* primitive = redactor->emplace_transform<RedactFtraceEvents>();
    primitive->emplace_ftrace_filter<FilterFtraceUsingSuspendResume>();
    primitive->emplace_post_filter_modifier<DoNothing>();
  }

  {
    // Remove all frame timeline events that don't belong to the target package.
    redactor->emplace_transform<FilterFrameEvents>();
  }

  redactor->emplace_transform<PrunePackageList>();

  {
    // This primitive has a dependencies on other primitives.
    // The overall flow to make this transform work is as follows:
    //
    // First: CollectClocks retrieves the clock ids to be used for perf samples
    // and sets up the RedactorClockConverter that will handle all the timestamp
    // transformations into trace time which is used by the Timeline.
    //
    // Second: PopulateAllowlists adds the perf samples to be included in the
    // redacted and BroadphasePacketFilter keeps those samples.
    //
    // Third: We emplace the PrunePerfEvents which actually
    // removes the perf samples that don't belong to the target package.
    auto* primitive = redactor->emplace_transform<PrunePerfEvents>();
    primitive->emplace_filter<ConnectedToPackage>();
  }

  // Process stats includes per-process information, such as:
  //
  //   processes {
  //   pid: 1
  //   vm_size_kb: 11716992
  //   vm_rss_kb: 5396
  //   rss_anon_kb: 2896
  //   rss_file_kb: 1728
  //   rss_shmem_kb: 772
  //   vm_swap_kb: 4236
  //   vm_locked_kb: 0
  //   vm_hwm_kb: 6720
  //   oom_score_adj: -1000
  // }
  //
  // Use the ConnectedToPackage primitive to ensure only the target package has
  // stats in the trace.
  {
    auto* primitive = redactor->emplace_transform<ScrubProcessStats>();
    primitive->emplace_filter<ConnectedToPackage>();
  }

  // Redacts all switch and waking events. This should use the same modifier and
  // filter as the process events (see below).
  {
    auto* primitive = redactor->emplace_transform<RedactSchedEvents>();
    primitive->emplace_modifier<ClearComms>();
    primitive->emplace_waking_filter<ConnectedToPackage>();
  }

  // Redacts all new task, rename task, process free events. This should use the
  // same modifier and filter as the schedule events (see above).
  {
    auto* primitive = redactor->emplace_transform<RedactProcessEvents>();
    primitive->emplace_modifier<ClearComms>();
    primitive->emplace_filter<ConnectedToPackage>();
  }

  // Merge Threads (part 1): Remove all waking events that connected to the
  // target package. Change the pids not connected to the target package.
  {
    auto* primitive = redactor->emplace_transform<RedactSchedEvents>();
    primitive->emplace_modifier<MergeThreadsPids>();
    primitive->emplace_waking_filter<ConnectedToPackage>();
  }

  // Merge Threads (part 2): Drop all process events not belonging to the
  // target package. No modification is needed.
  {
    auto* primitive = redactor->emplace_transform<RedactProcessEvents>();
    primitive->emplace_modifier<DoNothing>();
    primitive->emplace_filter<ConnectedToPackage>();
  }

  // Merge Threads (part 3): Replace ftrace event's pid (not the task's pid)
  // for all pids not connected to the target package.
  {
    auto* primitive = redactor->emplace_transform<RedactFtraceEvents>();
    primitive->emplace_post_filter_modifier<MergeThreadsPids>();
    primitive->emplace_ftrace_filter<AllowAll>();
  }

  // Add transforms that will change process trees. The order here matters:
  //
  //  1. Primitives removing processes/threads
  //  2. Primitives adding processes/threads
  //
  // If primitives are not in this order, newly added processes/threads may
  // get removed.
  {
    redactor->emplace_transform<ReduceThreadsInProcessTrees>();
    redactor->emplace_transform<AddSythThreadsToProcessTrees>();
  }

  // Optimizations:
  //
  // This block of transforms should be registered last. They clean-up after the
  // other transforms. The most common function will be to remove empty
  // messages.
  {
    redactor->emplace_transform<DropEmptyFtraceEvents>();
  }

  return redactor;
}

}  // namespace perfetto::trace_redaction
