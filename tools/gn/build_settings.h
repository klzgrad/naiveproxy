// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_BUILD_SETTINGS_H_
#define TOOLS_GN_BUILD_SETTINGS_H_

#include <map>
#include <memory>
#include <set>
#include <utility>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "tools/gn/args.h"
#include "tools/gn/label.h"
#include "tools/gn/scope.h"
#include "tools/gn/source_dir.h"
#include "tools/gn/source_file.h"

class Item;

// Settings for one build, which is one toplevel output directory. There
// may be multiple Settings objects that refer to this, one for each toolchain.
class BuildSettings {
 public:
  typedef base::Callback<void(std::unique_ptr<Item>)> ItemDefinedCallback;
  typedef base::Callback<void(const std::string&)> PrintCallback;

  BuildSettings();
  BuildSettings(const BuildSettings& other);
  ~BuildSettings();

  // Root target label.
  const Label& root_target_label() const { return root_target_label_; }
  void SetRootTargetLabel(const Label& r);

  // Absolute path of the source root on the local system. Everything is
  // relative to this. Does not end in a [back]slash.
  const base::FilePath& root_path() const { return root_path_; }
  const std::string& root_path_utf8() const { return root_path_utf8_; }
  void SetRootPath(const base::FilePath& r);

  // When nonempty, specifies a parallel directory higherarchy in which to
  // search for buildfiles if they're not found in the root higherarchy. This
  // allows us to keep buildfiles in a separate tree during development.
  const base::FilePath& secondary_source_path() const {
    return secondary_source_path_;
  }
  void SetSecondarySourcePath(const SourceDir& d);

  // Path of the python executable to run scripts with.
  base::FilePath python_path() const { return python_path_; }
  void set_python_path(const base::FilePath& p) { python_path_ = p; }

  const SourceFile& build_config_file() const { return build_config_file_; }
  void set_build_config_file(const SourceFile& f) { build_config_file_ = f; }

  // Path to a file containing the default text to use when running `gn args`.
  const SourceFile& arg_file_template_path() const {
    return arg_file_template_path_;
  }
  void set_arg_file_template_path(const SourceFile& f) {
    arg_file_template_path_ = f;
  }

  // The build directory is the root of all output files. The default toolchain
  // files go into here, and non-default toolchains will have separate
  // toolchain-specific root directories inside this.
  const SourceDir& build_dir() const { return build_dir_; }
  void SetBuildDir(const SourceDir& dir);

  // The build args are normally specified on the command-line.
  Args& build_args() { return build_args_; }
  const Args& build_args() const { return build_args_; }

  // Returns the full absolute OS path cooresponding to the given file in the
  // root source tree.
  base::FilePath GetFullPath(const SourceFile& file) const;
  base::FilePath GetFullPath(const SourceDir& dir) const;

  // Returns the absolute OS path inside the secondary source path. Will return
  // an empty FilePath if the secondary source path is empty. When loading a
  // buildfile, the GetFullPath should always be consulted first.
  base::FilePath GetFullPathSecondary(const SourceFile& file) const;
  base::FilePath GetFullPathSecondary(const SourceDir& dir) const;

  // Called when an item is defined from a background thread.
  void ItemDefined(std::unique_ptr<Item> item) const;
  void set_item_defined_callback(ItemDefinedCallback cb) {
    item_defined_callback_ = cb;
  }

  // Defines a callback that will be used to override the behavior of the
  // print function. This is used in tests to collect print output. If the
  // callback is is_null() (the default) the output will be printed to the
  // console.
  const PrintCallback& print_callback() const { return print_callback_; }
  void set_print_callback(const PrintCallback& cb) { print_callback_ = cb; }

  // A list of files that can call exec_script(). If the returned pointer is
  // null, exec_script may be called from anywhere.
  const std::set<SourceFile>* exec_script_whitelist() const {
    return exec_script_whitelist_.get();
  }
  void set_exec_script_whitelist(std::unique_ptr<std::set<SourceFile>> list) {
    exec_script_whitelist_ = std::move(list);
  }

 private:
  Label root_target_label_;
  base::FilePath root_path_;
  std::string root_path_utf8_;
  base::FilePath secondary_source_path_;
  base::FilePath python_path_;

  SourceFile build_config_file_;
  SourceFile arg_file_template_path_;
  SourceDir build_dir_;
  Args build_args_;

  ItemDefinedCallback item_defined_callback_;
  PrintCallback print_callback_;

  std::unique_ptr<std::set<SourceFile>> exec_script_whitelist_;

  DISALLOW_ASSIGN(BuildSettings);
};

#endif  // TOOLS_GN_BUILD_SETTINGS_H_
