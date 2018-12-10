// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_VISIBILITY_H_
#define TOOLS_GN_VISIBILITY_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "tools/gn/label_pattern.h"
#include "tools/gn/source_dir.h"

namespace base {
class Value;
}

class Err;
class Item;
class Label;
class Scope;
class Value;

class Visibility {
 public:
  // Defaults to private visibility (only the current file).
  Visibility();
  ~Visibility();

  // Set the visibility to the thing specified by the given value. On failure,
  // returns false and sets the error.
  bool Set(const SourceDir& current_dir, const Value& value, Err* err);

  // Sets the visibility to be public.
  void SetPublic();

  // Sets the visibility to be private to the given directory.
  void SetPrivate(const SourceDir& current_dir);

  // Returns true if the target with the given label can depend on one with the
  // current visibility.
  bool CanSeeMe(const Label& label) const;

  // Returns a string listing the visibility. |indent| number of spaces will
  // be added on the left side of the output. If |include_brackets| is set, the
  // result will be wrapped in "[ ]" and the contents further indented. The
  // result will end in a newline.
  std::string Describe(int indent, bool include_brackets) const;

  // Returns value representation of this visibility
  std::unique_ptr<base::Value> AsValue() const;

  // Helper function to check visibility between the given two items. If
  // to is invisible to from, returns false and sets the error.
  static bool CheckItemVisibility(const Item* from, const Item* to, Err* err);

  // Helper function to fill an item's visibility from the "visibility" value
  // in the current scope.
  static bool FillItemVisibility(Item* item, Scope* scope, Err* err);

 private:
  std::vector<LabelPattern> patterns_;

  DISALLOW_COPY_AND_ASSIGN(Visibility);
};

#endif  // TOOLS_GN_VISIBILITY_H_
