// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/i18n/icu_util.h"
#include "base/logging.h"
#include "base/test/scoped_task_environment.h"

namespace {

// Set up globals that a number of network tests use.
//
// Note that in general static initializers are not allowed, however this is
// just being used by test code.
struct InitGlobals {
  InitGlobals()
      : scoped_task_environment(
            base::test::ScopedTaskEnvironment::MainThreadType::IO) {
    base::CommandLine::Init(0, nullptr);

    // Set up ICU. ICU is used internally by GURL, which is used throughout the
    // //net code. Initializing ICU is important to prevent fuzztests from
    // asserting when handling non-ASCII urls.
    CHECK(base::i18n::InitializeICU());

    // Disable noisy logging as per "libFuzzer in Chrome" documentation:
    // testing/libfuzzer/getting_started.md#Disable-noisy-error-message-logging.
    logging::SetMinLogLevel(logging::LOG_FATAL);
  }

  // A number of tests use async code which depends on there being a
  // ScopedTaskEnvironment.  Setting one up here allows tests to reuse the
  // ScopedTaskEnvironment between runs.
  base::test::ScopedTaskEnvironment scoped_task_environment;

  base::AtExitManager at_exit_manager;
};

InitGlobals* init_globals = new InitGlobals();

}  // namespace
