// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_LABEL_H_
#define TOOLS_GN_LABEL_H_

#include <stddef.h>

#include "base/containers/hash_tables.h"
#include "tools/gn/source_dir.h"

class Err;
class Value;

// A label represents the name of a target or some other named thing in
// the source path. The label is always absolute and always includes a name
// part, so it starts with a slash, and has one colon.
class Label {
 public:
  Label();

  // Makes a label given an already-separated out path and name.
  // See also Resolve().
  Label(const SourceDir& dir,
        const base::StringPiece& name,
        const SourceDir& toolchain_dir,
        const base::StringPiece& toolchain_name);

  // Makes a label with an empty toolchain.
  Label(const SourceDir& dir, const base::StringPiece& name);
  Label(const Label& other);
  ~Label();

  // Resolves a string from a build file that may be relative to the
  // current directory into a fully qualified label. On failure returns an
  // is_null() label and sets the error.
  static Label Resolve(const SourceDir& current_dir,
                       const Label& current_toolchain,
                       const Value& input,
                       Err* err);

  bool is_null() const { return dir_.is_null(); }

  const SourceDir& dir() const { return dir_; }
  const std::string& name() const { return name_; }

  const SourceDir& toolchain_dir() const { return toolchain_dir_; }
  const std::string& toolchain_name() const { return toolchain_name_; }

  // Returns the current label's toolchain as its own Label.
  Label GetToolchainLabel() const;

  // Returns a copy of this label but with an empty toolchain.
  Label GetWithNoToolchain() const;

  // Formats this label in a way that we can present to the user or expose to
  // other parts of the system. SourceDirs end in slashes, but the user
  // expects names like "//chrome/renderer:renderer_config" when printed. The
  // toolchain is optionally included.
  std::string GetUserVisibleName(bool include_toolchain) const;

  // Like the above version, but automatically includes the toolchain if it's
  // not the default one. Normally the user only cares about the toolchain for
  // non-default ones, so this can make certain output more clear.
  std::string GetUserVisibleName(const Label& default_toolchain) const;

  bool operator==(const Label& other) const {
    return name_ == other.name_ && dir_ == other.dir_ &&
           toolchain_dir_ == other.toolchain_dir_ &&
           toolchain_name_ == other.toolchain_name_;
  }
  bool operator!=(const Label& other) const {
    return !operator==(other);
  }
  bool operator<(const Label& other) const {
    if (int c = dir_.value().compare(other.dir_.value()))
      return c < 0;
    if (int c = name_.compare(other.name_))
      return c < 0;
    if (int c = toolchain_dir_.value().compare(other.toolchain_dir_.value()))
      return c < 0;
    return toolchain_name_ < other.toolchain_name_;
  }

  void swap(Label& other) {
    dir_.swap(other.dir_);
    name_.swap(other.name_);
    toolchain_dir_.swap(other.toolchain_dir_);
    toolchain_name_.swap(other.toolchain_name_);
  }

  // Returns true if the toolchain dir/name of this object matches some
  // other object.
  bool ToolchainsEqual(const Label& other) const {
    return toolchain_dir_ == other.toolchain_dir_ &&
           toolchain_name_ == other.toolchain_name_;
  }

 private:
  SourceDir dir_;
  std::string name_;

  SourceDir toolchain_dir_;
  std::string toolchain_name_;
};

namespace BASE_HASH_NAMESPACE {

template<> struct hash<Label> {
  std::size_t operator()(const Label& v) const {
    hash<std::string> stringhash;
    return ((stringhash(v.dir().value()) * 131 +
             stringhash(v.name())) * 131 +
            stringhash(v.toolchain_dir().value())) * 131 +
           stringhash(v.toolchain_name());
  }
};

}  // namespace BASE_HASH_NAMESPACE

inline void swap(Label& lhs, Label& rhs) {
  lhs.swap(rhs);
}

extern const char kLabels_Help[];

#endif  // TOOLS_GN_LABEL_H_
