// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/value.h"

#include <stddef.h>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "tools/gn/scope.h"

Value::Value()
    : type_(NONE),
      boolean_value_(false),
      int_value_(0),
      origin_(nullptr) {
}

Value::Value(const ParseNode* origin, Type t)
    : type_(t),
      boolean_value_(false),
      int_value_(0),
      origin_(origin) {
}

Value::Value(const ParseNode* origin, bool bool_val)
    : type_(BOOLEAN),
      boolean_value_(bool_val),
      int_value_(0),
      origin_(origin) {
}

Value::Value(const ParseNode* origin, int64_t int_val)
    : type_(INTEGER),
      boolean_value_(false),
      int_value_(int_val),
      origin_(origin) {
}

Value::Value(const ParseNode* origin, std::string str_val)
    : type_(STRING),
      string_value_(std::move(str_val)),
      boolean_value_(false),
      int_value_(0),
      origin_(origin) {}

Value::Value(const ParseNode* origin, const char* str_val)
    : type_(STRING),
      string_value_(str_val),
      boolean_value_(false),
      int_value_(0),
      origin_(origin) {
}

Value::Value(const ParseNode* origin, std::unique_ptr<Scope> scope)
    : type_(SCOPE),
      string_value_(),
      boolean_value_(false),
      int_value_(0),
      scope_value_(std::move(scope)),
      origin_(origin) {}

Value::Value(const Value& other)
    : type_(other.type_),
      string_value_(other.string_value_),
      boolean_value_(other.boolean_value_),
      int_value_(other.int_value_),
      list_value_(other.list_value_),
      origin_(other.origin_) {
  if (type() == SCOPE && other.scope_value_.get())
    scope_value_ = other.scope_value_->MakeClosure();
}

Value::Value(Value&& other) noexcept = default;

Value::~Value() {
}

Value& Value::operator=(const Value& other) {
  type_ = other.type_;
  string_value_ = other.string_value_;
  boolean_value_ = other.boolean_value_;
  int_value_ = other.int_value_;
  list_value_ = other.list_value_;
  if (type() == SCOPE && other.scope_value_.get())
    scope_value_ = other.scope_value_->MakeClosure();
  origin_ = other.origin_;
  return *this;
}

// static
const char* Value::DescribeType(Type t) {
  switch (t) {
    case NONE:
      return "none";
    case BOOLEAN:
      return "boolean";
    case INTEGER:
      return "integer";
    case STRING:
      return "string";
    case LIST:
      return "list";
    case SCOPE:
      return "scope";
    default:
      NOTREACHED();
      return "UNKNOWN";
  }
}

void Value::SetScopeValue(std::unique_ptr<Scope> scope) {
  DCHECK(type_ == SCOPE);
  scope_value_ = std::move(scope);
}

std::string Value::ToString(bool quote_string) const {
  switch (type_) {
    case NONE:
      return "<void>";
    case BOOLEAN:
      return boolean_value_ ? "true" : "false";
    case INTEGER:
      return base::Int64ToString(int_value_);
    case STRING:
      if (quote_string) {
        std::string result = "\"";
        bool hanging_backslash = false;
        for (char ch : string_value_) {
          // If the last character was a literal backslash and the next
          // character could form a valid escape sequence, we need to insert
          // an extra backslash to prevent that.
          if (hanging_backslash && (ch == '$' || ch == '"' || ch == '\\'))
            result += '\\';
          // If the next character is a dollar sign or double quote, it needs
          // to be escaped; otherwise it can be printed as is.
          if (ch == '$' || ch == '"')
            result += '\\';
          result += ch;
          hanging_backslash = (ch == '\\');
        }
        // Again, we need to prevent the closing double quotes from becoming
        // an escape sequence.
        if (hanging_backslash)
          result += '\\';
        result += '"';
        return result;
      }
      return string_value_;
    case LIST: {
      std::string result = "[";
      for (size_t i = 0; i < list_value_.size(); i++) {
        if (i > 0)
          result += ", ";
        result += list_value_[i].ToString(true);
      }
      result.push_back(']');
      return result;
    }
    case SCOPE: {
      Scope::KeyValueMap scope_values;
      scope_value_->GetCurrentScopeValues(&scope_values);
      if (scope_values.empty())
        return std::string("{ }");

      std::string result = "{\n";
      for (const auto& pair : scope_values) {
        result += "  " + pair.first.as_string() + " = " +
                  pair.second.ToString(true) + "\n";
      }
      result += "}";

      return result;
    }
  }
  return std::string();
}

bool Value::VerifyTypeIs(Type t, Err* err) const {
  if (type_ == t)
    return true;

  *err = Err(origin(),
             std::string("This is not a ") + DescribeType(t) + ".",
             std::string("Instead I see a ") + DescribeType(type_) + " = " +
             ToString(true));
  return false;
}

bool Value::operator==(const Value& other) const {
  if (type_ != other.type_)
    return false;

  switch (type_) {
    case Value::BOOLEAN:
      return boolean_value() == other.boolean_value();
    case Value::INTEGER:
      return int_value() == other.int_value();
    case Value::STRING:
      return string_value() == other.string_value();
    case Value::LIST:
      if (list_value().size() != other.list_value().size())
        return false;
      for (size_t i = 0; i < list_value().size(); i++) {
        if (list_value()[i] != other.list_value()[i])
          return false;
      }
      return true;
    case Value::SCOPE:
      // Scopes are always considered not equal because there's currently
      // no use case for comparing them, and it requires a bunch of complex
      // iteration code.
      return false;
    default:
      return false;
  }
}

bool Value::operator!=(const Value& other) const {
  return !operator==(other);
}
