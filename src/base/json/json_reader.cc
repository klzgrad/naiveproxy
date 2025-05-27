// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/json/json_reader.h"

#include <string_view>
#include <utility>

#include "base/features.h"
#include "base/json/json_parser.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "build/build_config.h"

#if !BUILDFLAG(IS_NACL)
#include "base/strings/string_view_rust.h"
#include "third_party/rust/serde_json_lenient/v0_2/wrapper/functions.h"
#include "third_party/rust/serde_json_lenient/v0_2/wrapper/lib.rs.h"
#endif

// TODO(crbug.com/40811643): Move the C++ parser into components/nacl to just
// run in-process there. Don't compile base::JSONReader on NaCL at all.
#if !BUILDFLAG(IS_NACL)

namespace {
const char kSecurityJsonParsingTime[] = "Security.JSONParser.ParsingTime";
}  // namespace

// This namespace defines FFI-friendly functions that are be called from Rust in
// //third_party/rust/serde_json_lenient/v0_2/wrapper/.
namespace serde_json_lenient {

base::Value::List& list_append_list(base::Value::List& ctx) {
  ctx.Append(base::Value::List());
  return ctx.back().GetList();
}

base::Value::Dict& list_append_dict(base::Value::List& ctx) {
  ctx.Append(base::Value::Dict());
  return ctx.back().GetDict();
}

void list_append_none(base::Value::List& ctx) {
  ctx.Append(base::Value());
}

void list_append_bool(base::Value::List& ctx, bool val) {
  ctx.Append(val);
}

void list_append_i32(base::Value::List& ctx, int32_t val) {
  ctx.Append(val);
}

void list_append_f64(base::Value::List& ctx, double val) {
  ctx.Append(val);
}

void list_append_str(base::Value::List& ctx, rust::Str val) {
  ctx.Append(std::string(val));
}

base::Value::List& dict_set_list(base::Value::Dict& ctx, rust::Str key) {
  base::Value* value =
      ctx.Set(base::RustStrToStringView(key), base::Value::List());
  return value->GetList();
}

base::Value::Dict& dict_set_dict(base::Value::Dict& ctx, rust::Str key) {
  base::Value* value =
      ctx.Set(base::RustStrToStringView(key), base::Value::Dict());
  return value->GetDict();
}

void dict_set_none(base::Value::Dict& ctx, rust::Str key) {
  ctx.Set(base::RustStrToStringView(key), base::Value());
}

void dict_set_bool(base::Value::Dict& ctx, rust::Str key, bool val) {
  ctx.Set(base::RustStrToStringView(key), val);
}

void dict_set_i32(base::Value::Dict& ctx, rust::Str key, int32_t val) {
  ctx.Set(base::RustStrToStringView(key), val);
}

void dict_set_f64(base::Value::Dict& ctx, rust::Str key, double val) {
  ctx.Set(base::RustStrToStringView(key), val);
}

void dict_set_str(base::Value::Dict& ctx, rust::Str key, rust::Str val) {
  ctx.Set(base::RustStrToStringView(key), std::string(val));
}

namespace {

base::JSONReader::Result DecodeJSONInRust(std::string_view json,
                                          int options,
                                          size_t max_depth) {
  const JsonOptions rust_options = {
      .allow_trailing_commas =
          (options & base::JSON_ALLOW_TRAILING_COMMAS) != 0,
      .replace_invalid_characters =
          (options & base::JSON_REPLACE_INVALID_CHARACTERS) != 0,
      .allow_comments = (options & base::JSON_ALLOW_COMMENTS) != 0,
      .allow_newlines = (options & base::JSON_ALLOW_NEWLINES_IN_STRINGS) != 0,
      .allow_vert_tab = (options & base::JSON_ALLOW_VERT_TAB) != 0,
      .allow_x_escapes = (options & base::JSON_ALLOW_X_ESCAPES) != 0,
      .max_depth = max_depth,
  };

  base::Value::List list;
  DecodeError error;
  bool ok =
      decode_json(base::StringViewToRustSlice(json), rust_options, list, error);

  if (!ok) {
    return base::unexpected(base::JSONReader::Error{
        .message = std::string(error.message),
        .line = error.line,
        .column = error.column,
    });
  }

  return std::move(list.back());
}

}  // namespace
}  // namespace serde_json_lenient

#endif  // !BUILDFLAG(IS_NACL)

namespace base {

// static
std::optional<Value> JSONReader::Read(std::string_view json,
                                      int options,
                                      size_t max_depth) {
#if BUILDFLAG(IS_NACL)
  internal::JSONParser parser(options, max_depth);
  return parser.Parse(json);
#else   // BUILDFLAG(IS_NACL)
  SCOPED_UMA_HISTOGRAM_TIMER_MICROS(kSecurityJsonParsingTime);
  if (UsingRust()) {
    JSONReader::Result result =
        serde_json_lenient::DecodeJSONInRust(json, options, max_depth);
    if (!result.has_value()) {
      return std::nullopt;
    }
    return std::move(*result);
  } else {
    internal::JSONParser parser(options, max_depth);
    return parser.Parse(json);
  }
#endif  // BUILDFLAG(IS_NACL)
}

// static
std::optional<Value::Dict> JSONReader::ReadDict(std::string_view json,
                                                int options,
                                                size_t max_depth) {
  std::optional<Value> value = Read(json, options, max_depth);
  if (!value || !value->is_dict()) {
    return std::nullopt;
  }
  return std::move(*value).TakeDict();
}

// static
std::optional<Value::List> JSONReader::ReadList(std::string_view json,
                                                int options,
                                                size_t max_depth) {
  std::optional<Value> value = Read(json, options, max_depth);
  if (!value || !value->is_list()) {
    return std::nullopt;
  }
  return std::move(*value).TakeList();
}

// static
JSONReader::Result JSONReader::ReadAndReturnValueWithError(
    std::string_view json,
    int options) {
#if BUILDFLAG(IS_NACL)
  internal::JSONParser parser(options);
  auto value = parser.Parse(json);
  if (!value) {
    Error error;
    error.message = parser.GetErrorMessage();
    error.line = parser.error_line();
    error.column = parser.error_column();
    return base::unexpected(std::move(error));
  }

  return std::move(*value);
#else   // BUILDFLAG(IS_NACL)
  SCOPED_UMA_HISTOGRAM_TIMER_MICROS(kSecurityJsonParsingTime);
  if (UsingRust()) {
    return serde_json_lenient::DecodeJSONInRust(json, options,
                                                internal::kAbsoluteMaxDepth);
  } else {
    internal::JSONParser parser(options);
    auto value = parser.Parse(json);
    if (!value) {
      Error error;
      error.message = parser.GetErrorMessage();
      error.line = parser.error_line();
      error.column = parser.error_column();
      return base::unexpected(std::move(error));
    }

    return std::move(*value);
  }
#endif  // BUILDFLAG(IS_NACL)
}

// static
bool JSONReader::UsingRust() {
  // If features have not yet been enabled, we cannot check the feature, so fall
  // back to the C++ parser. In practice, this seems to apply to
  // `ReadPrefsFromDisk()`, which is parsing trusted JSON.
  if (!base::FeatureList::GetInstance()) {
    return false;
  }
#if BUILDFLAG(IS_NACL)
  return false;
#else
  return base::FeatureList::IsEnabled(base::features::kUseRustJsonParser);
#endif
}

}  // namespace base
