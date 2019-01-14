// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/win_util.h"

#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/scoped_native_library.h"
#include "base/stl_util.h"
#include "base/win/win_client_metrics.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace win {

namespace {

// Saves the current thread's locale ID when initialized, and restores it when
// the instance is going out of scope.
class ThreadLocaleSaver {
 public:
  ThreadLocaleSaver() : original_locale_id_(GetThreadLocale()) {}
  ~ThreadLocaleSaver() { SetThreadLocale(original_locale_id_); }

 private:
  LCID original_locale_id_;

  DISALLOW_COPY_AND_ASSIGN(ThreadLocaleSaver);
};

}  // namespace

// The test is somewhat silly, because some bots some have UAC enabled and some
// have it disabled. At least we check that it does not crash.
TEST(BaseWinUtilTest, TestIsUACEnabled) {
  UserAccountControlIsEnabled();
}

TEST(BaseWinUtilTest, TestGetUserSidString) {
  std::wstring user_sid;
  EXPECT_TRUE(GetUserSidString(&user_sid));
  EXPECT_TRUE(!user_sid.empty());
}

TEST(BaseWinUtilTest, TestGetNonClientMetrics) {
  NONCLIENTMETRICS_XP metrics = {0};
  GetNonClientMetrics(&metrics);
  EXPECT_GT(metrics.cbSize, 0u);
  EXPECT_GT(metrics.iScrollWidth, 0);
  EXPECT_GT(metrics.iScrollHeight, 0);
}

TEST(BaseWinUtilTest, TestGetLoadedModulesSnapshot) {
  std::vector<HMODULE> snapshot;

  ASSERT_TRUE(GetLoadedModulesSnapshot(::GetCurrentProcess(), &snapshot));
  size_t original_snapshot_size = snapshot.size();
  ASSERT_GT(original_snapshot_size, 0u);
  snapshot.clear();

  // Load in a new module. Pick msvidc32.dll as it is present from WinXP to
  // Win10 and yet rarely used.
  const wchar_t dll_name[] = L"msvidc32.dll";
  ASSERT_EQ(NULL, ::GetModuleHandle(dll_name));

  base::ScopedNativeLibrary new_dll((base::FilePath(dll_name)));
  ASSERT_NE(static_cast<HMODULE>(NULL), new_dll.get());
  ASSERT_TRUE(GetLoadedModulesSnapshot(::GetCurrentProcess(), &snapshot));
  ASSERT_GT(snapshot.size(), original_snapshot_size);
  ASSERT_TRUE(base::ContainsValue(snapshot, new_dll.get()));
}

TEST(BaseWinUtilTest, TestUint32ToInvalidHandle) {
  // Ensure that INVALID_HANDLE_VALUE is preserved when going to a 32-bit value
  // and back on 64-bit platforms.
  uint32_t invalid_handle = base::win::HandleToUint32(INVALID_HANDLE_VALUE);
  EXPECT_EQ(INVALID_HANDLE_VALUE, base::win::Uint32ToHandle(invalid_handle));
}

}  // namespace win
}  // namespace base
