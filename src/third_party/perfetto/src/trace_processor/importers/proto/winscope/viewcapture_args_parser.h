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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_ARGS_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_ARGS_PARSER_H_

#include <optional>

#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/tables/winscope_tables_py.h"

namespace perfetto::trace_processor::winscope {

// Specialized args parser to de-intern ViewCapture strings
class ViewCaptureArgsParser : public ArgsParser {
 public:
  using Key = ArgsParser::Key;
  using IidToStringMap = base::FlatHashMap<uint64_t, StringId>;

  ViewCaptureArgsParser(int64_t packet_timestamp,
                        ArgsTracker::BoundInserter& inserter,
                        TraceStorage& storage,
                        PacketSequenceStateGeneration* sequence_state,
                        tables::ViewCaptureTable::RowReference* snapshot_row,
                        tables::ViewCaptureViewTable::RowReference* view_row);
  void AddInteger(const Key&, int64_t) override;
  void AddUnsignedInteger(const Key&, uint64_t) override;

  base::FlatHashMap<StringId, IidToStringMap> flat_key_to_iid_args;

 private:
  bool TryAddDeinternedString(const Key&, uint64_t);
  std::optional<protozero::ConstChars> TryDeinternString(const Key&, uint64_t);

  template <uint32_t FieldNumber, typename RowRef>
  std::optional<protozero::ConstChars>
  DeinternString(uint64_t, RowRef*, void (RowRef::*setter)(StringPool::Id));

  const base::StringView ERROR_MSG{"STRING DE-INTERNING ERROR"};
  TraceStorage& storage_;
  tables::ViewCaptureTable::RowReference* snapshot_row_;
  tables::ViewCaptureViewTable::RowReference* view_row_;
};

}  // namespace perfetto::trace_processor::winscope

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_ARGS_PARSER_H_
