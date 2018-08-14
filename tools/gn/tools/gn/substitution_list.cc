// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/substitution_list.h"

#include <stddef.h>
#include <string.h>

#include "tools/gn/value.h"

SubstitutionList::SubstitutionList() = default;

SubstitutionList::SubstitutionList(const SubstitutionList& other) = default;

SubstitutionList::~SubstitutionList() = default;

bool SubstitutionList::Parse(const Value& value, Err* err) {
  if (!value.VerifyTypeIs(Value::LIST, err))
    return false;

  const std::vector<Value>& input_list = value.list_value();
  list_.resize(input_list.size());
  for (size_t i = 0; i < input_list.size(); i++) {
    if (!list_[i].Parse(input_list[i], err))
      return false;
  }

  SubstitutionBits bits;
  FillRequiredTypes(&bits);
  bits.FillVector(&required_types_);
  return true;
}

bool SubstitutionList::Parse(const std::vector<std::string>& values,
                             const ParseNode* origin,
                             Err* err) {
  list_.resize(values.size());
  for (size_t i = 0; i < values.size(); i++) {
    if (!list_[i].Parse(values[i], origin, err))
      return false;
  }

  SubstitutionBits bits;
  FillRequiredTypes(&bits);
  bits.FillVector(&required_types_);
  return true;
}

SubstitutionList SubstitutionList::MakeForTest(const char* a,
                                               const char* b,
                                               const char* c) {
  std::vector<std::string> input_strings;
  input_strings.push_back(a);
  if (b)
    input_strings.push_back(b);
  if (c)
    input_strings.push_back(c);

  Err err;
  SubstitutionList result;
  result.Parse(input_strings, nullptr, &err);
  return result;
}

void SubstitutionList::FillRequiredTypes(SubstitutionBits* bits) const {
  for (const auto& item : list_)
    item.FillRequiredTypes(bits);
}
