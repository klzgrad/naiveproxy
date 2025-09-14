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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_UTILS_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_UTILS_H_

#include <sqlite3.h>
#include <limits>
#include <string>
#include <vector>

#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/trace_processor/demangle.h"
#include "src/trace_processor/export_json.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/sql_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/util/glob.h"
#include "src/trace_processor/util/regex.h"

namespace perfetto {
namespace trace_processor {

struct ExportJson : public LegacySqlFunction {
  using Context = TraceStorage;
  static base::Status Run(TraceStorage* storage,
                          size_t /*argc*/,
                          sqlite3_value** argv,
                          SqlValue& /*out*/,
                          Destructors&);
};

base::Status ExportJson::Run(TraceStorage* storage,
                             size_t /*argc*/,
                             sqlite3_value** argv,
                             SqlValue& /*out*/,
                             Destructors&) {
  base::ScopedFstream output;
  if (sqlite3_value_type(argv[0]) == SQLITE_INTEGER) {
    // Assume input is an FD.
    output.reset(fdopen(sqlite3_value_int(argv[0]), "w"));
    if (!output) {
      return base::ErrStatus(
          "EXPORT_JSON: Couldn't open output file from given FD");
    }
  } else {
    const char* filename =
        reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
    output = base::OpenFstream(filename, "w");
    if (!output) {
      return base::ErrStatus("EXPORT_JSON: Couldn't open output file");
    }
  }
  return json::ExportJson(storage, output.get());
}

struct Hash : public LegacySqlFunction {
  static base::Status Run(void*,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors&);
};

base::Status Hash::Run(void*,
                       size_t argc,
                       sqlite3_value** argv,
                       SqlValue& out,
                       Destructors&) {
  base::FnvHasher hash;
  for (size_t i = 0; i < argc; ++i) {
    sqlite3_value* value = argv[i];
    int type = sqlite3_value_type(value);
    switch (type) {
      case SQLITE_INTEGER:
        hash.Update(sqlite3_value_int64(value));
        break;
      case SQLITE_TEXT: {
        const char* ptr =
            reinterpret_cast<const char*>(sqlite3_value_text(value));
        hash.Update(ptr, strlen(ptr));
        break;
      }
      default:
        return base::ErrStatus("HASH: arg %zu has unknown type %d", i, type);
    }
  }
  out = SqlValue::Long(static_cast<int64_t>(hash.digest()));
  return base::OkStatus();
}

struct Reverse : public LegacySqlFunction {
  static base::Status Run(void*,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors& destructors);
};

base::Status Reverse::Run(void*,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors& destructors) {
  if (argc != 1) {
    return base::ErrStatus("REVERSE: expected one arg but got %zu", argc);
  }

  // If the string is null, just return null as the result.
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    return base::OkStatus();
  }
  if (sqlite3_value_type(argv[0]) != SQLITE_TEXT) {
    return base::ErrStatus("REVERSE: argument should be string");
  }

  const char* in = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
  std::string_view in_str = in;
  std::string reversed(in_str.rbegin(), in_str.rend());

  std::unique_ptr<char, base::FreeDeleter> s(
      static_cast<char*>(malloc(reversed.size() + 1)));
  memcpy(s.get(), reversed.c_str(), reversed.size() + 1);

  destructors.string_destructor = free;
  out = SqlValue::String(s.release());
  return base::OkStatus();
}

struct Base64Encode : public LegacySqlFunction {
  static base::Status Run(void*,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors&);
};

base::Status Base64Encode::Run(void*,
                               size_t argc,
                               sqlite3_value** argv,
                               SqlValue& out,
                               Destructors& destructors) {
  if (argc != 1)
    return base::ErrStatus("Unsupported number of arg passed to Base64Encode");

  sqlite3_value* value = argv[0];
  if (sqlite3_value_type(value) != SQLITE_BLOB)
    return base::ErrStatus("Base64Encode only supports bytes argument");

  size_t byte_count = static_cast<size_t>(sqlite3_value_bytes(value));
  std::string res = base::Base64Encode(sqlite3_value_blob(value), byte_count);

  std::unique_ptr<char, base::FreeDeleter> s(
      static_cast<char*>(malloc(res.size() + 1)));
  memcpy(s.get(), res.c_str(), res.size() + 1);

  out = SqlValue::String(s.release());
  destructors.string_destructor = free;

  return base::OkStatus();
}

struct Demangle : public LegacySqlFunction {
  static base::Status Run(void*,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors& destructors);
};

base::Status Demangle::Run(void*,
                           size_t argc,
                           sqlite3_value** argv,
                           SqlValue& out,
                           Destructors& destructors) {
  if (argc != 1)
    return base::ErrStatus("Unsupported number of arg passed to DEMANGLE");
  sqlite3_value* value = argv[0];
  if (sqlite3_value_type(value) == SQLITE_NULL)
    return base::OkStatus();

  if (sqlite3_value_type(value) != SQLITE_TEXT)
    return base::ErrStatus("Unsupported type of arg passed to DEMANGLE");

  const char* mangled =
      reinterpret_cast<const char*>(sqlite3_value_text(value));

  std::unique_ptr<char, base::FreeDeleter> demangled =
      demangle::Demangle(mangled);
  if (!demangled)
    return base::OkStatus();

  destructors.string_destructor = free;
  out = SqlValue::String(demangled.release());
  return base::OkStatus();
}

struct WriteFile : public LegacySqlFunction {
  using Context = TraceStorage;
  static base::Status Run(TraceStorage* storage,
                          size_t,
                          sqlite3_value** argv,
                          SqlValue&,
                          Destructors&);
};

base::Status WriteFile::Run(TraceStorage*,
                            size_t argc,
                            sqlite3_value** argv,
                            SqlValue& out,
                            Destructors&) {
  if (argc != 2) {
    return base::ErrStatus("WRITE_FILE: expected %d args but got %zu", 2, argc);
  }

  base::Status status =
      sqlite::utils::TypeCheckSqliteValue(argv[0], SqlValue::kString);
  if (!status.ok()) {
    return base::ErrStatus("WRITE_FILE: argument 1, filename; %s",
                           status.c_message());
  }

  status = sqlite::utils::TypeCheckSqliteValue(argv[1], SqlValue::kBytes);
  if (!status.ok()) {
    return base::ErrStatus("WRITE_FILE: argument 2, content; %s",
                           status.c_message());
  }

  const std::string filename =
      reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));

  base::ScopedFstream file = base::OpenFstream(filename.c_str(), "wb");
  if (!file) {
    return base::ErrStatus("WRITE_FILE: Couldn't open output file %s (%s)",
                           filename.c_str(), strerror(errno));
  }

  int int_len = sqlite3_value_bytes(argv[1]);
  PERFETTO_CHECK(int_len >= 0);
  size_t len = (static_cast<size_t>(int_len));
  // Make sure to call last as sqlite3_value_bytes can invalidate pointer
  // returned.
  const void* data = sqlite3_value_text(argv[1]);
  if (fwrite(data, 1, len, file.get()) != len || fflush(file.get()) != 0) {
    return base::ErrStatus("WRITE_FILE: Failed to write to file %s (%s)",
                           filename.c_str(), strerror(errno));
  }

  out = SqlValue::Long(int_len);

  return base::OkStatus();
}

struct ExtractArg : public sqlite::Function<ExtractArg> {
  static constexpr char kName[] = "extract_arg";
  static constexpr int kArgCount = 2;

  using UserData = TraceStorage;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv);
};

void ExtractArg::Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
  sqlite::Type arg_set_value = sqlite::value::Type(argv[0]);
  sqlite::Type key_value = sqlite::value::Type(argv[1]);

  // If the arg set id is null, just return null as the result.
  if (arg_set_value == sqlite::Type::kNull) {
    return;
  }

  if (arg_set_value != sqlite::Type::kInteger) {
    return sqlite::result::Error(
        ctx, "EXTRACT_ARG: 1st argument should be arg set id");
  }

  if (key_value != sqlite::Type::kText) {
    return sqlite::result::Error(ctx,
                                 "EXTRACT_ARG: 2nd argument should be key");
  }

  uint32_t arg_set_id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
  const char* key = reinterpret_cast<const char*>(sqlite::value::Text(argv[1]));

  auto* storage = GetUserData(ctx);
  uint32_t row = storage->ExtractArgRowFast(arg_set_id, key);
  if (row == std::numeric_limits<uint32_t>::max()) {
    return;
  }
  auto rr = storage->arg_table()[row];
  switch (*storage->GetVariadicTypeForId(rr.value_type())) {
    case Variadic::Type::kBool:
    case Variadic::Type::kInt:
    case Variadic::Type::kUint:
    case Variadic::Type::kPointer:
      return sqlite::result::Long(ctx, *rr.int_value());
    case Variadic::Type::kJson:
    case Variadic::Type::kString:
      return sqlite::result::StaticString(
          ctx, storage->GetString(rr.string_value()).c_str());
    case Variadic::Type::kReal:
      return sqlite::result::Double(ctx, *rr.real_value());
    case Variadic::Type::kNull:
      return;
  }
}

struct SourceGeq : public LegacySqlFunction {
  static base::Status Run(void*,
                          size_t,
                          sqlite3_value**,
                          SqlValue&,
                          Destructors&) {
    return base::ErrStatus(
        "SOURCE_GEQ should not be called from the global scope");
  }
};

struct TablePtrBind : public LegacySqlFunction {
  static base::Status Run(void*,
                          size_t,
                          sqlite3_value**,
                          SqlValue&,
                          Destructors&) {
    return base::ErrStatus(
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

struct Regexp : public sqlite::Function<Regexp> {
  static constexpr char kName[] = "regexp";
  static constexpr int kArgCount = 2;

  using AuxData = regex::Regex;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
    if constexpr (regex::IsRegexSupported()) {
      const char* text =
          reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));
      auto* aux = GetAuxData(ctx, 0);
      if (PERFETTO_UNLIKELY(!aux || !text)) {
        const char* pattern_str =
            reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
        if (!text || !pattern_str) {
          return;
        }
        SQLITE_ASSIGN_OR_RETURN(ctx, auto regex,
                                regex::Regex::Create(pattern_str));
        auto ptr = std::make_unique<AuxData>(std::move(regex));
        aux = ptr.get();
        SetAuxData(ctx, 0, std::move(ptr));
      }
      return sqlite::result::Long(ctx, aux->Search(text));
    } else {
      PERFETTO_FATAL("Regex not supported");
    }
  }
};

struct RegexpExtract : public sqlite::Function<RegexpExtract> {
  static constexpr char kName[] = "regexp_extract";
  static constexpr int kArgCount = 2;

  struct AuxData {
    regex::Regex regex;
    std::vector<std::string_view> matches;
  };

  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
    if constexpr (regex::IsRegexSupported()) {
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
                                regex::Regex::Create(pattern_str));
        auto ptr = std::make_unique<AuxData>(AuxData{std::move(regex), {}});
        aux = ptr.get();
        SetAuxData(ctx, 1, std::move(ptr));
      }

      aux->regex.Submatch(text, aux->matches);
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
      return sqlite::result::TransientString(
          ctx, result_sv.data(), static_cast<int>(result_sv.size()));
    } else {
      PERFETTO_FATAL("Regex not supported");
    }
  }
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_UTILS_H_
