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

#include "perfetto/trace_processor/read_trace.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/importers/archive/gzip_trace_parser.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/proto/proto_trace_tokenizer.h"
#include "src/trace_processor/read_trace_internal.h"
#include "src/trace_processor/util/gzip_utils.h"
#include "src/trace_processor/util/trace_type.h"

#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {
namespace {

class SerializingProtoTraceReader : public ChunkedTraceReader {
 public:
  explicit SerializingProtoTraceReader(std::vector<uint8_t>* output)
      : output_(output) {}

  base::Status Parse(TraceBlobView blob) override {
    return tokenizer_.Tokenize(std::move(blob), [this](TraceBlobView packet) {
      uint8_t buffer[protozero::proto_utils::kMaxSimpleFieldEncodedSize];

      uint8_t* pos = buffer;
      pos = protozero::proto_utils::WriteVarInt(kTracePacketTag, pos);
      pos = protozero::proto_utils::WriteVarInt(packet.length(), pos);
      output_->insert(output_->end(), buffer, pos);

      output_->insert(output_->end(), packet.data(),
                      packet.data() + packet.length());
      return base::OkStatus();
    });
  }

  base::Status NotifyEndOfFile() override { return base::OkStatus(); }

 private:
  static constexpr uint8_t kTracePacketTag =
      protozero::proto_utils::MakeTagLengthDelimited(
          protos::pbzero::Trace::kPacketFieldNumber);

  ProtoTraceTokenizer tokenizer_;
  std::vector<uint8_t>* output_;
};

}  // namespace

base::Status ReadTrace(
    TraceProcessor* tp,
    const char* filename,
    const std::function<void(uint64_t parsed_size)>& progress_callback,
    bool call_notify_end_of_file) {
  RETURN_IF_ERROR(ReadTraceUnfinalized(tp, filename, progress_callback));
  if (call_notify_end_of_file) {
    return tp->NotifyEndOfFile();
  }
  return base::OkStatus();
}

base::Status DecompressTrace(const uint8_t* data,
                             size_t size,
                             std::vector<uint8_t>* output) {
  TraceType type = GuessTraceType(data, size);
  if (type != TraceType::kGzipTraceType && type != TraceType::kProtoTraceType) {
    return base::ErrStatus(
        "Only GZIP and proto trace types are supported by DecompressTrace");
  }

  if (type == TraceType::kGzipTraceType) {
    std::unique_ptr<ChunkedTraceReader> reader(
        new SerializingProtoTraceReader(output));
    GzipTraceParser parser(std::move(reader));
    RETURN_IF_ERROR(parser.ParseUnowned(data, size));
    return parser.NotifyEndOfFile();
  }

  PERFETTO_CHECK(type == TraceType::kProtoTraceType);

  protos::pbzero::Trace::Decoder decoder(data, size);
  util::GzipDecompressor decompressor;
  if (size > 0 && !decoder.packet()) {
    return base::ErrStatus("Trace does not contain valid packets");
  }
  for (auto it = decoder.packet(); it; ++it) {
    protos::pbzero::TracePacket::Decoder packet(*it);
    if (!packet.has_compressed_packets()) {
      it->SerializeAndAppendTo(output);
      continue;
    }

    // Make sure that to reset the stream between the gzip streams.
    auto bytes = packet.compressed_packets();
    decompressor.Reset();
    using ResultCode = util::GzipDecompressor::ResultCode;
    ResultCode ret = decompressor.FeedAndExtract(
        bytes.data, bytes.size, [&output](const uint8_t* buf, size_t buf_len) {
          output->insert(output->end(), buf, buf + buf_len);
        });
    if (ret == ResultCode::kError || ret == ResultCode::kNeedsMoreInput) {
      return base::ErrStatus("Failed while decompressing stream");
    }
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
