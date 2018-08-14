// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_VALUE_H_
#define TOOLS_GN_VALUE_H_

#include <stdint.h>

#include <map>
#include <memory>

#include "base/logging.h"
#include "base/macros.h"
#include "tools/gn/err.h"

class ParseNode;
class Scope;

// Represents a variable value in the interpreter.
class Value {
 public:
  enum Type {
    NONE = 0,
    BOOLEAN,
    INTEGER,
    STRING,
    LIST,
    SCOPE,
  };

  Value();
  Value(const ParseNode* origin, Type t);
  Value(const ParseNode* origin, bool bool_val);
  Value(const ParseNode* origin, int64_t int_val);
  Value(const ParseNode* origin, std::string str_val);
  Value(const ParseNode* origin, const char* str_val);
  // Values "shouldn't" have null scopes when type == Scope, so be sure to
  // always set one. However, this is not asserted since there are some
  // use-cases for creating values and immediately setting the scope on it. So
  // you can pass a null scope here if you promise to set it before any other
  // code gets it (code will generally assume the scope is not null).
  Value(const ParseNode* origin, std::unique_ptr<Scope> scope);

  Value(const Value& other);
  Value(Value&& other) noexcept;
  ~Value();

  Value& operator=(const Value& other);
  Value& operator=(Value&& other) = default;

  Type type() const { return type_; }

  // Returns a string describing the given type.
  static const char* DescribeType(Type t);

  // Returns the node that made this. May be NULL.
  const ParseNode* origin() const { return origin_; }
  void set_origin(const ParseNode* o) { origin_ = o; }

  bool& boolean_value() {
    DCHECK(type_ == BOOLEAN);
    return boolean_value_;
  }
  const bool& boolean_value() const {
    DCHECK(type_ == BOOLEAN);
    return boolean_value_;
  }

  int64_t& int_value() {
    DCHECK(type_ == INTEGER);
    return int_value_;
  }
  const int64_t& int_value() const {
    DCHECK(type_ == INTEGER);
    return int_value_;
  }

  std::string& string_value() {
    DCHECK(type_ == STRING);
    return string_value_;
  }
  const std::string& string_value() const {
    DCHECK(type_ == STRING);
    return string_value_;
  }

  std::vector<Value>& list_value() {
    DCHECK(type_ == LIST);
    return list_value_;
  }
  const std::vector<Value>& list_value() const {
    DCHECK(type_ == LIST);
    return list_value_;
  }

  Scope* scope_value() {
    DCHECK(type_ == SCOPE);
    return scope_value_.get();
  }
  const Scope* scope_value() const {
    DCHECK(type_ == SCOPE);
    return scope_value_.get();
  }
  void SetScopeValue(std::unique_ptr<Scope> scope);

  // Converts the given value to a string. Returns true if strings should be
  // quoted or the ToString of a string should be the string itself. If the
  // string is quoted, it will also enable escaping.
  std::string ToString(bool quote_strings) const;

  // Verifies that the value is of the given type. If it isn't, returns
  // false and sets the error.
  bool VerifyTypeIs(Type t, Err* err) const;

  // Compares values. Only the "value" is compared, not the origin.
  bool operator==(const Value& other) const;
  bool operator!=(const Value& other) const;

 private:
  // This are a lot of objects associated with every Value that need
  // initialization and tear down every time. It might be more efficient to
  // create a union of objects (see small_map) and only use the one we care
  // about.
  Type type_;
  std::string string_value_;
  bool boolean_value_;
  int64_t int_value_;
  std::vector<Value> list_value_;
  std::unique_ptr<Scope> scope_value_;

  const ParseNode* origin_;
};

#endif  // TOOLS_GN_VALUE_H_
