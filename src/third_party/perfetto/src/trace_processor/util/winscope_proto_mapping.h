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

#ifndef SRC_TRACE_PROCESSOR_UTIL_WINSCOPE_PROTO_MAPPING_H_
#define SRC_TRACE_PROCESSOR_UTIL_WINSCOPE_PROTO_MAPPING_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/cursor.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/android_tables_py.h"
#include "src/trace_processor/tables/winscope_tables_py.h"

namespace perfetto::trace_processor::util::winscope_proto_mapping {

namespace {
constexpr std::string_view CONTAINER_TYPE_COL = "container_type";

// Helper to extract string values via Cell() callback.
struct StringCellExtractor : dataframe::CellCallback {
  std::optional<base::StringView> result;
  void OnCell(int64_t) {}
  void OnCell(double) {}
  void OnCell(NullTermStringView v) { result = v; }
  void OnCell(std::nullptr_t) { result = std::nullopt; }
  void OnCell(uint32_t) {}
  void OnCell(int32_t) {}
};
constexpr std::string_view ROOT_WINDOW_CONTAINER = "RootWindowContainer";
constexpr std::string_view DISPLAY_CONTENT = "DisplayContent";
constexpr std::string_view DISPLAY_AREA = "DisplayArea";
constexpr std::string_view TASK = "Task";
constexpr std::string_view TASK_FRAGMENT = "TaskFragment";
constexpr std::string_view ACTIVITY = "Activity";
constexpr std::string_view WINDOW_TOKEN = "WindowToken";
constexpr std::string_view WINDOW_STATE = "WindowState";
constexpr std::string_view WINDOW_CONTAINER = "WindowContainer";

std::optional<const base::StringView> inline GetWindowContainerType(
    const dataframe::Dataframe& static_table,
    uint32_t row,
    StringPool*) {
  auto col_idx = static_table.IndexOfColumnLegacy(CONTAINER_TYPE_COL);
  if (!col_idx.has_value()) {
    return std::nullopt;
  }
  StringCellExtractor extractor;
  static_table.GetCell(row, col_idx.value(), extractor);
  return extractor.result;
}
}  // namespace

inline base::StatusOr<const char* const> GetProtoName(
    const std::string& table_name) {
  if (table_name == tables::SurfaceFlingerLayerTable::Name()) {
    return ".perfetto.protos.LayerProto";
  }
  if (table_name == tables::SurfaceFlingerLayersSnapshotTable::Name()) {
    return ".perfetto.protos.LayersSnapshotProto";
  }
  if (table_name == tables::SurfaceFlingerTransactionsTable::Name()) {
    return ".perfetto.protos.TransactionTraceEntry";
  }
  if (table_name == tables::WindowManagerShellTransitionProtosTable::Name()) {
    return ".perfetto.protos.ShellTransition";
  }
  if (table_name == tables::InputMethodClientsTable::Name()) {
    return ".perfetto.protos.InputMethodClientsTraceProto";
  }
  if (table_name == tables::InputMethodManagerServiceTable::Name()) {
    return ".perfetto.protos.InputMethodManagerServiceTraceProto";
  }
  if (table_name == tables::InputMethodServiceTable::Name()) {
    return ".perfetto.protos.InputMethodServiceTraceProto";
  }
  if (table_name == tables::ViewCaptureTable::Name()) {
    return ".perfetto.protos.ViewCapture";
  }
  if (table_name == tables::ViewCaptureViewTable::Name()) {
    return ".perfetto.protos.ViewCapture.View";
  }
  if (table_name == tables::WindowManagerTable::Name()) {
    return ".perfetto.protos.WindowManagerTraceEntry";
  }
  if (table_name == tables::WindowManagerWindowContainerTable::Name()) {
    return ".perfetto.protos.WindowContainerChildProto";
  }
  if (table_name == tables::AndroidKeyEventsTable::Name()) {
    return ".perfetto.protos.AndroidKeyEvent";
  }
  if (table_name == tables::AndroidMotionEventsTable::Name()) {
    return ".perfetto.protos.AndroidMotionEvent";
  }
  if (table_name == tables::AndroidInputEventDispatchTable::Name()) {
    return ".perfetto.protos.AndroidWindowInputDispatchEvent";
  }
  return base::ErrStatus("%s table does not have proto descriptor.",
                         table_name.c_str());
}

inline std::optional<const std::vector<uint32_t>> GetAllowedFields(
    const std::string& table_name) {
  if (table_name == tables::SurfaceFlingerLayersSnapshotTable::Name()) {
    // omit layers
    return std::vector<uint32_t>({1, 2, 4, 5, 6, 7, 8});
  }
  if (table_name == tables::ViewCaptureTable::Name()) {
    // omit views
    return std::vector<uint32_t>({1, 2});
  }
  if (table_name == tables::WindowManagerTable::Name()) {
    // omit root_window_container
    return std::vector<uint32_t>({1, 3, 4, 5, 6, 7, 8, 9, 19, 11, 12});
  }
  return std::nullopt;
}

inline std::optional<const std::vector<uint32_t>> GetAllowedFieldsPerRow(
    const std::string& table_name,
    const dataframe::Dataframe& static_table,
    uint32_t row,
    StringPool* string_pool) {
  if (table_name != tables::WindowManagerWindowContainerTable::Name()) {
    return std::nullopt;
  }
  auto maybe_container_type =
      GetWindowContainerType(static_table, row, string_pool);
  if (!maybe_container_type.has_value()) {
    return std::nullopt;
  }

  // proto message is WindowContainerChildProto
  const auto container_type = maybe_container_type.value();
  if (container_type == WINDOW_CONTAINER) {
    return std::vector<uint32_t>({2});  // window_container
  }
  if (container_type == DISPLAY_CONTENT) {
    return std::vector<uint32_t>({3});  // display_content
  }
  if (container_type == DISPLAY_AREA) {
    return std::vector<uint32_t>({4});  // display_area
  }
  if (container_type == TASK) {
    return std::vector<uint32_t>({5});  // task
  }
  if (container_type == ACTIVITY) {
    return std::vector<uint32_t>({6});  // activity
  }
  if (container_type == WINDOW_TOKEN) {
    return std::vector<uint32_t>({7});  // window_token
  }
  if (container_type == WINDOW_STATE) {
    return std::vector<uint32_t>({8});  // window
  }
  if (container_type == TASK_FRAGMENT) {
    return std::vector<uint32_t>({9});  // task_fragment
  }
  return std::nullopt;
}

inline std::optional<const std::string> GetGroupIdColName(
    const std::string& table_name) {
  if (table_name == tables::WindowManagerShellTransitionProtosTable::Name()) {
    return "transition_id";
  }
  return std::nullopt;
}

inline const tables::ViewCaptureInternedDataTable* GetInternedDataTable(
    const std::string& table_name,
    TraceStorage* storage) {
  if (table_name == tables::ViewCaptureTable::Name() ||
      table_name == tables::ViewCaptureViewTable::Name()) {
    return storage->mutable_viewcapture_interned_data_table();
  }
  return nullptr;
}

inline bool ShouldSkipRow(const std::string& table_name,
                          const dataframe::Dataframe& static_table,
                          uint32_t row,
                          StringPool* string_pool) {
  if (table_name != tables::WindowManagerWindowContainerTable::Name()) {
    return false;
  }
  auto maybe_container_type =
      GetWindowContainerType(static_table, row, string_pool);
  if (!maybe_container_type.has_value()) {
    return false;
  }
  const auto& container_type = maybe_container_type.value();
  return container_type == ROOT_WINDOW_CONTAINER;
}

}  // namespace perfetto::trace_processor::util::winscope_proto_mapping

#endif  // SRC_TRACE_PROCESSOR_UTIL_WINSCOPE_PROTO_MAPPING_H_
