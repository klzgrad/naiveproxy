// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TEST_PYTHON_UTILS_H_
#define NET_TEST_PYTHON_UTILS_H_

#include "base/compiler_specific.h"

namespace base {
class CommandLine;
class FilePath;
}

// This is the python path variable name.
extern const char kPythonPathEnv[];

// Clears the python path, this is useful for test hermeticity.
void ClearPythonPath();

// Appends the dir to python path environment variable.
void AppendToPythonPath(const base::FilePath& dir);

// Return the location of the compiler-generated python protobuf.
bool GetPyProtoPath(base::FilePath* dir);

// Returns if a virtualenv is currently active.
bool IsInPythonVirtualEnv();

// Returns the command that should be used to launch Python.
bool GetPythonCommand(base::CommandLine* python_cmd) WARN_UNUSED_RESULT;

#endif  // NET_TEST_PYTHON_UTILS_H_
