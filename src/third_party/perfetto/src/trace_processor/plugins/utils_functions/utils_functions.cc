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

#include "src/trace_processor/plugins/utils_functions/utils_functions.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/fnv_hash.h"
#include "perfetto/ext/base/regex.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/trace_processor/demangle.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/io.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/export_json.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/glob.h"

namespace perfetto::trace_processor {
namespace {

io::FileSystem* GetSqlFileSystem(TraceProcessorContext* context) {
  if (!context->config.enable_sql_file_access) {
    return nullptr;
  }
  return context->file_system;
}

class FileOutputWriter final : public json::OutputWriter {
 public:
  explicit FileOutputWriter(std::unique_ptr<io::File> file)
      : file_(std::move(file)) {}

  base::Status AppendString(const std::string& value) override {
    if (!status_.ok()) {
      return status_;
    }
    buffer_.append(value);
    if (buffer_.size() >= kBufferSize) {
      return Flush();
    }
    return status_;
  }

  base::Status Finish() { return Flush(); }

 private:
  base::Status Flush() {
    if (!status_.ok() || buffer_.empty()) {
      return status_;
    }
    status_ = file_->WriteAt(offset_, buffer_.data(), buffer_.size());
    if (status_.ok()) {
      offset_ += buffer_.size();
      buffer_.clear();
    }
    return status_;
  }

  static constexpr size_t kBufferSize = 64 * 1024;

  std::unique_ptr<io::File> file_;
  uint64_t offset_ = 0;
  std::string buffer_;
  base::Status status_ = base::OkStatus();
};

}  // namespace

struct ExportJson : public sqlite::Function<ExportJson> {
  static constexpr char kName[] = "export_json";
  static constexpr int kArgCount = 1;

  using UserData = TraceProcessorContext;
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void ExportJson::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  switch (sqlite::value::Type(argv[0])) {
    case sqlite::Type::kNull:
      return sqlite::utils::SetError(ctx,
                                     "EXPORT_JSON: filename cannot be null");
    case sqlite::Type::kText:
      break;
    case sqlite::Type::kInteger:
    case sqlite::Type::kFloat:
    case sqlite::Type::kBlob:
      return sqlite::utils::SetError(
          ctx, "EXPORT_JSON: argument must be a filename string");
  }

  auto* trace_context = GetUserData(ctx);
  io::FileSystem* file_system = GetSqlFileSystem(trace_context);
  if (!file_system) {
    return sqlite::utils::SetError(ctx, "EXPORT_JSON: File I/O is disabled");
  }
  io::FileOpenOptions options;
  options.access = io::FileAccess::kWriteOnly;
  options.create = true;
  options.truncate = true;
  std::unique_ptr<io::File> file;
  base::Status open_status =
      file_system->OpenFile(sqlite::value::Text(argv[0]), options, &file);
  if (!open_status.ok()) {
    return sqlite::utils::SetError(
        ctx, base::ErrStatus("EXPORT_JSON: %s", open_status.c_message()));
  }
  FileOutputWriter output(std::move(file));
  base::Status status = json::ExportJson(trace_context->storage.get(), &output);
  base::Status output_status = output.Finish();
  if (status.ok()) {
    status = std::move(output_status);
  }
  if (!status.ok()) {
    return sqlite::utils::SetError(ctx, status);
  }
  return sqlite::utils::ReturnNullFromFunction(ctx);
}

struct Hash : public sqlite::Function<Hash> {
  static constexpr char kName[] = "hash";
  static constexpr int kArgCount = -1;  // Variable arguments

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void Hash::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc >= 0);

  base::FnvHasher hash;
  for (int i = 0; i < argc; ++i) {
    sqlite3_value* value = argv[i];
    switch (sqlite::value::Type(value)) {
      case sqlite::Type::kInteger:
        hash.Update(sqlite::value::Int64(value));
        break;
      case sqlite::Type::kText: {
        const char* ptr = sqlite::value::Text(value);
        hash.Update(ptr, strlen(ptr));
        break;
      }
      case sqlite::Type::kNull:
      case sqlite::Type::kFloat:
      case sqlite::Type::kBlob:
        return sqlite::utils::SetError(
            ctx, base::ErrStatus("HASH: arg %d has unknown type", i));
    }
  }
  return sqlite::result::Long(ctx, static_cast<int64_t>(hash.digest()));
}

struct Reverse : public sqlite::Function<Reverse> {
  static constexpr char kName[] = "__intrinsic_reverse";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void Reverse::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  switch (sqlite::value::Type(argv[0])) {
    case sqlite::Type::kNull:
      return sqlite::utils::ReturnNullFromFunction(ctx);
    case sqlite::Type::kText: {
      const char* in = sqlite::value::Text(argv[0]);
      std::string_view in_str = in;
      std::string reversed(in_str.rbegin(), in_str.rend());

      return sqlite::result::TransientString(ctx, reversed.c_str(),
                                             static_cast<int>(reversed.size()));
    }
    case sqlite::Type::kInteger:
    case sqlite::Type::kFloat:
    case sqlite::Type::kBlob:
      return sqlite::utils::SetError(ctx, "REVERSE: argument should be string");
  }
}

struct Base64Encode : public sqlite::Function<Base64Encode> {
  static constexpr char kName[] = "__intrinsic_base64_encode";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void Base64Encode::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  switch (sqlite::value::Type(argv[0])) {
    case sqlite::Type::kNull:
      return sqlite::utils::ReturnNullFromFunction(ctx);
    case sqlite::Type::kBlob: {
      size_t byte_count = static_cast<size_t>(sqlite::value::Bytes(argv[0]));
      std::string res =
          base::Base64Encode(sqlite::value::Blob(argv[0]), byte_count);

      return sqlite::result::TransientString(ctx, res.c_str(),
                                             static_cast<int>(res.size()));
    }
    case sqlite::Type::kInteger:
    case sqlite::Type::kFloat:
    case sqlite::Type::kText:
      return sqlite::utils::SetError(
          ctx, "Base64Encode only supports bytes argument");
  }
}

struct Demangle : public sqlite::Function<Demangle> {
  static constexpr char kName[] = "__intrinsic_demangle";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void Demangle::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  switch (sqlite::value::Type(argv[0])) {
    case sqlite::Type::kNull:
      return sqlite::utils::ReturnNullFromFunction(ctx);
    case sqlite::Type::kText: {
      const char* mangled = sqlite::value::Text(argv[0]);
      std::unique_ptr<char, base::FreeDeleter> demangled =
          demangle::Demangle(mangled);
      if (!demangled) {
        return sqlite::utils::ReturnNullFromFunction(ctx);
      }
      int len = static_cast<int>(strlen(demangled.get()));
      return sqlite::result::RawString(ctx, demangled.release(), len, free);
    }
    case sqlite::Type::kInteger:
    case sqlite::Type::kFloat:
    case sqlite::Type::kBlob:
      return sqlite::utils::SetError(
          ctx, "Unsupported type of arg passed to DEMANGLE");
  }
}

struct FileWrite : public sqlite::Function<FileWrite> {
  static constexpr char kName[] = "__intrinsic_file_write";
  static constexpr int kArgCount = 2;

  using UserData = TraceProcessorContext;
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void FileWrite::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 2);

  if (sqlite::value::Type(argv[0]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(
        ctx, "__intrinsic_file_write: argument 1, filename must be string");
  }

  if (sqlite::value::Type(argv[1]) != sqlite::Type::kBlob) {
    return sqlite::utils::SetError(
        ctx, "__intrinsic_file_write: argument 2, content must be bytes");
  }

  io::FileSystem* file_system = GetSqlFileSystem(GetUserData(ctx));
  if (!file_system) {
    return sqlite::utils::SetError(
        ctx, "__intrinsic_file_write: File I/O is disabled");
  }

  io::FileOpenOptions options;
  options.access = io::FileAccess::kWriteOnly;
  options.create = true;
  options.truncate = true;
  std::unique_ptr<io::File> file;
  base::Status open_status =
      file_system->OpenFile(sqlite::value::Text(argv[0]), options, &file);
  if (!open_status.ok()) {
    return sqlite::utils::SetError(
        ctx,
        base::ErrStatus("__intrinsic_file_write: %s", open_status.c_message()));
  }

  int int_len = sqlite::value::Bytes(argv[1]);
  PERFETTO_CHECK(int_len >= 0);
  size_t len = static_cast<size_t>(int_len);
  // Make sure to call last as sqlite::value::Bytes can invalidate pointer
  // returned.
  const void* data = sqlite::value::Blob(argv[1]);
  base::Status status = file->WriteAt(0, data, len);
  if (!status.ok()) {
    return sqlite::utils::SetError(
        ctx, base::ErrStatus("__intrinsic_file_write: %s", status.c_message()));
  }

  return sqlite::result::Long(ctx, int_len);
}

struct TablePtrBind : public sqlite::Function<TablePtrBind> {
  static constexpr char kName[] = "__intrinsic_table_ptr_bind";
  static constexpr int kArgCount = -1;  // Variable arguments

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value**) {
    PERFETTO_DCHECK(argc >= 0);
    return sqlite::utils::SetError(
        ctx,
        "__intrinsic_table_ptr_bind should not be called from the global "
        "scope");
  }
};

struct Glob : public sqlite::Function<Glob> {
  static constexpr char kName[] = "glob";
  static constexpr int kArgCount = 2;

  using AuxData = util::GlobMatcher;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
    const char* text =
        reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));
    auto* aux = GetAuxData(ctx, 0);
    if (PERFETTO_UNLIKELY(!aux || !text)) {
      const char* pattern_str =
          reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
      if (!text || !pattern_str) {
        return;
      }
      auto ptr = std::make_unique<util::GlobMatcher>(
          util::GlobMatcher::FromPattern(pattern_str));
      aux = ptr.get();
      SetAuxData(ctx, 0, std::move(ptr));
    }
    return sqlite::result::Long(ctx, aux->Matches(text));
  }
};

struct RegexpAuxData {
  base::Regex regex;
  bool case_insensitive;
};

struct Regexp : public sqlite::Function<Regexp> {
  static constexpr char kName[] = "__intrinsic_regexp";
  static constexpr int kArgCount = 2;

  using AuxData = RegexpAuxData;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
    const char* input =
        reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));
    auto* aux = GetAuxData(ctx, 0);
    if (PERFETTO_UNLIKELY(!aux || !input)) {
      const char* pattern =
          reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
      if (!input || !pattern) {
        return;
      }
      SQLITE_ASSIGN_OR_RETURN(ctx, auto regex, base::Regex::Create(pattern));
      auto ptr = std::make_unique<AuxData>(
          AuxData{std::move(regex), /*case_insensitive=*/false});
      aux = ptr.get();
      SetAuxData(ctx, 0, std::move(ptr));
    }
    return sqlite::result::Long(ctx, aux->regex.PartialMatch(input));
  }
};

static base::StatusOr<bool> ParseRegexCaseInsensitive(const char* flags) {
  bool case_insensitive = false;
  for (const char* c = flags; *c; ++c) {
    if (*c == 'i') {
      case_insensitive = true;
    } else if (*c == 'c') {
      case_insensitive = false;
    } else {
      return base::ErrStatus("regexp: unknown flag '%c'", *c);
    }
  }
  return case_insensitive;
}

struct RegexpWithFlags : public sqlite::Function<RegexpWithFlags> {
  static constexpr char kName[] = "__intrinsic_regexp_with_flags";
  static constexpr int kArgCount = 3;

  using AuxData = RegexpAuxData;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
    const char* pattern =
        reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
    const char* input =
        reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));
    const char* flags =
        reinterpret_cast<const char*>(sqlite3_value_text(argv[2]));
    if (!input || !pattern || !flags) {
      return;
    }
    SQLITE_ASSIGN_OR_RETURN(ctx, bool case_insensitive,
                            ParseRegexCaseInsensitive(flags));
    auto* aux = GetAuxData(ctx, 0);
    if (PERFETTO_UNLIKELY(!aux || aux->case_insensitive != case_insensitive)) {
      auto case_sensitivity = case_insensitive
                                  ? base::Regex::CaseSensitivity::kInsensitive
                                  : base::Regex::CaseSensitivity::kSensitive;
      SQLITE_ASSIGN_OR_RETURN(ctx, auto regex,
                              base::Regex::Create(pattern, case_sensitivity));
      auto ptr = std::make_unique<AuxData>(
          AuxData{std::move(regex), case_insensitive});
      aux = ptr.get();
      SetAuxData(ctx, 0, std::move(ptr));
    }
    return sqlite::result::Long(ctx, aux->regex.PartialMatch(input));
  }
};

struct RegexpExtract : public sqlite::Function<RegexpExtract> {
  static constexpr char kName[] = "__intrinsic_regexp_extract";
  static constexpr int kArgCount = 2;

  struct AuxData {
    base::Regex regex;
    std::vector<std::string_view> matches;
  };

  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
    const char* text =
        reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
    auto* aux = GetAuxData(ctx, 1);
    if (PERFETTO_UNLIKELY(!aux || !text)) {
      const char* pattern_str =
          reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));
      if (!text || !pattern_str) {
        return;
      }
      SQLITE_ASSIGN_OR_RETURN(ctx, auto regex,
                              base::Regex::Create(pattern_str));
      auto ptr = std::make_unique<AuxData>(AuxData{std::move(regex), {}});
      aux = ptr.get();
      SetAuxData(ctx, 1, std::move(ptr));
    }

    aux->regex.PartialMatchWithGroups(text, aux->matches);
    if (PERFETTO_UNLIKELY(aux->matches.empty())) {
      return;
    }

    // As per re_nsub, groups[0] is the full match. groups[1] is the first
    // subexpression.
    if (PERFETTO_UNLIKELY(aux->matches.size() > 2)) {
      return sqlite::utils::SetError(
          ctx, "REGEXP_EXTRACT: pattern has more than one group.");
    }

    std::string_view result_sv;
    if (aux->matches.size() == 2 && !aux->matches[1].empty()) {
      // One group, and it matched.
      result_sv = aux->matches[1];
    } else {
      // No groups, or optional group did not match. Return full match.
      result_sv = aux->matches[0];
    }
    return sqlite::result::TransientString(ctx, result_sv.data(),
                                           static_cast<int>(result_sv.size()));
  }
};

struct RegexpReplaceSimple : public sqlite::Function<RegexpReplaceSimple> {
  static constexpr char kName[] = "__intrinsic_regexp_replace_simple";
  static constexpr int kArgCount = 3;

  using AuxData = base::Regex;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
    const char* text =
        reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
    const char* replacement =
        reinterpret_cast<const char*>(sqlite3_value_text(argv[2]));
    auto* aux = GetAuxData(ctx, 1);
    if (PERFETTO_UNLIKELY(!aux || !text || !replacement)) {
      const char* pattern_str =
          reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));
      if (!text || !pattern_str || !replacement) {
        return;
      }
      SQLITE_ASSIGN_OR_RETURN(ctx, auto regex,
                              base::Regex::Create(pattern_str));
      auto ptr = std::make_unique<AuxData>(std::move(regex));
      aux = ptr.get();
      SetAuxData(ctx, 0, std::move(ptr));
    }

    // TODO(sashwinbalaji): ideally GlobalReplace should return
    // std::unique_ptr<char[]> to avoid the copy into TransientString below.
    const std::string result = aux->GlobalReplace(text, replacement);

    return sqlite::result::TransientString(ctx, result.data(),
                                           static_cast<int>(result.size()));
  }
};

struct UnHex : public sqlite::Function<UnHex> {
  static constexpr char kName[] = "__intrinsic_unhex";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
    sqlite::Type type = sqlite::value::Type(argv[0]);

    if (type == sqlite::Type::kNull) {
      return sqlite::result::Null(ctx);
    }
    if (type != sqlite::Type::kText) {
      return sqlite::result::Error(ctx, "UNHEX: argument must be text");
    }

    std::string_view hex_str(sqlite::value::Text(argv[0]));

    // Trim leading and trailing whitespace
    size_t first = hex_str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string_view::npos) {
      return sqlite::result::Error(ctx,
                                   "UNHEX: input is empty or only whitespace");
    }
    size_t last = hex_str.find_last_not_of(" \t\n\r\f\v");
    hex_str = hex_str.substr(first, (last - first + 1));

    // Handle optional "0x" or "0X" prefix
    if (hex_str.length() >= 2 && hex_str[0] == '0' &&
        (hex_str[1] == 'x' || hex_str[1] == 'X')) {
      hex_str.remove_prefix(2);
    }

    if (hex_str.empty()) {
      return sqlite::result::Error(ctx,
                                   "UNHEX: hex string is empty after prefix");
    }

    std::optional<int64_t> result =
        perfetto::base::StringViewToInt64(hex_str, 16);

    if (!result.has_value()) {
      return sqlite::result::Error(ctx,
                                   "UNHEX: invalid or out of range hex string");
    }

    return sqlite::result::Long(ctx, *result);
  }
};

}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::utils_functions {
namespace {

class UtilsFunctionsPlugin : public Plugin<UtilsFunctionsPlugin> {
 public:
  ~UtilsFunctionsPlugin() override;

  void RegisterFunctions(PerfettoSqlConnection*,
                         std::vector<FunctionRegistration>& out) override {
    out.push_back(MakeFunctionRegistration<ExportJson>(trace_context_));
    out.push_back(MakeFunctionRegistration<Hash>(nullptr));
    out.push_back(MakeFunctionRegistration<Reverse>(nullptr));
    out.push_back(MakeFunctionRegistration<Base64Encode>(nullptr));
    out.push_back(MakeFunctionRegistration<Demangle>(nullptr));
    out.push_back(MakeFunctionRegistration<TablePtrBind>(nullptr));
    out.push_back(MakeFunctionRegistration<Glob>(nullptr));
    out.push_back(MakeFunctionRegistration<Regexp>(nullptr));
    out.push_back(MakeFunctionRegistration<RegexpWithFlags>(nullptr));
    out.push_back(MakeFunctionRegistration<RegexpExtract>(nullptr));
    out.push_back(MakeFunctionRegistration<RegexpReplaceSimple>(nullptr));
    out.push_back(MakeFunctionRegistration<UnHex>(nullptr));
    out.push_back(MakeFunctionRegistration<FileWrite>(trace_context_));
  }
};

UtilsFunctionsPlugin::~UtilsFunctionsPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<UtilsFunctionsPlugin>();
      },
      UtilsFunctionsPlugin::kPluginId, UtilsFunctionsPlugin::kDepIds.data(),
      UtilsFunctionsPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::utils_functions
