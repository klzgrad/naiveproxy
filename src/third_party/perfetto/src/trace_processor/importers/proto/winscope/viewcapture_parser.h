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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_PARSER_H_

#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/winscope/viewcapture_args_parser.h"
#include "src/trace_processor/importers/proto/winscope/winscope_context.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

namespace perfetto::trace_processor::winscope {

class ViewCaptureParser {
 public:
  explicit ViewCaptureParser(WinscopeContext*);
  void Parse(int64_t timestamp,
             protozero::ConstBytes,
             PacketSequenceStateGeneration*);

 private:
  void ParseView(
      int64_t timestamp,
      protozero::ConstBytes blob,
      tables::ViewCaptureTable::Id,
      PacketSequenceStateGeneration*,
      std::unordered_map<int32_t, bool>& computed_visibility,
      std::unordered_map<int32_t, tables::WinscopeTraceRectTable::Id>&
          computed_rects);

  void AddDeinternedData(const ViewCaptureArgsParser&, uint32_t);

  WinscopeContext* const context_;
  util::ProtoToArgsParser args_parser_;
};
}  // namespace perfetto::trace_processor::winscope

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_PARSER_H_
