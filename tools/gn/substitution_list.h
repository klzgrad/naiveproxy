// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_SUBSTITUTION_LIST_H_
#define TOOLS_GN_SUBSTITUTION_LIST_H_

#include <string>
#include <vector>

#include "tools/gn/substitution_pattern.h"

// Represents a list of strings with {{substitution_patterns}} in them.
class SubstitutionList {
 public:
  SubstitutionList();
  SubstitutionList(const SubstitutionList& other);
  ~SubstitutionList();

  bool Parse(const Value& value, Err* err);
  bool Parse(const std::vector<std::string>& values,
             const ParseNode* origin,
             Err* err);

  // Makes a SubstitutionList from the given hardcoded patterns.
  static SubstitutionList MakeForTest(
      const char* a,
      const char* b = nullptr,
      const char* c = nullptr);

  const std::vector<SubstitutionPattern>& list() const { return list_; }

  // Returns a list of all substitution types used by the patterns in this
  // list, with the exception of LITERAL.
  const std::vector<SubstitutionType>& required_types() const {
    return required_types_;
  }

  void FillRequiredTypes(SubstitutionBits* bits) const;

 private:
  std::vector<SubstitutionPattern> list_;

  std::vector<SubstitutionType> required_types_;
};

#endif  // TOOLS_GN_SUBSTITUTION_LIST_H_
