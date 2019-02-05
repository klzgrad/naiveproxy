// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/test/python_utils.h"

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/process/launch.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(PythonUtils, Clear) {
  std::unique_ptr<base::Environment> env(base::Environment::Create());
  env->SetVar(kPythonPathEnv, "foo");
  EXPECT_TRUE(env->HasVar(kPythonPathEnv));

  ClearPythonPath();
  EXPECT_FALSE(env->HasVar(kPythonPathEnv));
}

TEST(PythonUtils, Append) {
  const base::FilePath::CharType kAppendDir1[] =
      FILE_PATH_LITERAL("test/path_append1");
  const base::FilePath::CharType kAppendDir2[] =
      FILE_PATH_LITERAL("test/path_append2");

  std::unique_ptr<base::Environment> env(base::Environment::Create());

  std::string python_path;
  base::FilePath append_path1(kAppendDir1);
  base::FilePath append_path2(kAppendDir2);

  // Get a clean start
  env->UnSetVar(kPythonPathEnv);

  // Append the path
  AppendToPythonPath(append_path1);
  env->GetVar(kPythonPathEnv, &python_path);
  ASSERT_EQ(python_path, "test/path_append1");

  // Append the safe path again, nothing changes
  AppendToPythonPath(append_path2);
  env->GetVar(kPythonPathEnv, &python_path);
#if defined(OS_WIN)
  ASSERT_EQ(std::string("test/path_append1;test/path_append2"), python_path);
#elif defined(OS_POSIX)
  ASSERT_EQ(std::string("test/path_append1:test/path_append2"), python_path);
#endif
}

TEST(PythonUtils, PythonRunTime) {
  base::CommandLine cmd_line(base::CommandLine::NO_PROGRAM);
  EXPECT_TRUE(GetPythonCommand(&cmd_line));

  // Run a python command to print a string and make sure the output is what
  // we want.
  cmd_line.AppendArg("-c");
  std::string input("PythonUtilsTest");
  std::string python_cmd = base::StringPrintf("print '%s';", input.c_str());
  cmd_line.AppendArg(python_cmd);
  std::string output;
  EXPECT_TRUE(base::GetAppOutput(cmd_line, &output));
  base::TrimWhitespaceASCII(output, base::TRIM_TRAILING, &output);
  EXPECT_EQ(input, output);
}
