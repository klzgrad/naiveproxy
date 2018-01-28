// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_SUBSTITUTION_PATTERN_H_
#define TOOLS_GN_SUBSTITUTION_PATTERN_H_

#include <string>
#include <vector>

#include "tools/gn/substitution_type.h"

class BuildSettings;
class Err;
class ParseNode;
class Value;

// Represents a string with {{substitution_patterns}} in them.
class SubstitutionPattern {
 public:
  struct Subrange {
    Subrange();
    explicit Subrange(SubstitutionType t, const std::string& l = std::string());
    ~Subrange();

    inline bool operator==(const Subrange& other) const {
      return type == other.type && literal == other.literal;
    }

    SubstitutionType type;

    // When type_ == LITERAL, this specifies the literal.
    std::string literal;
  };

  SubstitutionPattern();
  SubstitutionPattern(const SubstitutionPattern& other);
  ~SubstitutionPattern();

  // Parses the given string and fills in the pattern. The pattern must only
  // be initialized once. On failure, returns false and sets the error.
  bool Parse(const Value& value, Err* err);
  bool Parse(const std::string& str, const ParseNode* origin, Err* err);

  // Makes a pattern given a hardcoded string. Will assert if the string is
  // not a valid pattern.
  static SubstitutionPattern MakeForTest(const char* str);

  // Returns the pattern as a string with substitutions in them.
  std::string AsString() const;

  // Sets the bits in the given vector corresponding to the substitutions used
  // by this pattern. SUBSTITUTION_LITERAL is ignored.
  void FillRequiredTypes(SubstitutionBits* bits) const;

  // Checks whether this pattern resolves to something in the output directory
  // for the given build settings. If not, returns false and fills in the given
  // error.
  bool IsInOutputDir(const BuildSettings* build_settings,
                     Err* err) const;

  // Returns a vector listing the substitutions used by this pattern, not
  // counting SUBSTITUTION_LITERAL.
  const std::vector<SubstitutionType>& required_types() const {
    return required_types_;
  }

  const std::vector<Subrange>& ranges() const { return ranges_; }
  bool empty() const { return ranges_.empty(); }

 private:
  std::vector<Subrange> ranges_;
  const ParseNode* origin_;

  std::vector<SubstitutionType> required_types_;
};

#endif  // TOOLS_GN_SUBSTITUTION_PATTERN_H_
