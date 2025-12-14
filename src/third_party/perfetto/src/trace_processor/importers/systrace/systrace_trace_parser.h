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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_SYSTRACE_SYSTRACE_TRACE_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_SYSTRACE_SYSTRACE_TRACE_PARSER_H_

#include <cstdint>
#include <deque>
#include <memory>

#include "perfetto/base/status.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/systrace/systrace_line.h"
#include "src/trace_processor/importers/systrace/systrace_line_parser.h"
#include "src/trace_processor/importers/systrace/systrace_line_tokenizer.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

class SystraceTraceParser : public ChunkedTraceReader {
 public:
  explicit SystraceTraceParser(TraceProcessorContext*);
  ~SystraceTraceParser() override;

  // ChunkedTraceReader implementation.
  base::Status Parse(TraceBlobView) override;
  base::Status NotifyEndOfFile() override;

 private:
  enum ParseState {
    kBeforeParse,
    kHtmlBeforeSystrace,
    kTraceDataSection,
    kSystrace,
    kProcessDumpLong,
    kProcessDumpShort,
    kCgroupDump,
    kEndOfSystrace,
  };

  ParseState state_ = ParseState::kBeforeParse;

  // Used to glue together trace packets that span across two (or more)
  // Parse() boundaries.
  std::deque<uint8_t> partial_buf_;

  SystraceLineTokenizer line_tokenizer_;
  SystraceLineParser line_parser_;
  TraceProcessorContext* ctx_;

  std::unique_ptr<TraceSorter::Stream<SystraceLine>> stream_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_SYSTRACE_SYSTRACE_TRACE_PARSER_H_
