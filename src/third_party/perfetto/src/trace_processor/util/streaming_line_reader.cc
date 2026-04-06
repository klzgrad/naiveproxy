/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/trace_processor/util/streaming_line_reader.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"

namespace perfetto::trace_processor::util {

StreamingLineReader::StreamingLineReader(LinesCallback cb)
    : lines_callback_(std::move(cb)) {}
StreamingLineReader::~StreamingLineReader() = default;

char* StreamingLineReader::BeginWrite(size_t write_buf_size) {
  PERFETTO_DCHECK(size_before_write_ == 0);
  size_before_write_ = buf_.size();
  buf_.resize(size_before_write_ + write_buf_size);
  return &buf_[size_before_write_];
}

void StreamingLineReader::EndWrite(size_t size_written) {
  PERFETTO_DCHECK(size_before_write_ + size_written <= buf_.size());
  buf_.resize(size_before_write_ + size_written);
  size_before_write_ = 0;

  size_t consumed = Tokenize(base::StringView(buf_.data(), buf_.size()));
  PERFETTO_DCHECK(consumed <= buf_.size());

  // Unless we got very lucky, the last line in the chunk just written will be
  // incomplete. Move it to the beginning of the buffer so it gets glued
  // together on the next {Begin,End}Write() call.
  buf_.erase(buf_.begin(), buf_.begin() + static_cast<int64_t>(consumed));
}

size_t StreamingLineReader::Tokenize(base::StringView input) {
  size_t chars_consumed = 0;
  const char* line_start = input.data();
  std::vector<base::StringView> lines;
  lines.reserve(1000);  // An educated guess to avoid silly expansions.
  for (const char* c = input.data(); c < input.end(); ++c) {
    if (*c != '\n')
      continue;
    lines.emplace_back(line_start, static_cast<size_t>(c - line_start));
    line_start = c + 1;
    chars_consumed = static_cast<size_t>(c + 1 - input.data());
  }  // for(c : input)

  PERFETTO_DCHECK(lines.empty() ^ (chars_consumed != 0));
  if (!lines.empty())
    lines_callback_(lines);
  return chars_consumed;
}

}  // namespace perfetto::trace_processor::util
