// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/token.h"

#include "base/logging.h"

Token::Token() : type_(INVALID), value_() {
}

Token::Token(const Location& location,
             Type t,
             const base::StringPiece& v)
    : type_(t),
      value_(v),
      location_(location) {
}

Token::Token(const Token& other) = default;

bool Token::IsIdentifierEqualTo(const char* v) const {
  return type_ == IDENTIFIER && value_ == v;
}

bool Token::IsStringEqualTo(const char* v) const {
  return type_ == STRING && value_ == v;
}
