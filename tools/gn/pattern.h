// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_PATTERN_H_
#define TOOLS_GN_PATTERN_H_

#include <stddef.h>

#include <string>
#include <vector>

#include "tools/gn/value.h"

class Pattern {
 public:
  struct Subrange {
    enum Type {
      LITERAL,  // Matches exactly the contents of the string.
      ANYTHING,  // * (zero or more chars).
      PATH_BOUNDARY  // '/' or beginning of string.
    };

    explicit Subrange(Type t, const std::string& l = std::string())
        : type(t),
          literal(l) {
    }

    // Returns the minimum number of chars that this subrange requires.
    size_t MinSize() const {
      switch (type) {
        case LITERAL:
          return literal.size();
        case ANYTHING:
          return 0;
        case PATH_BOUNDARY:
          return 0;  // Can match beginning or end of string, which is 0 len.
        default:
          return 0;
      }
    }

    Type type;

    // When type == LITERAL this is the text to match.
    std::string literal;
  };

  explicit Pattern(const std::string& s);
  Pattern(const Pattern& other);
  ~Pattern();

  // Returns true if the current pattern matches the given string.
  bool MatchesString(const std::string& s) const;

 private:
  // allow_implicit_path_boundary determines if a path boundary should accept
  // matches at the beginning or end of the string.
  bool RecursiveMatch(const std::string& s,
                      size_t begin_char,
                      size_t subrange_index,
                      bool allow_implicit_path_boundary) const;

  std::vector<Subrange> subranges_;

  // Set to true when the subranges are "*foo" ("ANYTHING" followed by a
  // literal). This covers most patterns so we optimize for this.
  bool is_suffix_;
};

class PatternList {
 public:
  PatternList();
  PatternList(const PatternList& other);
  ~PatternList();

  bool is_empty() const { return patterns_.empty(); }

  void Append(const Pattern& pattern);

  // Initializes the pattern list from a give list of pattern strings. Sets
  // |*err| on failure.
  void SetFromValue(const Value& v, Err* err);

  bool MatchesString(const std::string& s) const;
  bool MatchesValue(const Value& v) const;

 private:
  std::vector<Pattern> patterns_;
};

#endif  // TOOLS_GN_PATTERN_H_
