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

#include "src/trace_processor/importers/android_bugreport/chunked_line_reader.h"

#include <cstddef>
#include <cstring>
#include <utility>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"

namespace perfetto ::trace_processor {

namespace {

TraceBlobView Append(const TraceBlobView& head, const TraceBlobView& tail) {
  size_t size = head.size() + tail.size();
  if (size == 0) {
    return TraceBlobView();
  }
  auto blob = TraceBlob::Allocate(size);
  memcpy(blob.data(), head.data(), head.size());
  memcpy(blob.data() + head.size(), tail.data(), tail.size());
  return TraceBlobView(std::move(blob));
}

struct SpliceResult {
  // Full line including '\n'.
  TraceBlobView line;
  // Any leftovers
  TraceBlobView leftovers;
  // True if a complete line was found (even if it's empty)
  bool found_line;
};

SpliceResult SpliceAtNewLine(TraceBlobView data) {
  for (size_t i = 0; i < data.size(); ++i) {
    if (data.data()[i] == '\n') {
      return {data.slice_off(0, i), data.slice_off(i + 1, data.size() - i - 1),
              true};
    }
  }
  return {TraceBlobView(), std::move(data), false};
}
}  // namespace

base::Status ChunkedLineReader::OnLine(const TraceBlobView& data) {
  return ParseLine(base::StringView(reinterpret_cast<const char*>(data.data()),
                                    data.size()));
}

base::StatusOr<TraceBlobView> ChunkedLineReader::SpliceLoop(
    TraceBlobView data) {
  while (true) {
    SpliceResult res = SpliceAtNewLine(std::move(data));
    if (!res.found_line) {
      return std::move(res.leftovers);
    }
    RETURN_IF_ERROR(OnLine(res.line));
    data = std::move(res.leftovers);
  }
}

base::Status ChunkedLineReader::Parse(TraceBlobView data) {
  if (data.size() == 0) {
    return base::OkStatus();
  }

  if (PERFETTO_LIKELY(buffer_.size() == 0)) {
    ASSIGN_OR_RETURN(buffer_, SpliceLoop(std::move(data)));
    return base::OkStatus();
  }

  SpliceResult res = SpliceAtNewLine(std::move(data));
  if (!res.found_line) {
    buffer_ = Append(buffer_, res.leftovers);
    return base::OkStatus();
  }

  buffer_ = Append(buffer_, res.line);
  RETURN_IF_ERROR(OnLine(buffer_));
  ASSIGN_OR_RETURN(buffer_, SpliceLoop(std::move(res.leftovers)));
  return base::OkStatus();
}

base::Status ChunkedLineReader::NotifyEndOfFile() {
  EndOfStream(base::StringView(reinterpret_cast<const char*>(buffer_.data()),
                               buffer_.size()));
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
