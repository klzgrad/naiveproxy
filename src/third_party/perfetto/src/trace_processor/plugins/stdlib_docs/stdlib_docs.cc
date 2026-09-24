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

#include "src/trace_processor/plugins/stdlib_docs/stdlib_docs.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/core/plugin/registration.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/plugins/stdlib_docs/tables_py.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/simple_json_serializer.h"
#include "src/trace_processor/util/sql_module_doc_parser.h"
#include "src/trace_processor/util/sql_modules.h"

namespace perfetto::trace_processor::stdlib_docs {

namespace {

template <typename Entry>
std::string SerializeEntries(const std::vector<Entry>& entries) {
  return json::SerializeJson([&](json::JsonValueSerializer&& writer) {
    std::move(writer).WriteArray([&](json::JsonArraySerializer& array) {
      for (const auto& entry : entries) {
        array.AppendDict([&](json::JsonDictSerializer& dict) {
          dict.AddString("name", entry.name);
          dict.AddString("type", entry.type);
          dict.AddString("description", entry.description);
        });
      }
    });
  });
}

template <typename Entry>
void AppendSummaryEntries(const char* heading,
                          const std::vector<Entry>& entries,
                          std::string* summary) {
  if (entries.empty()) {
    return;
  }
  summary->append("\n").append(heading).append(":");
  for (const auto& entry : entries) {
    summary->append("\n- ")
        .append(entry.name)
        .append(" (")
        .append(entry.type)
        .append(")");
    if (!entry.description.empty()) {
      summary->append(": ").append(entry.description);
    }
  }
}

std::string_view ShortDescription(std::string_view description) {
  size_t size = std::min<size_t>(description.size(), 160);
  while (size > 0 && size < description.size() &&
         (static_cast<uint8_t>(description[size]) & 0xc0) == 0x80) {
    --size;
  }
  return description.substr(0, size);
}

std::string HumanizeName(std::string_view package,
                         std::string_view module,
                         std::string_view name) {
  std::string humanized;
  humanized.reserve(package.size() + module.size() + name.size() + 2);
  humanized.append(package);
  humanized.push_back(' ');
  humanized.append(module);
  if (!name.empty() && name != module) {
    humanized.push_back(' ');
    humanized.append(name);
  }
  for (char& c : humanized) {
    if (c == '.' || c == '_') {
      c = ' ';
    }
  }
  return humanized;
}

template <typename Arg, typename Column>
std::string BuildSummary(std::string_view package,
                         std::string_view module,
                         std::string_view name,
                         std::string_view qualified_name,
                         std::string_view object_type,
                         bool exposed,
                         std::string_view description,
                         std::string_view return_type,
                         std::string_view return_description,
                         const std::vector<Arg>& args,
                         const std::vector<Column>& columns) {
  std::string summary;
  summary.reserve(package.size() + module.size() + name.size() +
                  description.size() + return_description.size() + 128);
  summary.append("Package: ").append(package);
  summary.append("\nModule: ").append(module);
  if (object_type != "MODULE") {
    summary.append("\nName: ").append(name);
  }
  summary.append("\nQualified name: ").append(qualified_name);
  summary.append("\nSearch aliases: ")
      .append(HumanizeName(package, module, name))
      .append("\nKind: ")
      .append(exposed ? "public " : "internal ");
  summary.append(object_type);
  if (!description.empty()) {
    summary.append("\nDescription: ").append(description);
  }
  AppendSummaryEntries("Arguments", args, &summary);
  if (!return_type.empty()) {
    summary.append("\nReturns: ").append(return_type);
    if (!return_description.empty()) {
      summary.append(": ").append(return_description);
    }
  }
  AppendSummaryEntries("Columns", columns, &summary);
  return summary;
}

// Parses |module_key| in the caller-resolved |package|. The package must not
// be re-derived from the key: names can contain dots (e.g. "dev.perfetto.test"
// owns "dev.perfetto.test.common").
base::StatusOr<stdlib_doc::ParsedModule> ParseModule(
    const sql_modules::RegisteredPackage* package,
    const std::string& module_key) {
  if (!package) {
    return base::ErrStatus("Module not found: %s", module_key.c_str());
  }
  const auto* mod = package->modules.Find(module_key);
  if (!mod) {
    return base::ErrStatus("Module not found: %s", module_key.c_str());
  }
  PERFETTO_DCHECK(mod->size() <= std::numeric_limits<uint32_t>::max());
  auto parsed = stdlib_doc::ParseStdlibModule(
      mod->data(), static_cast<uint32_t>(mod->size()));
  for (const auto& err : parsed.errors) {
    PERFETTO_DLOG("stdlib docs: parse error in '%s': %s", module_key.c_str(),
                  err.c_str());
  }
  return parsed;
}

// __intrinsic_stdlib_objects is a zero-argument, table-like intrinsic.
class StdlibDocsObjects : public StaticTableFunction {
 public:
  class Cursor : public StaticTableFunction::Cursor {
   public:
    Cursor(StringPool* pool, const PerfettoSqlConnection* connection)
        : string_pool_(pool), engine_(connection), table_(pool) {}

    bool Run(const std::vector<SqlValue>& arguments) override {
      PERFETTO_DCHECK(arguments.empty());
      auto dataframe = GetDataframe();
      if (!dataframe.ok()) {
        return OnFailure(dataframe.status());
      }
      return OnSuccess(*dataframe);
    }

   private:
    base::StatusOr<dataframe::Dataframe*> GetDataframe() {
      table_.Clear();
      const std::vector<stdlib_doc::Arg> no_args;
      const std::vector<stdlib_doc::Column> no_columns;
      for (const auto& package_and_module : engine_->GetModules()) {
        const std::string& package = package_and_module.first;
        const std::string& module = package_and_module.second;
        auto parsed = ParseModule(engine_->FindPackage(package), module);
        if (!parsed.ok()) {
          return parsed.status();
        }

        auto insert = [&](std::string_view name,
                          std::string_view qualified_name,
                          std::string_view object_type, bool exposed,
                          std::string_view description,
                          std::string_view return_type,
                          std::string_view return_description, const auto& args,
                          const auto& columns) {
          tables::StdlibDocsObjectsTable::Row row;
          row.package = string_pool_->InternString(base::StringView(package));
          row.module = string_pool_->InternString(base::StringView(module));
          row.name = string_pool_->InternString(base::StringView(name));
          row.qualified_name =
              string_pool_->InternString(base::StringView(qualified_name));
          row.object_type =
              string_pool_->InternString(base::StringView(object_type));
          row.exposed = exposed ? 1 : 0;
          row.short_description = string_pool_->InternString(
              base::StringView(ShortDescription(description)));
          row.summary = string_pool_->InternString(base::StringView(
              BuildSummary(package, module, name, qualified_name, object_type,
                           exposed, description, return_type,
                           return_description, args, columns)));
          row.description =
              string_pool_->InternString(base::StringView(description));
          row.return_type =
              string_pool_->InternString(base::StringView(return_type));
          row.return_description =
              string_pool_->InternString(base::StringView(return_description));
          row.args = string_pool_->InternString(
              base::StringView(SerializeEntries(args)));
          row.cols = string_pool_->InternString(
              base::StringView(SerializeEntries(columns)));
          table_.Insert(row);
        };

        insert(module, module, "MODULE", true, "", "", "", no_args, no_columns);
        for (const auto& table_view : parsed->table_views) {
          insert(table_view.name, module + "." + table_view.name,
                 table_view.type, table_view.exposed, table_view.description,
                 "", "", no_args, table_view.columns);
        }
        for (const auto& function : parsed->functions) {
          insert(function.name, module + "." + function.name,
                 function.is_table_function ? "TABLE_FUNCTION" : "FUNCTION",
                 function.exposed, function.description, function.return_type,
                 function.return_description, function.args, function.columns);
        }
        for (const auto& macro : parsed->macros) {
          insert(macro.name, module + "." + macro.name, "MACRO", macro.exposed,
                 macro.description, macro.return_type, macro.return_description,
                 macro.args, no_columns);
        }
      }

      return &table_.dataframe();
    }

    StringPool* string_pool_ = nullptr;
    const PerfettoSqlConnection* engine_ = nullptr;
    tables::StdlibDocsObjectsTable table_;
  };

  StdlibDocsObjects(StringPool* pool, const PerfettoSqlConnection* connection)
      : string_pool_(pool), engine_(connection) {}

  std::unique_ptr<StaticTableFunction::Cursor> MakeCursor() override {
    return std::make_unique<Cursor>(string_pool_, engine_);
  }

  dataframe::DataframeSpec CreateSpec() override {
    return tables::StdlibDocsObjectsTable::kSpec.ToUntypedDataframeSpec();
  }

  std::string TableName() override { return "__intrinsic_stdlib_objects"; }

  uint32_t GetArgumentCount() const override { return 0; }

 private:
  StringPool* string_pool_ = nullptr;
  const PerfettoSqlConnection* engine_ = nullptr;
};

class StdlibDocsPlugin : public Plugin<StdlibDocsPlugin> {
 public:
  ~StdlibDocsPlugin() override;

  void RegisterStaticTableFunctions(
      PerfettoSqlConnection* connection,
      std::vector<std::unique_ptr<StaticTableFunction>>& functions) override {
    StringPool* pool = trace_context_->storage->mutable_string_pool();
    functions.emplace_back(
        std::make_unique<StdlibDocsObjects>(pool, connection));
  }
};

StdlibDocsPlugin::~StdlibDocsPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<StdlibDocsPlugin>();
      },
      StdlibDocsPlugin::kPluginId, StdlibDocsPlugin::kDepIds.data(),
      StdlibDocsPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::stdlib_docs
