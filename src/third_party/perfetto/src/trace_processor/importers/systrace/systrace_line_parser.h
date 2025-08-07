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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_SYSTRACE_SYSTRACE_LINE_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_SYSTRACE_SYSTRACE_LINE_PARSER_H_

#include "perfetto/trace_processor/status.h"

#include "src/trace_processor/importers/common/trace_parser.h"
#include "src/trace_processor/importers/ftrace/rss_stat_tracker.h"
#include "src/trace_processor/importers/systrace/systrace_line.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {

class SystraceLineParser {
 public:
  explicit SystraceLineParser(TraceProcessorContext*);

  base::Status ParseLine(const SystraceLine&);

 private:
  TraceProcessorContext* const context_;
  RssStatTracker rss_stat_tracker_;

  const StringId sched_wakeup_name_id_ = kNullStringId;
  const StringId sched_waking_name_id_ = kNullStringId;
  const StringId workqueue_name_id_ = kNullStringId;
  const StringId sched_blocked_reason_id_ = kNullStringId;
  const StringId io_wait_id_ = kNullStringId;
  const StringId waker_utid_id_ = kNullStringId;
  const StringId unknown_thread_name_id_ = kNullStringId;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_SYSTRACE_SYSTRACE_LINE_PARSER_H_
