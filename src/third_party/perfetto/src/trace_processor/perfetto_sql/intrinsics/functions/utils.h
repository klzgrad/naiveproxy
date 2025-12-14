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

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/fnv_hash.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/trace_processor/demangle.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/export_json.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/glob.h"
#include "src/trace_processor/util/regex.h"

namespace perfetto::trace_processor {

struct ExportJson : public sqlite::Function<ExportJson> {
  static constexpr char kName[] = "export_json";
  static constexpr int kArgCount = 1;

  using UserData = TraceStorage;
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void ExportJson::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  auto* storage = GetUserData(ctx);
  base::ScopedFstream output;

  switch (sqlite::value::Type(argv[0])) {
    case sqlite::Type::kNull:
      return sqlite::utils::SetError(ctx,
                                     "EXPORT_JSON: filename cannot be null");
    case sqlite::Type::kInteger: {
      // Assume input is an FD.
      int fd = static_cast<int>(sqlite::value::Int64(argv[0]));
      output.reset(fdopen(fd, "w"));
      if (!output) {
        return sqlite::utils::SetError(
            ctx, "EXPORT_JSON: Couldn't open output file from given FD");
      }
      break;
    }
    case sqlite::Type::kText: {
      const char* filename = sqlite::value::Text(argv[0]);
      output = base::OpenFstream(filename, "w");
      if (!output) {
        return sqlite::utils::SetError(
            ctx, "EXPORT_JSON: Couldn't open output file");
      }
      break;
    }
    case sqlite::Type::kFloat:
    case sqlite::Type::kBlob:
      return sqlite::utils::SetError(
          ctx,
          "EXPORT_JSON: argument must be filename string or file descriptor");
  }

  auto status = json::ExportJson(storage, output.get());
  if (!status.ok()) {
    return sqlite::utils::SetError(ctx, status);
  }

  // ExportJson returns no value (void function)
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
  static constexpr char kName[] = "reverse";
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
  static constexpr char kName[] = "base64_encode";
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
  static constexpr char kName[] = "demangle";
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

struct WriteFile : public sqlite::Function<WriteFile> {
  static constexpr char kName[] = "write_file";
  static constexpr int kArgCount = 2;

  using UserData = TraceStorage;
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void WriteFile::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 2);

  if (sqlite::value::Type(argv[0]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(
        ctx, "WRITE_FILE: argument 1, filename must be string");
  }

  if (sqlite::value::Type(argv[1]) != sqlite::Type::kBlob) {
    return sqlite::utils::SetError(
        ctx, "WRITE_FILE: argument 2, content must be bytes");
  }

  const char* filename = sqlite::value::Text(argv[0]);
  base::ScopedFstream file = base::OpenFstream(filename, "wb");
  if (!file) {
    return sqlite::utils::SetError(
        ctx, base::ErrStatus("WRITE_FILE: Couldn't open output file %s (%s)",
                             filename, strerror(errno)));
  }

  int int_len = sqlite::value::Bytes(argv[1]);
  PERFETTO_CHECK(int_len >= 0);
  size_t len = static_cast<size_t>(int_len);
  // Make sure to call last as sqlite::value::Bytes can invalidate pointer
  // returned.
  const void* data = sqlite::value::Blob(argv[1]);
  if (fwrite(data, 1, len, file.get()) != len || fflush(file.get()) != 0) {
    return sqlite::utils::SetError(
        ctx, base::ErrStatus("WRITE_FILE: Failed to write to file %s (%s)",
                             filename, strerror(errno)));
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
      // Always-true branch to avoid spurious no-return warnings.
      if (ctx) {
        PERFETTO_FATAL("Regex not supported");
      }
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
      // Always-true branch to avoid spurious no-return warnings.
      if (ctx) {
        PERFETTO_FATAL("Regex not supported");
      }
    }
  }
};

struct UnHex : public sqlite::Function<UnHex> {
  static constexpr char kName[] = "UNHEX";
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

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_UTILS_H_
