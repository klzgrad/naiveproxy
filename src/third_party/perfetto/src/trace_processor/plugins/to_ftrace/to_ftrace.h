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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_TO_FTRACE_TO_FTRACE_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_TO_FTRACE_TO_FTRACE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "perfetto/ext/base/dynamic_string_writer.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

class SystraceSerializer {
 public:
  using ScopedCString = base::DynamicStringWriter::ScopedCString;

  explicit SystraceSerializer(TraceProcessorContext* context);

  ScopedCString SerializeToString(uint32_t raw_row);

 private:
  using StringIdMap =
      base::FlatHashMap<StringId, std::vector<std::optional<uint32_t>>>;

  void SerializePrefix(uint32_t raw_row, base::DynamicStringWriter* writer);

  StringIdMap proto_id_to_arg_index_by_event_;
  const TraceStorage* storage_ = nullptr;
  TraceProcessorContext* context_ = nullptr;
  tables::ArgTable::ConstCursor cursor_;
  base::DynamicStringWriter line_writer_;
};

struct ToFtrace : public sqlite::Function<ToFtrace> {
  struct UserData {
    explicit UserData(TraceProcessorContext* ctx)
        : storage(ctx->storage.get()), serializer(ctx) {}
    const TraceStorage* storage;
    SystraceSerializer serializer;
  };

  static constexpr char kName[] = "TO_FTRACE";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::to_ftrace {

// Registers the ToFtrace plugin with the global plugin set. Idempotent;
// only the first call has an effect. Must run before the first GetPluginSet()
// call (i.e. before constructing TraceProcessorImpl).
void RegisterPlugin();

}  // namespace perfetto::trace_processor::to_ftrace

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_TO_FTRACE_TO_FTRACE_H_
