// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_OUTPUT_FILE_H_
#define TOOLS_GN_OUTPUT_FILE_H_

#include <stddef.h>

#include <string>

#include "base/containers/hash_tables.h"
#include "tools/gn/build_settings.h"

class SourceFile;

// A simple wrapper around a string that indicates the string is a path
// relative to the output directory.
class OutputFile {
 public:
  OutputFile();
  explicit OutputFile(std::string&& v);
  explicit OutputFile(const std::string& v);
  OutputFile(const BuildSettings* build_settings,
             const SourceFile& source_file);
  ~OutputFile();

  std::string& value() { return value_; }
  const std::string& value() const { return value_; }

  // Converts to a SourceFile by prepending the build directory to the file.
  // The *Dir version requires that the current OutputFile ends in a slash, and
  // the *File version is the opposite.
  SourceFile AsSourceFile(const BuildSettings* build_settings) const;
  SourceDir AsSourceDir(const BuildSettings* build_settings) const;

  bool operator==(const OutputFile& other) const {
    return value_ == other.value_;
  }
  bool operator!=(const OutputFile& other) const {
    return value_ != other.value_;
  }
  bool operator<(const OutputFile& other) const {
    return value_ < other.value_;
  }

 private:
  std::string value_;
};

namespace BASE_HASH_NAMESPACE {

template<> struct hash<OutputFile> {
  std::size_t operator()(const OutputFile& v) const {
    hash<std::string> h;
    return h(v.value());
  }
};

}  // namespace BASE_HASH_NAMESPACE

inline void swap(OutputFile& lhs, OutputFile& rhs) {
  lhs.value().swap(rhs.value());
}

#endif  // TOOLS_GN_OUTPUT_FILE_H_
