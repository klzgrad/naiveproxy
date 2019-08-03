// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/typed_event_handler.h"

#include <windows.foundation.h>

#include "base/test/bind_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace win {

TEST(TypedEventHandlerTest, InvokeSuccess) {
  bool called_callback = false;
  TypedEventHandler<IInspectable*, IInspectable*> handler(
      base::BindLambdaForTesting([&](IInspectable* sender, IInspectable* args) {
        EXPECT_EQ(reinterpret_cast<IInspectable*>(0x01), sender);
        EXPECT_EQ(reinterpret_cast<IInspectable*>(0x02), args);
        called_callback = true;
        return S_OK;
      }));

  EXPECT_FALSE(called_callback);
  HRESULT hr = handler.Invoke(reinterpret_cast<IInspectable*>(0x01),
                              reinterpret_cast<IInspectable*>(0x02));
  EXPECT_TRUE(called_callback);
  EXPECT_EQ(S_OK, hr);
}

TEST(TypedEventHandlerTest, InvokeFail) {
  bool called_callback = false;
  TypedEventHandler<IInspectable*, IInspectable*> handler(
      base::BindLambdaForTesting([&](IInspectable* sender, IInspectable* args) {
        EXPECT_EQ(nullptr, sender);
        EXPECT_EQ(nullptr, args);
        called_callback = true;
        return E_FAIL;
      }));

  EXPECT_FALSE(called_callback);
  HRESULT hr = handler.Invoke(nullptr, nullptr);
  EXPECT_TRUE(called_callback);
  EXPECT_EQ(E_FAIL, hr);
}

}  // namespace win
}  // namespace base
