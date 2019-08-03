// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_reference_counted.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class Base : public QuicReferenceCounted {
 public:
  explicit Base(bool* destroyed) : destroyed_(destroyed) {
    *destroyed_ = false;
  }

 protected:
  ~Base() override { *destroyed_ = true; }

 private:
  bool* destroyed_;
};

class Derived : public Base {
 public:
  explicit Derived(bool* destroyed) : Base(destroyed) {}

 private:
  ~Derived() override {}
};

class QuicReferenceCountedTest : public QuicTest {};

TEST_F(QuicReferenceCountedTest, DefaultConstructor) {
  QuicReferenceCountedPointer<Base> a;
  EXPECT_EQ(nullptr, a);
  EXPECT_EQ(nullptr, a.get());
  EXPECT_FALSE(a);
}

TEST_F(QuicReferenceCountedTest, ConstructFromRawPointer) {
  bool destroyed = false;
  {
    QuicReferenceCountedPointer<Base> a(new Base(&destroyed));
    EXPECT_FALSE(destroyed);
  }
  EXPECT_TRUE(destroyed);
}

TEST_F(QuicReferenceCountedTest, RawPointerAssignment) {
  bool destroyed = false;
  {
    QuicReferenceCountedPointer<Base> a;
    Base* rct = new Base(&destroyed);
    a = rct;
    EXPECT_FALSE(destroyed);
  }
  EXPECT_TRUE(destroyed);
}

TEST_F(QuicReferenceCountedTest, PointerCopy) {
  bool destroyed = false;
  {
    QuicReferenceCountedPointer<Base> a(new Base(&destroyed));
    {
      QuicReferenceCountedPointer<Base> b(a);
      EXPECT_EQ(a, b);
      EXPECT_FALSE(destroyed);
    }
    EXPECT_FALSE(destroyed);
  }
  EXPECT_TRUE(destroyed);
}

TEST_F(QuicReferenceCountedTest, PointerCopyAssignment) {
  bool destroyed = false;
  {
    QuicReferenceCountedPointer<Base> a(new Base(&destroyed));
    {
      QuicReferenceCountedPointer<Base> b = a;
      EXPECT_EQ(a, b);
      EXPECT_FALSE(destroyed);
    }
    EXPECT_FALSE(destroyed);
  }
  EXPECT_TRUE(destroyed);
}

TEST_F(QuicReferenceCountedTest, PointerCopyFromOtherType) {
  bool destroyed = false;
  {
    QuicReferenceCountedPointer<Derived> a(new Derived(&destroyed));
    {
      QuicReferenceCountedPointer<Base> b(a);
      EXPECT_EQ(a.get(), b.get());
      EXPECT_FALSE(destroyed);
    }
    EXPECT_FALSE(destroyed);
  }
  EXPECT_TRUE(destroyed);
}

TEST_F(QuicReferenceCountedTest, PointerCopyAssignmentFromOtherType) {
  bool destroyed = false;
  {
    QuicReferenceCountedPointer<Derived> a(new Derived(&destroyed));
    {
      QuicReferenceCountedPointer<Base> b = a;
      EXPECT_EQ(a.get(), b.get());
      EXPECT_FALSE(destroyed);
    }
    EXPECT_FALSE(destroyed);
  }
  EXPECT_TRUE(destroyed);
}

TEST_F(QuicReferenceCountedTest, PointerMove) {
  bool destroyed = false;
  QuicReferenceCountedPointer<Base> a(new Derived(&destroyed));
  EXPECT_FALSE(destroyed);
  QuicReferenceCountedPointer<Base> b(std::move(a));
  EXPECT_FALSE(destroyed);
  EXPECT_NE(nullptr, b);
  EXPECT_EQ(nullptr, a);  // NOLINT

  b = nullptr;
  EXPECT_TRUE(destroyed);
}

TEST_F(QuicReferenceCountedTest, PointerMoveAssignment) {
  bool destroyed = false;
  QuicReferenceCountedPointer<Base> a(new Derived(&destroyed));
  EXPECT_FALSE(destroyed);
  QuicReferenceCountedPointer<Base> b = std::move(a);
  EXPECT_FALSE(destroyed);
  EXPECT_NE(nullptr, b);
  EXPECT_EQ(nullptr, a);  // NOLINT

  b = nullptr;
  EXPECT_TRUE(destroyed);
}

TEST_F(QuicReferenceCountedTest, PointerMoveFromOtherType) {
  bool destroyed = false;
  QuicReferenceCountedPointer<Derived> a(new Derived(&destroyed));
  EXPECT_FALSE(destroyed);
  QuicReferenceCountedPointer<Base> b(std::move(a));
  EXPECT_FALSE(destroyed);
  EXPECT_NE(nullptr, b);
  EXPECT_EQ(nullptr, a);  // NOLINT

  b = nullptr;
  EXPECT_TRUE(destroyed);
}

TEST_F(QuicReferenceCountedTest, PointerMoveAssignmentFromOtherType) {
  bool destroyed = false;
  QuicReferenceCountedPointer<Derived> a(new Derived(&destroyed));
  EXPECT_FALSE(destroyed);
  QuicReferenceCountedPointer<Base> b = std::move(a);
  EXPECT_FALSE(destroyed);
  EXPECT_NE(nullptr, b);
  EXPECT_EQ(nullptr, a);  // NOLINT

  b = nullptr;
  EXPECT_TRUE(destroyed);
}

}  // namespace
}  // namespace test
}  // namespace quic
