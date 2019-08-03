// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/protected_memory.h"
#include "base/cfi_buildflags.h"
#include "base/memory/protected_memory_cfi.h"
#include "base/synchronization/lock.h"
#include "base/test/gtest_util.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

namespace {

struct Data {
  Data() = default;
  Data(int foo_) : foo(foo_) {}
  int foo;
};

}  // namespace

class ProtectedMemoryTest : public ::testing::Test {
 protected:
  // Run tests one at a time. Some of the negative tests can not be made thread
  // safe.
  void SetUp() final { lock.Acquire(); }
  void TearDown() final { lock.Release(); }

  Lock lock;
};

PROTECTED_MEMORY_SECTION ProtectedMemory<int> init;

TEST_F(ProtectedMemoryTest, Initializer) {
  static ProtectedMemory<int>::Initializer I(&init, 4);
  EXPECT_EQ(*init, 4);
}

PROTECTED_MEMORY_SECTION ProtectedMemory<Data> data;

TEST_F(ProtectedMemoryTest, Basic) {
  AutoWritableMemory writer = AutoWritableMemory::Create(data);
  data->foo = 5;
  EXPECT_EQ(data->foo, 5);
}

#if defined(GTEST_HAS_DEATH_TEST) && !defined(OS_ANDROID)

#if PROTECTED_MEMORY_ENABLED
TEST_F(ProtectedMemoryTest, ReadOnlyOnStart) {
  EXPECT_DEATH({ data->foo = 6; AutoWritableMemory::Create(data); }, "");
}

TEST_F(ProtectedMemoryTest, ReadOnlyAfterSetWritable) {
  { AutoWritableMemory writer = AutoWritableMemory::Create(data); }
  EXPECT_DEATH({ data->foo = 7; }, "");
}

TEST_F(ProtectedMemoryTest, AssertMemoryIsReadOnly) {
  AssertMemoryIsReadOnly(&data->foo);
  { AutoWritableMemory::Create(data); }
  AssertMemoryIsReadOnly(&data->foo);

  ProtectedMemory<Data> writable_data;
  EXPECT_DCHECK_DEATH({ AssertMemoryIsReadOnly(&writable_data->foo); });
}

TEST_F(ProtectedMemoryTest, FailsIfDefinedOutsideOfProtectMemoryRegion) {
  ProtectedMemory<Data> data;
  EXPECT_DCHECK_DEATH({ AutoWritableMemory::Create(data); });
}

TEST_F(ProtectedMemoryTest, UnsanitizedCfiCallOutsideOfProtectedMemoryRegion) {
  ProtectedMemory<void (*)(void)> data;
  EXPECT_DCHECK_DEATH({ UnsanitizedCfiCall(data)(); });
}
#endif  // PROTECTED_MEMORY_ENABLED

namespace {

struct BadIcall {
  BadIcall() = default;
  BadIcall(int (*fp_)(int)) : fp(fp_) {}
  int (*fp)(int);
};

unsigned int bad_icall(int i) {
  return 4 + i;
}

}  // namespace

PROTECTED_MEMORY_SECTION ProtectedMemory<BadIcall> icall_pm1;

TEST_F(ProtectedMemoryTest, BadMemberCall) {
  static ProtectedMemory<BadIcall>::Initializer I(
      &icall_pm1, BadIcall(reinterpret_cast<int (*)(int)>(&bad_icall)));

  EXPECT_EQ(UnsanitizedCfiCall(icall_pm1, &BadIcall::fp)(1), 5);
#if !BUILDFLAG(CFI_ICALL_CHECK)
  EXPECT_EQ(icall_pm1->fp(1), 5);
#elif BUILDFLAG(CFI_ENFORCEMENT_TRAP) || BUILDFLAG(CFI_ENFORCEMENT_DIAGNOSTIC)
  EXPECT_DEATH({ icall_pm1->fp(1); }, "");
#endif
}

PROTECTED_MEMORY_SECTION ProtectedMemory<int (*)(int)> icall_pm2;

TEST_F(ProtectedMemoryTest, BadFnPtrCall) {
  static ProtectedMemory<int (*)(int)>::Initializer I(
      &icall_pm2, reinterpret_cast<int (*)(int)>(&bad_icall));

  EXPECT_EQ(UnsanitizedCfiCall(icall_pm2)(1), 5);
#if !BUILDFLAG(CFI_ICALL_CHECK)
  EXPECT_EQ((*icall_pm2)(1), 5);
#elif BUILDFLAG(CFI_ENFORCEMENT_TRAP) || BUILDFLAG(CFI_ENFORCEMENT_DIAGNOSTIC)
  EXPECT_DEATH({ (*icall_pm2)(1); }, "");
#endif
}

#endif  // defined(GTEST_HAS_DEATH_TEST) && !defined(OS_ANDROID)

}  // namespace base
