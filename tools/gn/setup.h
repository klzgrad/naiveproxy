// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_SETUP_H_
#define TOOLS_GN_SETUP_H_

#include <memory>
#include <vector>

#include "base/files/file_path.h"
#include "base/macros.h"
#include "tools/gn/build_settings.h"
#include "tools/gn/builder.h"
#include "tools/gn/label_pattern.h"
#include "tools/gn/loader.h"
#include "tools/gn/scheduler.h"
#include "tools/gn/scope.h"
#include "tools/gn/settings.h"
#include "tools/gn/token.h"
#include "tools/gn/toolchain.h"

class InputFile;
class ParseNode;

namespace base {
class CommandLine;
}

extern const char kDotfile_Help[];

// Helper class to setup the build settings and environment for the various
// commands to run.
class Setup {
 public:
  Setup();
  ~Setup();

  // Configures the build for the current command line. On success returns
  // true. On failure, prints the error and returns false.
  //
  // The parameter is the string the user specified for the build directory. We
  // will try to interpret this as a SourceDir if possible, and will fail if is
  // is malformed.
  //
  // With force_create = false, setup will fail if the build directory doesn't
  // alreay exist with an args file in it. With force_create set to true, the
  // directory will be created if necessary. Commands explicitly doing
  // generation should set this to true to create it, but querying commands
  // should set it to false to prevent creating oddly-named directories in case
  // the user omits the build directory argument (which is easy to do).
  bool DoSetup(const std::string& build_dir, bool force_create);

  // Runs the load, returning true on success. On failure, prints the error
  // and returns false. This includes both RunPreMessageLoop() and
  // RunPostMessageLoop().
  bool Run();

  Scheduler& scheduler() { return scheduler_; }

  // Returns the file used to store the build arguments. Note that the path
  // might not exist.
  SourceFile GetBuildArgFile() const;

  // Sets whether the build arguments should be filled during setup from the
  // command line/build argument file. This will be true by default. The use
  // case for setting it to false is when editing build arguments, we don't
  // want to rely on them being valid.
  void set_fill_arguments(bool fa) { fill_arguments_ = fa; }

  // After a successful run, setting this will additionally cause the public
  // headers to be checked. Defaults to false.
  void set_check_public_headers(bool s) {
    check_public_headers_ = s;
  }

  // Read from the .gn file, these are the targets to check. If the .gn file
  // does not specify anything, this will be null. If the .gn file specifies
  // the empty list, this will be non-null but empty.
  const std::vector<LabelPattern>* check_patterns() const {
    return check_patterns_.get();
  }

  BuildSettings& build_settings() { return build_settings_; }
  Builder& builder() { return builder_; }
  LoaderImpl* loader() { return loader_.get(); }

  // Name of the file in the root build directory that contains the build
  // arguements.
  static const char kBuildArgFileName[];

 private:
  // Performs the two sets of operations to run the generation before and after
  // the message loop is run.
  void RunPreMessageLoop();
  bool RunPostMessageLoop();

  // Fills build arguments. Returns true on success.
  bool FillArguments(const base::CommandLine& cmdline);

  // Fills the build arguments from the command line or from the build arg file.
  bool FillArgsFromCommandLine(const std::string& args);
  bool FillArgsFromFile();

  // Given an already-loaded args_input_file_, parses and saves the resulting
  // arguments. Backend for the different FillArgs variants.
  bool FillArgsFromArgsInputFile();

  // Writes the build arguments to the build arg file.
  bool SaveArgsToFile();

  // Fills the root directory into the settings. Returns true on success.
  bool FillSourceDir(const base::CommandLine& cmdline);

  // Fills the build directory given the value the user has specified.
  // Must happen after FillSourceDir so we can resolve source-relative
  // paths. If require_exists is false, it will fail if the dir doesn't exist.
  bool FillBuildDir(const std::string& build_dir, bool require_exists);

  // Fills the python path portion of the command line. On failure, sets
  // it to just "python".
  bool FillPythonPath(const base::CommandLine& cmdline);

  // Run config file.
  bool RunConfigFile();

  bool FillOtherConfig(const base::CommandLine& cmdline);

  BuildSettings build_settings_;
  scoped_refptr<LoaderImpl> loader_;
  Builder builder_;

  SourceFile root_build_file_;

  bool check_public_headers_;

  // See getter for info.
  std::unique_ptr<std::vector<LabelPattern>> check_patterns_;

  Scheduler scheduler_;

  // These settings and toolchain are used to interpret the command line and
  // dot file.
  Settings dotfile_settings_;
  Scope dotfile_scope_;

  // State for invoking the dotfile.
  base::FilePath dotfile_name_;
  std::unique_ptr<InputFile> dotfile_input_file_;
  std::vector<Token> dotfile_tokens_;
  std::unique_ptr<ParseNode> dotfile_root_;

  // Default overrides, specified in the dotfile.
  // Owned by the Value (if it exists) in the dotfile_scope_.
  const Scope* default_args_;

  // Set to true when we should populate the build arguments from the command
  // line or build argument file. See setter above.
  bool fill_arguments_;

  // State for invoking the command line args. We specifically want to keep
  // this around for the entire run so that Values can blame to the command
  // line when we issue errors about them.
  std::unique_ptr<InputFile> args_input_file_;
  std::vector<Token> args_tokens_;
  std::unique_ptr<ParseNode> args_root_;

  DISALLOW_COPY_AND_ASSIGN(Setup);
};

#endif  // TOOLS_GN_SETUP_H_
