// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/bind_test_util.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/location.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace {

// A helper class for MakeExpectedRunClosure() that fails if it is
// destroyed without Run() having been called.  This class may be used
// from multiple threads as long as Run() is called at most once
// before destruction.
class RunChecker {
 public:
  explicit RunChecker(const Location& location)
      : location_(location), called_(false) {}

  ~RunChecker() {
    if (!called_) {
      ADD_FAILURE_AT(location_.file_name(), location_.line_number());
    }
  }

  void Run() {
    DCHECK(!called_);
    called_ = true;
  }

 private:
  Location location_;
  bool called_;
};

}  // namespace

OnceClosure MakeExpectedRunClosure(const Location& location) {
  return BindOnce(&RunChecker::Run, Owned(new RunChecker(location)));
}

OnceClosure MakeExpectedNotRunClosure(const Location& location) {
  return BindOnce(
      [](const Location& location) {
        ADD_FAILURE_AT(location.file_name(), location.line_number());
      },
      location);
}

}  // namespace base
