// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iterator>
#include <set>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "tools/gn/analyzer.h"
#include "tools/gn/commands.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/location.h"
#include "tools/gn/setup.h"

namespace commands {

const char kAnalyze[] = "analyze";
const char kAnalyze_HelpShort[] =
    "analyze: Analyze which targets are affected by a list of files.";
const char kAnalyze_Help[] =
    R"(gn analyze <out_dir> <input_path> <output_path>

  Analyze which targets are affected by a list of files.

  This command takes three arguments:

  out_dir is the path to the build directory.

  input_path is a path to a file containing a JSON object with three fields:

   - "files": A list of the filenames to check.

   - "test_targets": A list of the labels for targets that are needed to run
     the tests we wish to run.

   - "additional_compile_targets": A list of the labels for targets that we
     wish to rebuild, but aren't necessarily needed for testing. The important
     difference between this field and "test_targets" is that if an item in
     the additional_compile_targets list refers to a group, then any
     dependencies of that group will be returned if they are out of date, but
     the group itself does not need to be. If the dependencies themselves are
     groups, the same filtering is repeated. This filtering can be used to
     avoid rebuilding dependencies of a group that are unaffected by the input
     files. The list may also contain the string "all" to refer to a
     pseudo-group that contains every root target in the build graph.

     This filtering behavior is also known as "pruning" the list of compile
     targets.

  output_path is a path indicating where the results of the command are to be
  written. The results will be a file containing a JSON object with one or more
  of following fields:

   - "compile_targets": A list of the labels derived from the input
     compile_targets list that are affected by the input files. Due to the way
     the filtering works for compile targets as described above, this list may
     contain targets that do not appear in the input list.

   - "test_targets": A list of the labels from the input test_targets list that
     are affected by the input files. This list will be a proper subset of the
     input list.

   - "invalid_targets": A list of any names from the input that do not exist in
     the build graph. If this list is non-empty, the "error" field will also be
     set to "Invalid targets".

   - "status": A string containing one of three values:

       - "Found dependency"
       - "No dependency"
       - "Found dependency (all) "

     In the first case, the lists returned in compile_targets and test_targets
     should be passed to ninja to build. In the second case, nothing was
     affected and no build is necessary. In the third case, GN could not
     determine the correct answer and returned the input as the output in order
     to be safe.

   - "error": This will only be present if an error occurred, and will contain
     a string describing the error. This includes cases where the input file is
     not in the right format, or contains invalid targets.

  The command returns 1 if it is unable to read the input file or write the
  output file, or if there is something wrong with the build such that gen
  would also fail, and 0 otherwise. In particular, it returns 0 even if the
  "error" key is non-empty and a non-fatal error occurred. In other words, it
  tries really hard to always write something to the output JSON and convey
  errors that way rather than via return codes.
)";

int RunAnalyze(const std::vector<std::string>& args) {
  if (args.size() != 3) {
    Err(Location(), "You're holding it wrong.",
        "Usage: \"gn analyze <out_dir> <input_path> <output_path>")
        .PrintToStdout();
    return 1;
  }

  std::string input;
  bool ret = base::ReadFileToString(UTF8ToFilePath(args[1]), &input);
  if (!ret) {
    Err(Location(), "Input file " + args[1] + " not found.").PrintToStdout();
    return 1;
  }

  Setup* setup = new Setup;
  if (!setup->DoSetup(args[0], false) || !setup->Run())
    return 1;

  Analyzer analyzer(setup->builder());

  Err err;
  std::string output = Analyzer(setup->builder()).Analyze(input, &err);
  if (err.has_error()) {
    err.PrintToStdout();
    return 1;
  }

  WriteFile(UTF8ToFilePath(args[2]), output, &err);
  if (err.has_error()) {
    err.PrintToStdout();
    return 1;
  }

  return 0;
}

}  // namespace commands
