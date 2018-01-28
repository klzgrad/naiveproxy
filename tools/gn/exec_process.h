// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_EXEC_PROCESS_H_
#define TOOLS_GN_EXEC_PROCESS_H_

#include <string>

namespace base {
class CommandLine;
class FilePath;
}

namespace internal {

bool ExecProcess(const base::CommandLine& cmdline,
                 const base::FilePath& startup_dir,
                 std::string* std_out,
                 std::string* std_err,
                 int* exit_code);

}  // namespace internal

#endif  // TOOLS_GN_EXEC_PROCESS_H_
