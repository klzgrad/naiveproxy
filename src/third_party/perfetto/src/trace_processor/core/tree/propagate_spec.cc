/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/core/tree/propagate_spec.h"

#include <string>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto::trace_processor::core::tree {

base::StatusOr<PropagateSpec> ParsePropagateSpec(const std::string& spec) {
  std::string trimmed = base::TrimWhitespace(spec);
  if (trimmed.empty()) {
    return base::ErrStatus("propagate spec: empty string");
  }

  // Find '(' to extract agg function name.
  auto lparen = trimmed.find('(');
  if (lparen == std::string::npos) {
    return base::ErrStatus("propagate spec: expected '(' in '%s'",
                           trimmed.c_str());
  }
  std::string agg_str =
      base::ToUpper(base::TrimWhitespace(trimmed.substr(0, lparen)));

  PropagateAggOp agg_op;
  if (agg_str == "SUM") {
    agg_op = PropagateAggOp::kSum;
  } else if (agg_str == "MIN") {
    agg_op = PropagateAggOp::kMin;
  } else if (agg_str == "MAX") {
    agg_op = PropagateAggOp::kMax;
  } else if (agg_str == "FIRST") {
    agg_op = PropagateAggOp::kFirst;
  } else if (agg_str == "LAST") {
    agg_op = PropagateAggOp::kLast;
  } else {
    return base::ErrStatus("propagate spec: unknown aggregate '%s'",
                           agg_str.c_str());
  }

  // Find ')' to extract source column name.
  auto rparen = trimmed.find(')', lparen + 1);
  if (rparen == std::string::npos) {
    return base::ErrStatus("propagate spec: expected ')' in '%s'",
                           trimmed.c_str());
  }
  std::string source_col =
      base::TrimWhitespace(trimmed.substr(lparen + 1, rparen - lparen - 1));
  if (source_col.empty()) {
    return base::ErrStatus("propagate spec: empty source column in '%s'",
                           trimmed.c_str());
  }

  // Find 'AS' (case-insensitive) after ')'.
  std::string remainder = trimmed.substr(rparen + 1);
  std::string remainder_upper = base::ToUpper(remainder);
  auto as_pos = remainder_upper.find("AS");
  if (as_pos == std::string::npos) {
    return base::ErrStatus("propagate spec: expected 'AS' in '%s'",
                           trimmed.c_str());
  }

  // Verify that 'AS' is preceded and followed by whitespace or is at the
  // boundary.
  if (as_pos > 0 && remainder[as_pos - 1] != ' ' &&
      remainder[as_pos - 1] != '\t') {
    return base::ErrStatus(
        "propagate spec: expected whitespace before 'AS' in '%s'",
        trimmed.c_str());
  }
  if (as_pos + 2 < remainder.size() && remainder[as_pos + 2] != ' ' &&
      remainder[as_pos + 2] != '\t') {
    return base::ErrStatus(
        "propagate spec: expected whitespace after 'AS' in '%s'",
        trimmed.c_str());
  }

  std::string output_col = base::TrimWhitespace(remainder.substr(as_pos + 2));
  if (output_col.empty()) {
    return base::ErrStatus("propagate spec: empty output column in '%s'",
                           trimmed.c_str());
  }

  PropagateSpec result;
  result.agg_op = agg_op;
  result.source_col_name = std::move(source_col);
  result.output_col_name = std::move(output_col);
  return result;
}

}  // namespace perfetto::trace_processor::core::tree
