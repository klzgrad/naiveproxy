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

#include "perfetto/ext/base/utils.h"

namespace perfetto {
namespace trace_processor {

ProtoTraceTokenizer::ProtoTraceTokenizer() = default;

base::Status ProtoTraceTokenizer::Decompress(TraceBlobView input,
                                             TraceBlobView* output) {
  PERFETTO_DCHECK(util::IsGzipSupported());

  std::vector<uint8_t> data;
  data.reserve(input.length());

  // Ensure that the decompressor is able to cope with a new stream of data.
  decompressor_.Reset();
  using ResultCode = util::GzipDecompressor::ResultCode;
  ResultCode ret = decompressor_.FeedAndExtract(
      input.data(), input.length(),
      [&data](const uint8_t* buffer, size_t buffer_len) {
        data.insert(data.end(), buffer, buffer + buffer_len);
      });

  if (ret == ResultCode::kError || ret == ResultCode::kNeedsMoreInput) {
    return base::ErrStatus("Failed to decompress (error code: %d)",
                           static_cast<int>(ret));
  }

  TraceBlob out_blob = TraceBlob::CopyFrom(data.data(), data.size());
  *output = TraceBlobView(std::move(out_blob));
  return base::OkStatus();
}

}  // namespace trace_processor
}  // namespace perfetto
