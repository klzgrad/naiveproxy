// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_LABEL_PATTERN_H_
#define TOOLS_GN_LABEL_PATTERN_H_

#include "base/strings/string_piece.h"
#include "tools/gn/label.h"
#include "tools/gn/source_dir.h"

class Err;
class Value;

extern const char kLabelPattern_Help[];

// A label pattern is a simple pattern that matches labels. It is used for
// specifying visibility and other times when multiple targets need to be
// referenced.
class LabelPattern {
 public:
  enum Type {
    MATCH = 1,           // Exact match for a given target.
    DIRECTORY,           // Only targets in the file in the given directory.
    RECURSIVE_DIRECTORY  // The given directory and any subdir.
                         // (also indicates "public" when dir is empty).
  };

  LabelPattern();
  LabelPattern(Type type,
               const SourceDir& dir,
               const base::StringPiece& name,
               const Label& toolchain_label);
  LabelPattern(const LabelPattern& other);
  ~LabelPattern();

  // Converts the given input string to a pattern. This does special stuff
  // to treat the pattern as a label. Sets the error on failure.
  static LabelPattern GetPattern(const SourceDir& current_dir,
                                 const Value& value,
                                 Err* err);

  // Returns true if the given input string might match more than one thing.
  static bool HasWildcard(const std::string& str);

  // Returns true if this pattern matches the given label.
  bool Matches(const Label& label) const;

  // Returns true if any of the patterns in the vector match the label.
  static bool VectorMatches(const std::vector<LabelPattern>& patterns,
                            const Label& label);

  // Returns a string representation of this pattern.
  std::string Describe() const;

  Type type() const { return type_; }

  const SourceDir& dir() const { return dir_; }
  const std::string& name() const { return name_; }

  const Label& toolchain() const { return toolchain_; }
  void set_toolchain(const Label& tc) { toolchain_ = tc; }

 private:
  // If nonempty, specifies the toolchain to use. If empty, this will match
  // all toolchains. This is independent of the match type.
  Label toolchain_;

  Type type_;

  // Used when type_ == PRIVATE and PRIVATE_RECURSIVE. This specifies the
  // directory that to which the pattern is private to.
  SourceDir dir_;

  // Empty name means match everything. Otherwise the name must match
  // exactly.
  std::string name_;
};

#endif  // TOOLS_GN_LABEL_PATTERN_H_
