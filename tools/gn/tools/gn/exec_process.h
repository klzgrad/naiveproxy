// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_EXEC_PROCESS_H_
#define TOOLS_GN_EXEC_PROCESS_H_

#include <string>

#include "base/strings/string16.h"
#include "util/build_config.h"

namespace base {
class CommandLine;
class FilePath;
}  // namespace base

namespace internal {

bool ExecProcess(const base::CommandLine& cmdline,
                 const base::FilePath& startup_dir,
                 std::string* std_out,
                 std::string* std_err,
                 int* exit_code);

#if defined(OS_WIN)
bool ExecProcess(const base::string16& cmdline_str,
                 const base::FilePath& startup_dir,
                 std::string* std_out,
                 std::string* std_err,
                 int* exit_code);
#endif  // OS_WIN

}  // namespace internal

#endif  // TOOLS_GN_EXEC_PROCESS_H_
