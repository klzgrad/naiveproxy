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

#include "src/trace_processor/importers/proto/proto_trace_tokenizer.h"
#include "perfetto/trace_processor/trace_blob.h"

#include <optional>

#include "perfetto/ext/base/utils.h"
#include "src/trace_processor/util/decompressor.h"

namespace perfetto {
namespace trace_processor {

ProtoTraceTokenizer::ProtoTraceTokenizer() = default;

base::Status ProtoTraceTokenizer::Decompress(util::CompressionType type,
                                             TraceBlobView input,
                                             TraceBlobView* output) {
  if (!decompressor_) {
    decompressor_ = util::CreateDecompressor(type);
    decompressor_type_ = type;
    if (!decompressor_) {
      auto info = util::GetCompressionCodecInfo(type);
      return base::ErrStatus(
          "Cannot decompress compressed_packets: %s is not enabled in this "
          "build (rebuild with %s=true)",
          info.name, info.gn_arg);
    }
  } else if (decompressor_type_ != type) {
    // Malformed (or adversarial) input: a well-formed trace never mixes codecs.
    return base::ErrStatus(
        "A trace must not mix compressed_packets and zstd_compressed_packets "
        "(ERR:tp-corrupt)");
  } else {
    decompressor_->Reset();
  }

  // A bundle holds exactly one compressed frame, so any trailing input is
  // malformed. Hand the owned buffer straight to the TraceBlobView so the
  // packets sliced out of it cost no second copy.
  std::optional<util::DecompressedBuffer> buffer =
      util::DecompressToBuffer(*decompressor_, input.data(), input.length(),
                               util::FrameMode::kSingleFrame);
  if (!buffer) {
    return base::ErrStatus(
        "Failed to decompress compressed_packets (ERR:tp-corrupt)");
  }
  *output = TraceBlobView(
      TraceBlob::TakeOwnership(std::move(buffer->data), buffer->size));
  return base::OkStatus();
}

}  // namespace trace_processor
}  // namespace perfetto
