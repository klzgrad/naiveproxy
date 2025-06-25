// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/json/json_reader.h"

#include <string_view>
#include <utility>

#include "base/json/json_parser.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"


namespace base {

std::string JSONReader::Error::ToString() const {
  return base::StrCat({"line ", base::NumberToString(line), ", column ",
                       base::NumberToString(column), ": ", message});
}

// static
std::optional<Value> JSONReader::Read(std::string_view json,
                                      int options,
                                      size_t max_depth) {
  internal::JSONParser parser(options, max_depth);
  return parser.Parse(json);
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

}  // namespace base
