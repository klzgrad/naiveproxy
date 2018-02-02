// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_SETTINGS_H_
#define TOOLS_GN_SETTINGS_H_

#include "base/files/file_path.h"
#include "base/macros.h"
#include "tools/gn/build_settings.h"
#include "tools/gn/import_manager.h"
#include "tools/gn/output_file.h"
#include "tools/gn/scope.h"
#include "tools/gn/source_dir.h"
#include "tools/gn/toolchain.h"

// Holds the settings for one toolchain invocation. There will be one
// Settings object for each toolchain type, each referring to the same
// BuildSettings object for shared stuff.
//
// The Settings object is const once it is constructed, which allows us to
// use it from multiple threads during target generation without locking (which
// is important, because it gets used a lot).
//
// The Toolchain object holds the set of stuff that is set by the toolchain
// declaration, which obviously needs to be set later when we actually parse
// the file with the toolchain declaration in it.
class Settings {
 public:
  // Constructs a toolchain settings.
  //
  // The output_subdir_name is the name we should use for the subdirectory in
  // the build output directory for this toolchain's outputs. The default
  // toolchain would use an empty string (it goes in the root build dir).
  // Otherwise, it must end in a slash.
  Settings(const BuildSettings* build_settings,
           const std::string& output_subdir_name);
  ~Settings();

  const BuildSettings* build_settings() const { return build_settings_; }

  // The actual Toolchain object pointer is not available on the settings
  // object because it might not be resolved yet. Code running after the
  // load is complete can ask the Builder for the Toolchain corresponding to
  // this label.
  const Label& toolchain_label() const { return toolchain_label_; }
  void set_toolchain_label(const Label& l) { toolchain_label_ = l; }

  const Label& default_toolchain_label() const {
    return default_toolchain_label_;
  }
  void set_default_toolchain_label(const Label& default_label) {
    default_toolchain_label_ = default_label;
  }

  // Indicates if this corresponds to the default toolchain.
  bool is_default() const {
    return toolchain_label_ == default_toolchain_label_;
  }

  const OutputFile& toolchain_output_subdir() const {
    return toolchain_output_subdir_;
  }
  const SourceDir& toolchain_output_dir() const {
    return toolchain_output_dir_;
  }

  // Directory for generated files.
  const SourceDir& toolchain_gen_dir() const {
    return toolchain_gen_dir_;
  }

  // The import manager caches the result of executing imported files in the
  // context of a given settings object.
  //
  // See the ItemTree getter in GlobalSettings for why this doesn't return a
  // const pointer.
  ImportManager& import_manager() const { return import_manager_; }

  const Scope* base_config() const { return &base_config_; }
  Scope* base_config() { return &base_config_; }

  // Set to true when every target we encounter should be generated. False
  // means that only targets that have a dependency from (directly or
  // indirectly) some magic root node are actually generated. See the comments
  // on ItemTree for more.
  bool greedy_target_generation() const {
    return greedy_target_generation_;
  }
  void set_greedy_target_generation(bool gtg) {
    greedy_target_generation_ = gtg;
  }

 private:
  const BuildSettings* build_settings_;

  Label toolchain_label_;
  Label default_toolchain_label_;

  mutable ImportManager import_manager_;

  // The subdirectory inside the build output for this toolchain. For the
  // default toolchain, this will be empty (since the deafult toolchain's
  // output directory is the same as the build directory). When nonempty, this
  // is guaranteed to end in a slash.
  OutputFile toolchain_output_subdir_;

  // Full source file path to the toolchain output directory.
  SourceDir toolchain_output_dir_;

  SourceDir toolchain_gen_dir_;

  Scope base_config_;

  bool greedy_target_generation_;

  DISALLOW_COPY_AND_ASSIGN(Settings);
};

#endif  // TOOLS_GN_SETTINGS_H_
