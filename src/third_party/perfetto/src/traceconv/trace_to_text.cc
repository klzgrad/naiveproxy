/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/traceconv/trace_to_text.h"

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/protozero/proto_ring_buffer.h"
#include "src/traceconv/trace.descriptor.h"
#include "src/traceconv/utils.h"
#include "src/traceconv/winscope.descriptor.h"

#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/gzip_utils.h"
#include "src/trace_processor/util/protozero_to_text.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto {
namespace trace_to_text {
namespace {

using perfetto::trace_processor::DescriptorPool;
using trace_processor::TraceType;
using trace_processor::util::GzipDecompressor;

template <size_t N>
static void WriteToOutput(std::ostream* output, const char (&str)[N]) {
  output->write(str, sizeof(str) - 1);
}

// Online algorithm to covert trace binary to text format.
// Usage:
//  - Feed the trace-binary in a sequence of memblock, and it will continue to
//    write the output in given std::ostream*.
class OnlineTraceToText {
 public:
  OnlineTraceToText(std::ostream* output) : output_(output) {
    pool_.AddFromFileDescriptorSet(kTraceDescriptor.data(),
                                   kTraceDescriptor.size());
    pool_.AddFromFileDescriptorSet(kWinscopeDescriptor.data(),
                                   kWinscopeDescriptor.size());
  }
  OnlineTraceToText(const OnlineTraceToText&) = delete;
  OnlineTraceToText& operator=(const OnlineTraceToText&) = delete;
  void Feed(const uint8_t* data, size_t len);
  bool ok() const { return ok_; }

 private:
  std::string TracePacketToText(protozero::ConstBytes packet,
                                uint32_t indent_depth);
  void PrintCompressedPackets(protozero::ConstBytes packets);

  bool ok_ = true;
  std::ostream* output_;
  protozero::ProtoRingBuffer ring_buffer_;
  DescriptorPool pool_;
  size_t bytes_processed_ = 0;
  size_t packet_ = 0;
};

std::string OnlineTraceToText::TracePacketToText(protozero::ConstBytes packet,
                                                 uint32_t indent_depth) {
  namespace pb0_to_text = trace_processor::protozero_to_text;
  return pb0_to_text::ProtozeroToText(pool_, ".perfetto.protos.TracePacket",
                                      packet, pb0_to_text::kIncludeNewLines,
                                      indent_depth);
}

void OnlineTraceToText::PrintCompressedPackets(protozero::ConstBytes packets) {
  WriteToOutput(output_, "compressed_packets {\n");
  if (trace_processor::util::IsGzipSupported()) {
    std::vector<uint8_t> whole_data =
        GzipDecompressor::DecompressFully(packets.data, packets.size);
    protos::pbzero::Trace::Decoder decoder(whole_data.data(),
                                           whole_data.size());
    for (auto it = decoder.packet(); it; ++it) {
      WriteToOutput(output_, "  packet {\n");
      std::string text = TracePacketToText(*it, 2);
      output_->write(text.data(), std::streamsize(text.size()));
      WriteToOutput(output_, "\n  }\n");
    }
  } else {
    static const char kErrMsg[] =
        "Cannot decode compressed packets. zlib not enabled in the build "
        "config";
    WriteToOutput(output_, kErrMsg);
    static bool log_once = [] {
      PERFETTO_ELOG("%s", kErrMsg);
      return true;
    }();
    base::ignore_result(log_once);
  }
  WriteToOutput(output_, "}\n");
}

void OnlineTraceToText::Feed(const uint8_t* data, size_t len) {
  ring_buffer_.Append(data, static_cast<size_t>(len));
  while (true) {
    auto token = ring_buffer_.ReadMessage();
    if (token.fatal_framing_error) {
      PERFETTO_ELOG("Failed to tokenize trace packet");
      ok_ = false;
      return;
    }
    if (!token.valid()) {
      // no need to set `ok_ = false` here because this just means
      // we've run out of packets in the ring buffer.
      break;
    }

    if (token.field_id != protos::pbzero::Trace::kPacketFieldNumber) {
      PERFETTO_ELOG("Skipping invalid field");
      continue;
    }
    protos::pbzero::TracePacket::Decoder decoder(token.start, token.len);
    bytes_processed_ += token.len;
    if ((packet_++ & 0x3f) == 0) {
      fprintf(stderr, "Processing trace: %8zu KB%c", bytes_processed_ / 1024,
              kProgressChar);
      fflush(stderr);
    }
    if (decoder.has_compressed_packets()) {
      PrintCompressedPackets(decoder.compressed_packets());
    } else {
      WriteToOutput(output_, "packet {\n");
      protozero::ConstBytes packet = {token.start, token.len};
      std::string text = TracePacketToText(packet, 1 /* indent_depth */);
      output_->write(text.data(), std::streamsize(text.size()));
      WriteToOutput(output_, "\n}\n");
    }
  }
}

class InputReader {
 public:
  InputReader(std::istream* input) : input_(input) {}
  // Request the input-stream to read next |len_limit| bytes and load
  // it in |data|. It also updates the |len| with actual number of bytes loaded
  // in |data|. This can be less than requested |len_limit| if we have reached
  // at the end of the file.
  bool Read(uint8_t* data, uint32_t* len, uint32_t len_limit) {
    if (input_->eof())
      return false;
    input_->read(reinterpret_cast<char*>(data), std::streamsize(len_limit));
    if (input_->bad() || (input_->fail() && !input_->eof())) {
      PERFETTO_ELOG("Failed while reading trace");
      ok_ = false;
      return false;
    }
    *len = uint32_t(input_->gcount());
    return true;
  }
  bool ok() const { return ok_; }

 private:
  std::istream* input_;
  bool ok_ = true;
};

}  // namespace

bool TraceToText(std::istream* input, std::ostream* output) {
  constexpr size_t kMaxMsgSize = protozero::ProtoRingBuffer::kMaxMsgSize;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[kMaxMsgSize]);
  uint32_t buffer_len = 0;

  InputReader input_reader(input);
  OnlineTraceToText online_trace_to_text(output);

  input_reader.Read(buffer.get(), &buffer_len, kMaxMsgSize);
  TraceType type = trace_processor::GuessTraceType(buffer.get(), buffer_len);

  if (type == TraceType::kGzipTraceType) {
    GzipDecompressor decompressor;
    auto consumer = [&](const uint8_t* data, size_t len) {
      online_trace_to_text.Feed(data, len);
    };
    using ResultCode = GzipDecompressor::ResultCode;
    do {
      ResultCode code =
          decompressor.FeedAndExtract(buffer.get(), buffer_len, consumer);
      if (code == ResultCode::kError || !online_trace_to_text.ok())
        return false;
    } while (input_reader.Read(buffer.get(), &buffer_len, kMaxMsgSize));
    return input_reader.ok();
  } else if (type == TraceType::kProtoTraceType ||
             type == trace_processor::kSymbolsTraceType) {
    do {
      online_trace_to_text.Feed(buffer.get(), buffer_len);
      if (!online_trace_to_text.ok())
        return false;
    } while (input_reader.Read(buffer.get(), &buffer_len, kMaxMsgSize));
    return input_reader.ok();
  } else {
    PERFETTO_ELOG("Unrecognised file (type: %d).", type);
    return false;
  }
}

}  // namespace trace_to_text
}  // namespace perfetto
