// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/json/json_writer.h"

#include <stdint.h>

#include <cmath>
#include <limits>

#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "util/build_config.h"

namespace base {

#if defined(OS_WIN)
const char kPrettyPrintLineEnding[] = "\r\n";
#else
const char kPrettyPrintLineEnding[] = "\n";
#endif

// static
bool JSONWriter::Write(const Value& node, std::string* json) {
  return WriteWithOptions(node, 0, json);
}

// static
bool JSONWriter::WriteWithOptions(const Value& node,
                                  int options,
                                  std::string* json) {
  json->clear();
  // Is there a better way to estimate the size of the output?
  json->reserve(1024);

  JSONWriter writer(options, json);
  bool result = writer.BuildJSONString(node, 0U);

  if (options & OPTIONS_PRETTY_PRINT)
    json->append(kPrettyPrintLineEnding);

  return result;
}

JSONWriter::JSONWriter(int options, std::string* json)
    : omit_binary_values_((options & OPTIONS_OMIT_BINARY_VALUES) != 0),
      omit_double_type_preservation_(
          (options & OPTIONS_OMIT_DOUBLE_TYPE_PRESERVATION) != 0),
      pretty_print_((options & OPTIONS_PRETTY_PRINT) != 0),
      json_string_(json) {
  DCHECK(json);
}

bool JSONWriter::BuildJSONString(const Value& node, size_t depth) {
  switch (node.type()) {
    case Value::Type::NONE: {
      json_string_->append("null");
      return true;
    }

    case Value::Type::BOOLEAN: {
      bool value;
      bool result = node.GetAsBoolean(&value);
      DCHECK(result);
      json_string_->append(value ? "true" : "false");
      return result;
    }

    case Value::Type::INTEGER: {
      int value;
      bool result = node.GetAsInteger(&value);
      DCHECK(result);
      json_string_->append(IntToString(value));
      return result;
    }

    case Value::Type::STRING: {
      std::string value;
      bool result = node.GetAsString(&value);
      DCHECK(result);
      EscapeJSONString(value, true, json_string_);
      return result;
    }

    case Value::Type::LIST: {
      json_string_->push_back('[');
      if (pretty_print_)
        json_string_->push_back(' ');

      const ListValue* list = nullptr;
      bool first_value_has_been_output = false;
      bool result = node.GetAsList(&list);
      DCHECK(result);
      for (const auto& value : *list) {
        if (omit_binary_values_ && value.type() == Value::Type::BINARY)
          continue;

        if (first_value_has_been_output) {
          json_string_->push_back(',');
          if (pretty_print_)
            json_string_->push_back(' ');
        }

        if (!BuildJSONString(value, depth))
          result = false;

        first_value_has_been_output = true;
      }

      if (pretty_print_)
        json_string_->push_back(' ');
      json_string_->push_back(']');
      return result;
    }

    case Value::Type::DICTIONARY: {
      json_string_->push_back('{');
      if (pretty_print_)
        json_string_->append(kPrettyPrintLineEnding);

      const DictionaryValue* dict = nullptr;
      bool first_value_has_been_output = false;
      bool result = node.GetAsDictionary(&dict);
      DCHECK(result);
      for (DictionaryValue::Iterator itr(*dict); !itr.IsAtEnd();
           itr.Advance()) {
        if (omit_binary_values_ && itr.value().type() == Value::Type::BINARY) {
          continue;
        }

        if (first_value_has_been_output) {
          json_string_->push_back(',');
          if (pretty_print_)
            json_string_->append(kPrettyPrintLineEnding);
        }

        if (pretty_print_)
          IndentLine(depth + 1U);

        EscapeJSONString(itr.key(), true, json_string_);
        json_string_->push_back(':');
        if (pretty_print_)
          json_string_->push_back(' ');

        if (!BuildJSONString(itr.value(), depth + 1U))
          result = false;

        first_value_has_been_output = true;
      }

      if (pretty_print_) {
        json_string_->append(kPrettyPrintLineEnding);
        IndentLine(depth);
      }

      json_string_->push_back('}');
      return result;
    }

    case Value::Type::BINARY:
      // Successful only if we're allowed to omit it.
      DLOG_IF(ERROR, !omit_binary_values_) << "Cannot serialize binary value.";
      return omit_binary_values_;
  }
  NOTREACHED();
  return false;
}

void JSONWriter::IndentLine(size_t depth) {
  json_string_->append(depth * 3U, ' ');
}

}  // namespace base
