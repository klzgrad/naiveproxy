/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "perfetto/tracing/internal/interceptor_trace_writer.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/message_handle.h"
#include "perfetto/tracing/interceptor.h"
#include "perfetto/tracing/internal/data_source_internal.h"

namespace perfetto {
namespace internal {

// static
std::atomic<uint32_t> InterceptorTraceWriter::next_sequence_id_{};

InterceptorTraceWriter::InterceptorTraceWriter(
    std::unique_ptr<InterceptorBase::ThreadLocalState> tls,
    InterceptorBase::TracePacketCallback packet_callback,
    DataSourceStaticState* static_state,
    uint32_t instance_index)
    : tls_(std::move(tls)),
      packet_callback_(std::move(packet_callback)),
      static_state_(static_state),
      instance_index_(instance_index),
      sequence_id_(++next_sequence_id_) {}

InterceptorTraceWriter::~InterceptorTraceWriter() = default;

protozero::MessageHandle<protos::pbzero::TracePacket>
InterceptorTraceWriter::NewTracePacket() {
  Flush();
  auto packet = TraceWriter::TracePacketHandle(cur_packet_.get());
  packet->set_trusted_packet_sequence_id(sequence_id_);
  return packet;
}

void InterceptorTraceWriter::Flush(std::function<void()> callback) {
  if (!cur_packet_.empty()) {
    InterceptorBase::TracePacketCallbackArgs args{};
    args.static_state = static_state_;
    args.instance_index = instance_index_;
    args.tls = tls_.get();

    const auto& slices = cur_packet_.GetSlices();
    if (slices.size() == 1) {
      // Fast path: the current packet fits into a single slice.
      auto slice_range = slices.begin()->GetUsedRange();
      args.packet_data = protozero::ConstBytes{
          slice_range.begin,
          static_cast<size_t>(slice_range.end - slice_range.begin)};
      bytes_written_ += static_cast<uint64_t>(args.packet_data.size);
      packet_callback_(std::move(args));
    } else {
      // Fallback: stitch together multiple slices.
      auto stitched_data = cur_packet_.SerializeAsArray();
      args.packet_data =
          protozero::ConstBytes{stitched_data.data(), stitched_data.size()};
      bytes_written_ += static_cast<uint64_t>(stitched_data.size());
      packet_callback_(std::move(args));
    }
    cur_packet_.Reset();
  }
  if (callback)
    callback();
}

void InterceptorTraceWriter::FinishTracePacket() {}

uint64_t InterceptorTraceWriter::written() const {
  return bytes_written_;
}

uint64_t InterceptorTraceWriter::drop_count() const {
  return 0;
}

}  // namespace internal
}  // namespace perfetto
