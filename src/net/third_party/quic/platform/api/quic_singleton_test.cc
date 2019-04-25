// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/api/quic_singleton.h"

#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class Foo {
 public:
  static Foo* GetInstance() { return quic::QuicSingleton<Foo>::get(); }

 private:
  Foo() = default;
  friend quic::QuicSingletonFriend<Foo>;
};

class QuicSingletonTest : public QuicTest {};

TEST_F(QuicSingletonTest, Get) {
  Foo* f1 = Foo::GetInstance();
  Foo* f2 = Foo::GetInstance();
  EXPECT_EQ(f1, f2);
}

}  // namespace
}  // namespace test
}  // namespace quic
