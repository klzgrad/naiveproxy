/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_CONTEXT_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_CONTEXT_H_

#include "src/trace_processor/importers/proto/winscope/protolog_message_decoder.h"
#include "src/trace_processor/importers/proto/winscope/shell_transitions_tracker.h"
#include "src/trace_processor/importers/proto/winscope/winscope_rect_tracker.h"
#include "src/trace_processor/importers/proto/winscope/winscope_transform_tracker.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::winscope {

class WinscopeContext {
 public:
  explicit WinscopeContext(TraceProcessorContext* context)
      : trace_processor_context_(context),
        shell_transitions_tracker_(context),
        protolog_message_decoder_(context),
        rect_tracker_(context),
        transform_tracker_(context) {}

  TraceProcessorContext* trace_processor_context_;
  ShellTransitionsTracker shell_transitions_tracker_;
  ProtoLogMessageDecoder protolog_message_decoder_;
  WinscopeRectTracker rect_tracker_;
  WinscopeTransformTracker transform_tracker_;
};

}  // namespace perfetto::trace_processor::winscope

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_CONTEXT_H_
