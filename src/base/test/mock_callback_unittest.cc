// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/mock_callback.h"

#include "base/callback.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::InSequence;
using testing::Return;

namespace base {
namespace {

TEST(MockCallbackTest, ZeroArgs) {
  MockCallback<Closure> mock_closure;
  EXPECT_CALL(mock_closure, Run());
  mock_closure.Get().Run();

  MockCallback<Callback<int()>> mock_int_callback;
  {
    InSequence sequence;
    EXPECT_CALL(mock_int_callback, Run()).WillOnce(Return(42));
    EXPECT_CALL(mock_int_callback, Run()).WillOnce(Return(88));
  }
  EXPECT_EQ(42, mock_int_callback.Get().Run());
  EXPECT_EQ(88, mock_int_callback.Get().Run());
}

TEST(MockCallbackTest, WithArgs) {
  MockCallback<Callback<int(int, int)>> mock_two_int_callback;
  EXPECT_CALL(mock_two_int_callback, Run(1, 2)).WillOnce(Return(42));
  EXPECT_CALL(mock_two_int_callback, Run(0, 0)).WillRepeatedly(Return(-1));
  Callback<int(int, int)> two_int_callback = mock_two_int_callback.Get();
  EXPECT_EQ(-1, two_int_callback.Run(0, 0));
  EXPECT_EQ(42, two_int_callback.Run(1, 2));
  EXPECT_EQ(-1, two_int_callback.Run(0, 0));
}

TEST(MockCallbackTest, ZeroArgsOnce) {
  MockCallback<OnceClosure> mock_closure;
  EXPECT_CALL(mock_closure, Run());
  mock_closure.Get().Run();

  MockCallback<OnceCallback<int()>> mock_int_callback;
  EXPECT_CALL(mock_int_callback, Run()).WillOnce(Return(88));
  EXPECT_EQ(88, mock_int_callback.Get().Run());
}

TEST(MockCallbackTest, WithArgsOnce) {
  MockCallback<OnceCallback<int(int, int)>> mock_two_int_callback;
  EXPECT_CALL(mock_two_int_callback, Run(1, 2)).WillOnce(Return(42));
  OnceCallback<int(int, int)> two_int_callback = mock_two_int_callback.Get();
  EXPECT_EQ(42, std::move(two_int_callback).Run(1, 2));
}

}  // namespace
}  // namespace base
