// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/output_conversion.h"

#include "tools/gn/settings.h"
#include "tools/gn/value.h"

namespace {

void ToString(const Value& output, std::ostream& out) {
  out << output.ToString(false);
}

void ToStringQuoted(const Value& output, std::ostream& out) {
  out << "\"" << output.ToString(false) << "\"";
}

void Indent(int indent, std::ostream& out) {
  for (int i = 0; i < indent; ++i)
    out << "  ";
}

// Forward declare so it can be used recursively.
void RenderScopeToJSON(const Value& output, std::ostream& out, int indent);

void RenderListToJSON(const Value& output, std::ostream& out, int indent) {
  assert(indent > 0);
  bool first = true;
  out << "[\n";
  for (const auto& value : output.list_value()) {
    if (!first)
      out << ",\n";
    Indent(indent, out);
    if (value.type() == Value::SCOPE)
      RenderScopeToJSON(value, out, indent + 1);
    else if (value.type() == Value::LIST)
      RenderListToJSON(value, out, indent + 1);
    else
      out << value.ToString(true);
    first = false;
  }
  out << "\n";
  Indent(indent - 1, out);
  out << "]";
}

void RenderScopeToJSON(const Value& output, std::ostream& out, int indent) {
  assert(indent > 0);
  Scope::KeyValueMap scope_values;
  output.scope_value()->GetCurrentScopeValues(&scope_values);
  bool first = true;
  out << "{\n";
  for (const auto& pair : scope_values) {
    if (!first)
      out << ",\n";
    Indent(indent, out);
    out << "\"" << pair.first.as_string() << "\": ";
    if (pair.second.type() == Value::SCOPE)
      RenderScopeToJSON(pair.second, out, indent + 1);
    else if (pair.second.type() == Value::LIST)
      RenderListToJSON(pair.second, out, indent + 1);
    else
      out << pair.second.ToString(true);
    first = false;
  }
  out << "\n";
  Indent(indent - 1, out);
  out << "}";
}

void OutputListLines(const Value& output, std::ostream& out) {
  assert(output.type() == Value::LIST);
  const std::vector<Value>& list = output.list_value();
  for (const auto& cur : list)
    out << cur.ToString(false) << "\n";
}

void OutputString(const Value& output, std::ostream& out) {
  if (output.type() == Value::NONE)
    return;
  if (output.type() == Value::STRING) {
    ToString(output, out);
    return;
  }
  ToStringQuoted(output, out);
}

void OutputValue(const Value& output, std::ostream& out) {
  if (output.type() == Value::NONE)
    return;
  if (output.type() == Value::STRING) {
    ToStringQuoted(output, out);
    return;
  }
  ToString(output, out);
}

// The direct Value::ToString call wraps the scope in '{}', which we don't want
// here for the top-level scope being output.
void OutputScope(const Value& output, std::ostream& out) {
  Scope::KeyValueMap scope_values;
  output.scope_value()->GetCurrentScopeValues(&scope_values);
  for (const auto& pair : scope_values) {
    out << "  " << pair.first.as_string() << " = " << pair.second.ToString(true)
        << "\n";
  }
}

void OutputDefault(const Value& output, std::ostream& out) {
  if (output.type() == Value::LIST)
    OutputListLines(output, out);
  else
    ToString(output, out);
}

void OutputJSON(const Value& output, std::ostream& out) {
  if (output.type() == Value::SCOPE) {
    RenderScopeToJSON(output, out, /*indent=*/1);
    return;
  }
  if (output.type() == Value::LIST) {
    RenderListToJSON(output, out, /*indent=*/1);
    return;
  }
  ToStringQuoted(output, out);
}

void DoConvertValueToOutput(const Value& output,
                            const std::string& output_conversion,
                            const Value& original_output_conversion,
                            std::ostream& out,
                            Err* err) {
  if (output_conversion == "") {
    OutputDefault(output, out);
  } else if (output_conversion == "list lines") {
    OutputListLines(output, out);
  } else if (output_conversion == "string") {
    OutputString(output, out);
  } else if (output_conversion == "value") {
    OutputValue(output, out);
  } else if (output_conversion == "json") {
    OutputJSON(output, out);
  } else if (output_conversion == "scope") {
    if (output.type() != Value::SCOPE) {
      *err = Err(original_output_conversion, "Not a valid scope.");
      return;
    }
    OutputScope(output, out);
  } else {
    // If we make it here, we didn't match any of the valid options.
    *err = Err(original_output_conversion, "Not a valid output_conversion.",
               "Run gn help output_conversion to see your options.");
  }
}

}  // namespace

void ConvertValueToOutput(const Settings* settings,
                          const Value& output,
                          const Value& output_conversion,
                          std::ostream& out,
                          Err* err) {
  if (output_conversion.type() == Value::NONE) {
    OutputDefault(output, out);
    return;
  }
  if (!output_conversion.VerifyTypeIs(Value::STRING, err))
    return;

  DoConvertValueToOutput(output, output_conversion.string_value(),
                         output_conversion, out, err);
}
