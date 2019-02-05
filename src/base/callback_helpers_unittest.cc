// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/callback_helpers.h"

#include "base/bind.h"
#include "base/callback.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

void Increment(int* value) {
  (*value)++;
}

TEST(CallbackHelpersTest, TestResetAndReturn) {
  int run_count = 0;

  base::Closure cb = base::Bind(&Increment, &run_count);
  EXPECT_EQ(0, run_count);
  base::ResetAndReturn(&cb).Run();
  EXPECT_EQ(1, run_count);
  EXPECT_FALSE(cb);

  run_count = 0;

  base::OnceClosure cb2 = base::BindOnce(&Increment, &run_count);
  EXPECT_EQ(0, run_count);
  base::ResetAndReturn(&cb2).Run();
  EXPECT_EQ(1, run_count);
  EXPECT_FALSE(cb2);
}

TEST(CallbackHelpersTest, TestScopedClosureRunnerExitScope) {
  int run_count = 0;
  {
    base::ScopedClosureRunner runner(base::Bind(&Increment, &run_count));
    EXPECT_EQ(0, run_count);
  }
  EXPECT_EQ(1, run_count);
}

TEST(CallbackHelpersTest, TestScopedClosureRunnerRelease) {
  int run_count = 0;
  base::OnceClosure c;
  {
    base::ScopedClosureRunner runner(base::Bind(&Increment, &run_count));
    c = runner.Release();
    EXPECT_EQ(0, run_count);
  }
  EXPECT_EQ(0, run_count);
  std::move(c).Run();
  EXPECT_EQ(1, run_count);
}

TEST(CallbackHelpersTest, TestScopedClosureRunnerReplaceClosure) {
  int run_count_1 = 0;
  int run_count_2 = 0;
  {
    base::ScopedClosureRunner runner;
    runner.ReplaceClosure(base::Bind(&Increment, &run_count_1));
    runner.ReplaceClosure(base::Bind(&Increment, &run_count_2));
    EXPECT_EQ(0, run_count_1);
    EXPECT_EQ(0, run_count_2);
  }
  EXPECT_EQ(0, run_count_1);
  EXPECT_EQ(1, run_count_2);
}

TEST(CallbackHelpersTest, TestScopedClosureRunnerRunAndReset) {
  int run_count_3 = 0;
  {
    base::ScopedClosureRunner runner(base::Bind(&Increment, &run_count_3));
    EXPECT_EQ(0, run_count_3);
    runner.RunAndReset();
    EXPECT_EQ(1, run_count_3);
  }
  EXPECT_EQ(1, run_count_3);
}

TEST(CallbackHelpersTest, TestScopedClosureRunnerMoveConstructor) {
  int run_count = 0;
  {
    std::unique_ptr<base::ScopedClosureRunner> runner(
        new base::ScopedClosureRunner(base::Bind(&Increment, &run_count)));
    base::ScopedClosureRunner runner2(std::move(*runner));
    runner.reset();
    EXPECT_EQ(0, run_count);
  }
  EXPECT_EQ(1, run_count);
}

TEST(CallbackHelpersTest, TestScopedClosureRunnerMoveAssignment) {
  int run_count_1 = 0;
  int run_count_2 = 0;
  {
    base::ScopedClosureRunner runner(base::Bind(&Increment, &run_count_1));
    {
      base::ScopedClosureRunner runner2(base::Bind(&Increment, &run_count_2));
      runner = std::move(runner2);
      EXPECT_EQ(0, run_count_1);
      EXPECT_EQ(0, run_count_2);
    }
    EXPECT_EQ(0, run_count_1);
    EXPECT_EQ(0, run_count_2);
  }
  EXPECT_EQ(0, run_count_1);
  EXPECT_EQ(1, run_count_2);
}

TEST(CallbackHelpersTest, TestAdaptCallbackForRepeating) {
  int count = 0;
  base::OnceCallback<void(int*)> cb =
      base::BindOnce([](int* count) { ++*count; });

  base::RepeatingCallback<void(int*)> wrapped =
      base::AdaptCallbackForRepeating(std::move(cb));

  EXPECT_EQ(0, count);
  wrapped.Run(&count);
  EXPECT_EQ(1, count);
  wrapped.Run(&count);
  EXPECT_EQ(1, count);
}

}  // namespace
