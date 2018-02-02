// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_CONFIG_VALUES_H_
#define TOOLS_GN_CONFIG_VALUES_H_

#include <string>
#include <vector>

#include "base/macros.h"
#include "tools/gn/lib_file.h"
#include "tools/gn/source_dir.h"
#include "tools/gn/source_file.h"

// Holds the values (include_dirs, defines, compiler flags, etc.) for a given
// config or target.
class ConfigValues {
 public:
  ConfigValues();
  ~ConfigValues();

  // Appends the values from the given config to this one.
  void AppendValues(const ConfigValues& append);

#define STRING_VALUES_ACCESSOR(name) \
    const std::vector<std::string>& name() const { return name##_; } \
    std::vector<std::string>& name() { return name##_; }
#define DIR_VALUES_ACCESSOR(name) \
    const std::vector<SourceDir>& name() const { return name##_; } \
    std::vector<SourceDir>& name() { return name##_; }

  // =================================================================
  // IMPORTANT: If you add a new one, be sure to update AppendValues()
  //            and command_desc.cc.
  // =================================================================
  STRING_VALUES_ACCESSOR(arflags)
  STRING_VALUES_ACCESSOR(asmflags)
  STRING_VALUES_ACCESSOR(cflags)
  STRING_VALUES_ACCESSOR(cflags_c)
  STRING_VALUES_ACCESSOR(cflags_cc)
  STRING_VALUES_ACCESSOR(cflags_objc)
  STRING_VALUES_ACCESSOR(cflags_objcc)
  STRING_VALUES_ACCESSOR(defines)
  DIR_VALUES_ACCESSOR   (include_dirs)
  STRING_VALUES_ACCESSOR(ldflags)
  DIR_VALUES_ACCESSOR   (lib_dirs)
  // =================================================================
  // IMPORTANT: If you add a new one, be sure to update AppendValues()
  //            and command_desc.cc.
  // =================================================================

#undef STRING_VALUES_ACCESSOR
#undef DIR_VALUES_ACCESSOR

  const std::vector<LibFile>& libs() const { return libs_; }
  std::vector<LibFile>& libs() { return libs_; }

  bool has_precompiled_headers() const {
    return !precompiled_header_.empty() || !precompiled_source_.is_null();
  }
  const std::string& precompiled_header() const {
    return precompiled_header_;
  }
  void set_precompiled_header(const std::string& f) {
    precompiled_header_ = f;
  }
  const SourceFile& precompiled_source() const {
    return precompiled_source_;
  }
  void set_precompiled_source(const SourceFile& f) {
    precompiled_source_ = f;
  }

 private:
  std::vector<std::string> arflags_;
  std::vector<std::string> asmflags_;
  std::vector<std::string> cflags_;
  std::vector<std::string> cflags_c_;
  std::vector<std::string> cflags_cc_;
  std::vector<std::string> cflags_objc_;
  std::vector<std::string> cflags_objcc_;
  std::vector<std::string> defines_;
  std::vector<SourceDir>   include_dirs_;
  std::vector<std::string> ldflags_;
  std::vector<SourceDir>   lib_dirs_;
  std::vector<LibFile>     libs_;
  // If you add a new one, be sure to update AppendValues().

  std::string precompiled_header_;
  SourceFile precompiled_source_;
};

#endif  // TOOLS_GN_CONFIG_VALUES_H_
