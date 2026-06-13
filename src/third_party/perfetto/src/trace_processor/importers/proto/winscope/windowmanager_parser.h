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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINDOWMANAGER_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINDOWMANAGER_PARSER_H_

#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/importers/proto/winscope/windowmanager_hierarchy_walker.h"
#include "src/trace_processor/importers/proto/winscope/winscope_context.h"
#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

namespace perfetto::trace_processor::winscope {

class WindowManagerParser {
 public:
  explicit WindowManagerParser(WinscopeContext*);
  void Parse(int64_t timestamp, protozero::ConstBytes blob);

 private:
  tables::WindowManagerTable::Id InsertSnapshotRow(
      int64_t timestamp,
      protos::pbzero::WindowManagerTraceEntry::Decoder& entry_decoder);
  void InsertWindowContainerRows(
      int64_t timestamp,
      tables::WindowManagerTable::Id snapshot_id,
      const std::vector<WindowManagerHierarchyWalker::ExtractedWindowContainer>&
          window_containers);
  tables::WinscopeTraceRectTable::Id InsertRectRows(
      const WindowManagerHierarchyWalker::ExtractedRect& rect);
  tables::WinscopeTransformTable::Id MaybeInsertIdentityTransformRow();
  void InsertWindowContainerArgs(
      int64_t timestamp,
      tables::WindowManagerWindowContainerTable::Id row_id,
      const WindowManagerHierarchyWalker::ExtractedWindowContainer&
          window_container);

  WinscopeContext* const context_;
  WindowManagerHierarchyWalker hierarchy_walker_;
  util::ProtoToArgsParser args_parser_;
  std::optional<tables::WinscopeTransformTable::Id> transform_id_;
};
}  // namespace perfetto::trace_processor::winscope

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINDOWMANAGER_PARSER_H_
