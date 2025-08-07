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

#include "src/trace_processor/util/sql_argument.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/basic_types.h"

namespace perfetto::trace_processor::sql_argument {

bool IsValidName(base::StringView str) {
  if (str.empty()) {
    return false;
  }
  auto pred = [](char c) { return !(isalnum(c) || c == '_'); };
  return std::find_if(str.begin(), str.end(), pred) == str.end();
}

std::optional<Type> ParseType(base::StringView str) {
  if (str.CaseInsensitiveEq("bool")) {
    return Type::kBool;
  }
  if (str.CaseInsensitiveOneOf({"long", "timestamp", "duration", "id", "joinid",
                                "argsetid", "int", "uint"})) {
    return Type::kLong;
  }
  if (str.CaseInsensitiveOneOf({"double", "float"})) {
    return Type::kDouble;
  }
  if (str.CaseInsensitiveEq("string")) {
    return Type::kString;
  }
  if (str.CaseInsensitiveOneOf({"bytes", "proto"})) {
    return Type::kBytes;
  }
  return std::nullopt;
}

const char* TypeToHumanFriendlyString(sql_argument::Type type) {
  using Type = sql_argument::Type;
  switch (type) {
    case Type::kBool:
      return "BOOL";
    case Type::kLong:
      return "LONG";
    case Type::kDouble:
      return "DOUBLE";
    case Type::kString:
      return "STRING";
    case Type::kBytes:
      return "BYTES";
  }
  PERFETTO_FATAL("For GCC");
}

SqlValue::Type TypeToSqlValueType(sql_argument::Type type) {
  using Type = sql_argument::Type;
  switch (type) {
    case Type::kBool:
    case Type::kLong:
      return SqlValue::kLong;
    case Type::kDouble:
      return SqlValue::kDouble;
    case Type::kString:
      return SqlValue::kString;
    case Type::kBytes:
      return SqlValue::kBytes;
  }
  PERFETTO_FATAL("For GCC");
}

base::Status ParseArgumentDefinitions(const std::string& args,
                                      std::vector<ArgumentDefinition>& out) {
  std::string trimmed_args = base::TrimWhitespace(args);
  for (const auto& arg : base::SplitString(trimmed_args, ",")) {
    const auto& arg_name_and_type =
        (base::SplitString(base::TrimWhitespace(arg), " "));
    if (arg_name_and_type.size() != 2) {
      return base::ErrStatus(
          "argument '%s' in function prototype should be of the form `name "
          "TYPE`",
          arg.c_str());
    }

    const auto& arg_name = arg_name_and_type[0];
    const auto& arg_type_str = arg_name_and_type[1];
    if (!IsValidName(base::StringView(arg_name)))
      return base::ErrStatus("argument '%s' is not alphanumeric", arg.c_str());

    auto opt_arg_type = ParseType(base::StringView(arg_type_str));
    if (!opt_arg_type) {
      return base::ErrStatus("unknown argument type in argument '%s'",
                             arg.c_str());
    }
    out.emplace_back("$" + arg_name, *opt_arg_type);
  }
  return base::OkStatus();
}

std::string SerializeArguments(const std::vector<ArgumentDefinition>& args) {
  bool comma = false;
  std::string serialized;
  for (const auto& arg : args) {
    if (comma) {
      serialized.append(", ");
    }
    comma = true;
    serialized.append(arg.name().c_str());
    serialized.push_back(' ');
    serialized.append(TypeToHumanFriendlyString(arg.type()));
  }
  return serialized;
}

}  // namespace perfetto::trace_processor::sql_argument
