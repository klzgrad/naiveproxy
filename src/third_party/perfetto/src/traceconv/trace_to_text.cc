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
#include "src/traceconv/android_extension.descriptor.h"
#include "src/traceconv/trace.descriptor.h"
#include "src/traceconv/utils.h"

#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

#include "src/trace_processor/util/decompressor.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/protozero_to_text.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto {
namespace trace_to_text {
namespace {

using perfetto::trace_processor::DescriptorPool;
namespace util = trace_processor::util;

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
  OnlineTraceToText(std::ostream* output, const TraceToTextOptions& options)
      : output_(output), skip_unknown_fields_(options.skip_unknown_fields) {
    pool_.AddFromFileDescriptorSet(kTraceDescriptor.data(),
                                   kTraceDescriptor.size());
    pool_.AddFromFileDescriptorSet(kAndroidExtensionDescriptor.data(),
                                   kAndroidExtensionDescriptor.size());
  }
  OnlineTraceToText(const OnlineTraceToText&) = delete;
  OnlineTraceToText& operator=(const OnlineTraceToText&) = delete;
  void Feed(const uint8_t* data, size_t len);
  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }

 private:
  std::string TracePacketToText(protozero::ConstBytes packet,
                                uint32_t indent_depth);
  void PrintCompressedPackets(protozero::ConstBytes packets,
                              util::CompressionType type);

  bool ok_ = true;
  std::string error_;
  std::ostream* output_;
  protozero::ProtoRingBuffer ring_buffer_;
  DescriptorPool pool_;
  size_t bytes_processed_ = 0;
  size_t packet_ = 0;
  bool skip_unknown_fields_ = false;
};

std::string OnlineTraceToText::TracePacketToText(protozero::ConstBytes packet,
                                                 uint32_t indent_depth) {
  namespace pb0_to_text = trace_processor::protozero_to_text;
  return pb0_to_text::ProtozeroToText(pool_, ".perfetto.protos.TracePacket",
                                      packet, pb0_to_text::kIncludeNewLines,
                                      indent_depth, skip_unknown_fields_);
}

void OnlineTraceToText::PrintCompressedPackets(protozero::ConstBytes packets,
                                               util::CompressionType type) {
  if (type == util::CompressionType::kZstd) {
    WriteToOutput(output_, "zstd_compressed_packets {\n");
  } else {
    WriteToOutput(output_, "compressed_packets {\n");
  }
  if (util::IsCompressionSupported(type)) {
    std::optional<util::DecompressedBuffer> whole_data =
        util::DecompressToBuffer(type, packets.data, packets.size);
    if (whole_data) {
      protos::pbzero::Trace::Decoder decoder(whole_data->data.get(),
                                             whole_data->size);
      for (auto it = decoder.packet(); it; ++it) {
        WriteToOutput(output_, "  packet {\n");
        std::string text = TracePacketToText(*it, 2);
        output_->write(text.data(), std::streamsize(text.size()));
        WriteToOutput(output_, "\n  }\n");
      }
    } else {
      WriteToOutput(output_,
                    "  # Failed to decompress: corrupt or truncated packets\n");
    }
  } else {
    static const char kErrMsg[] =
        "Cannot decode compressed packets: the codec is not enabled in the "
        "build config";
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
      error_ = "failed to tokenize trace packet (corrupt or truncated)";
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
      ProgressLine("Processing trace: %8zu KB", bytes_processed_ / 1024);
    }
    if (decoder.has_compressed_packets()) {
      PrintCompressedPackets(decoder.compressed_packets(),
                             util::CompressionType::kGzip);
    } else if (decoder.has_zstd_compressed_packets()) {
      PrintCompressedPackets(decoder.zstd_compressed_packets(),
                             util::CompressionType::kZstd);
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
      error_ = "failed while reading trace";
      ok_ = false;
      return false;
    }
    *len = uint32_t(input_->gcount());
    return true;
  }
  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }

 private:
  std::istream* input_;
  bool ok_ = true;
  std::string error_;
};

}  // namespace

base::Status TraceToText(std::istream* input,
                         std::ostream* output,
                         const TraceToTextOptions& options) {
  constexpr size_t kMaxMsgSize = protozero::ProtoRingBuffer::kMaxMsgSize;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[kMaxMsgSize]);
  uint32_t buffer_len = 0;

  InputReader input_reader(input);
  OnlineTraceToText online_trace_to_text(output, options);

  input_reader.Read(buffer.get(), &buffer_len, kMaxMsgSize);
  trace_processor::CompressedTraceType type =
      trace_processor::SniffCompressedTraceType(buffer.get(), buffer_len);

  if (type == trace_processor::CompressedTraceType::kGzip ||
      type == trace_processor::CompressedTraceType::kZstd) {
    util::CompressionType codec =
        type == trace_processor::CompressedTraceType::kZstd
            ? util::CompressionType::kZstd
            : util::CompressionType::kGzip;
    std::unique_ptr<util::Decompressor> decompressor =
        util::CreateDecompressor(codec);
    if (!decompressor) {
      // The codec isn't enabled in this build.
      return base::ErrStatus(
          "cannot decode compressed packets: the codec is not enabled in the "
          "build config");
    }

    using ResultCode = util::Decompressor::ResultCode;
    uint8_t out[4096];
    ResultCode code = ResultCode::kNeedsMoreInput;
    do {
      // A frame that ended right at the previous chunk's edge means this chunk
      // opens a new concatenated frame (e.g. pzstd output); reset first.
      if (code == ResultCode::kEof)
        decompressor->Reset();

      decompressor->Feed(buffer.get(), buffer_len);
      for (;;) {
        auto res = decompressor->ExtractOutput(out, sizeof(out));
        if (res.ret == ResultCode::kError) {
          EndProgressLine();
          return base::ErrStatus(
              "failed to decompress, trace is likely corrupt");
        }
        if (res.bytes_written > 0)
          online_trace_to_text.Feed(out, res.bytes_written);
        if (!online_trace_to_text.ok()) {
          EndProgressLine();
          return base::ErrStatus("failed to convert trace to text: %s",
                                 online_trace_to_text.error().c_str());
        }
        code = res.ret;
        if (res.ret == ResultCode::kOk)
          continue;  // More output buffered; keep draining.
        if (res.ret == ResultCode::kNeedsMoreInput)
          break;  // Frame continues in the next chunk.
        // kEof: this frame is done. If input remains it's another concatenated
        // frame in the same chunk, so reset and decode it; otherwise the chunk
        // ended on a frame boundary.
        if (decompressor->AvailIn() == 0)
          break;
        decompressor->Reset();
      }
      // At EOF, Read() returns true once more with buffer_len == 0; stop rather
      // than feed an empty chunk, which would flip `code` off kEof and be
      // misread as a truncated stream below.
    } while (input_reader.Read(buffer.get(), &buffer_len, kMaxMsgSize) &&
             buffer_len > 0);

    if (code != ResultCode::kEof) {
      EndProgressLine();
      return base::ErrStatus(
          "compressed stream incomplete, trace is likely corrupt");
    }
    if (!input_reader.ok()) {
      EndProgressLine();
      return base::ErrStatus("failed to read trace: %s",
                             input_reader.error().c_str());
    }
    EndProgressLine();
    return base::OkStatus();
  } else if (type == trace_processor::CompressedTraceType::kProto) {
    do {
      online_trace_to_text.Feed(buffer.get(), buffer_len);
      if (!online_trace_to_text.ok()) {
        EndProgressLine();
        return base::ErrStatus("failed to convert trace to text: %s",
                               online_trace_to_text.error().c_str());
      }
    } while (input_reader.Read(buffer.get(), &buffer_len, kMaxMsgSize));
    if (!input_reader.ok()) {
      EndProgressLine();
      return base::ErrStatus("failed to read trace: %s",
                             input_reader.error().c_str());
    }
    EndProgressLine();
    return base::OkStatus();
  } else {
    return base::ErrStatus(
        "unrecognised file: the input does not look like a Perfetto trace");
  }
}

}  // namespace trace_to_text
}  // namespace perfetto
