// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/registry.h"

#include <stdint.h>
#include <windows.h>

#include <cstring>
#include <vector>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "base/test/scoped_task_environment.h"
#include "base/win/windows_version.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace win {

namespace {

const char16 kRootKey[] = STRING16_LITERAL("Base_Registry_Unittest");

class RegistryTest : public testing::Test {
 protected:
#if defined(_WIN64)
  static const REGSAM kNativeViewMask = KEY_WOW64_64KEY;
  static const REGSAM kRedirectedViewMask = KEY_WOW64_32KEY;
#else
  static const REGSAM kNativeViewMask = KEY_WOW64_32KEY;
  static const REGSAM kRedirectedViewMask = KEY_WOW64_64KEY;
#endif  //  _WIN64

  RegistryTest() {}
  void SetUp() override {
    // Create a temporary key.
    RegKey key(HKEY_CURRENT_USER, STRING16_LITERAL(""), KEY_ALL_ACCESS);
    key.DeleteKey(kRootKey);
    ASSERT_NE(ERROR_SUCCESS, key.Open(HKEY_CURRENT_USER, kRootKey, KEY_READ));
    ASSERT_EQ(ERROR_SUCCESS, key.Create(HKEY_CURRENT_USER, kRootKey, KEY_READ));
    foo_software_key_ = STRING16_LITERAL("Software\\");
    foo_software_key_ += kRootKey;
    foo_software_key_ += STRING16_LITERAL("\\Foo");
  }

  void TearDown() override {
    // Clean up the temporary key.
    RegKey key(HKEY_CURRENT_USER, STRING16_LITERAL(""), KEY_SET_VALUE);
    ASSERT_EQ(ERROR_SUCCESS, key.DeleteKey(kRootKey));
    ASSERT_NE(ERROR_SUCCESS, key.Open(HKEY_CURRENT_USER, kRootKey, KEY_READ));
  }

  static bool IsRedirectorPresent() {
#if defined(_WIN64)
    return true;
#else
    return OSInfo::GetInstance()->wow64_status() == OSInfo::WOW64_ENABLED;
#endif
  }

  string16 foo_software_key_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RegistryTest);
};

// static
const REGSAM RegistryTest::kNativeViewMask;
const REGSAM RegistryTest::kRedirectedViewMask;

TEST_F(RegistryTest, ValueTest) {
  RegKey key;

  string16 foo_key(kRootKey);
  foo_key += STRING16_LITERAL("\\Foo");
  ASSERT_EQ(ERROR_SUCCESS, key.Create(HKEY_CURRENT_USER, foo_key.c_str(),
                                      KEY_READ));

  {
    ASSERT_EQ(ERROR_SUCCESS, key.Open(HKEY_CURRENT_USER, foo_key.c_str(),
                                      KEY_READ | KEY_SET_VALUE));
    ASSERT_TRUE(key.Valid());

    const char16 kStringValueName[] = STRING16_LITERAL("StringValue");
    const char16 kDWORDValueName[] = STRING16_LITERAL("DWORDValue");
    const char16 kInt64ValueName[] = STRING16_LITERAL("Int64Value");
    const char16 kStringData[] = STRING16_LITERAL("string data");
    const DWORD kDWORDData = 0xdeadbabe;
    const int64_t kInt64Data = 0xdeadbabedeadbabeLL;

    // Test value creation
    ASSERT_EQ(ERROR_SUCCESS, key.WriteValue(kStringValueName, kStringData));
    ASSERT_EQ(ERROR_SUCCESS, key.WriteValue(kDWORDValueName, kDWORDData));
    ASSERT_EQ(ERROR_SUCCESS, key.WriteValue(kInt64ValueName, &kInt64Data,
                                            sizeof(kInt64Data), REG_QWORD));
    EXPECT_EQ(3U, key.GetValueCount());
    EXPECT_TRUE(key.HasValue(kStringValueName));
    EXPECT_TRUE(key.HasValue(kDWORDValueName));
    EXPECT_TRUE(key.HasValue(kInt64ValueName));

    // Test Read
    string16 string_value;
    DWORD dword_value = 0;
    int64_t int64_value = 0;
    ASSERT_EQ(ERROR_SUCCESS, key.ReadValue(kStringValueName, &string_value));
    ASSERT_EQ(ERROR_SUCCESS, key.ReadValueDW(kDWORDValueName, &dword_value));
    ASSERT_EQ(ERROR_SUCCESS, key.ReadInt64(kInt64ValueName, &int64_value));
    EXPECT_EQ(kStringData, string_value);
    EXPECT_EQ(kDWORDData, dword_value);
    EXPECT_EQ(kInt64Data, int64_value);

    // Make sure out args are not touched if ReadValue fails
    const char16* kNonExistent = STRING16_LITERAL("NonExistent");
    ASSERT_NE(ERROR_SUCCESS, key.ReadValue(kNonExistent, &string_value));
    ASSERT_NE(ERROR_SUCCESS, key.ReadValueDW(kNonExistent, &dword_value));
    ASSERT_NE(ERROR_SUCCESS, key.ReadInt64(kNonExistent, &int64_value));
    EXPECT_EQ(kStringData, string_value);
    EXPECT_EQ(kDWORDData, dword_value);
    EXPECT_EQ(kInt64Data, int64_value);

    // Test delete
    ASSERT_EQ(ERROR_SUCCESS, key.DeleteValue(kStringValueName));
    ASSERT_EQ(ERROR_SUCCESS, key.DeleteValue(kDWORDValueName));
    ASSERT_EQ(ERROR_SUCCESS, key.DeleteValue(kInt64ValueName));
    EXPECT_EQ(0U, key.GetValueCount());
    EXPECT_FALSE(key.HasValue(kStringValueName));
    EXPECT_FALSE(key.HasValue(kDWORDValueName));
    EXPECT_FALSE(key.HasValue(kInt64ValueName));
  }
}

TEST_F(RegistryTest, BigValueIteratorTest) {
  RegKey key;
  string16 foo_key(kRootKey);
  foo_key += STRING16_LITERAL("\\Foo");
  ASSERT_EQ(ERROR_SUCCESS, key.Create(HKEY_CURRENT_USER, foo_key.c_str(),
                                      KEY_READ));
  ASSERT_EQ(ERROR_SUCCESS, key.Open(HKEY_CURRENT_USER, foo_key.c_str(),
                                    KEY_READ | KEY_SET_VALUE));
  ASSERT_TRUE(key.Valid());

  // Create a test value that is larger than MAX_PATH.
  string16 data(MAX_PATH * 2, 'a');

  ASSERT_EQ(ERROR_SUCCESS, key.WriteValue(data.c_str(), data.c_str()));

  RegistryValueIterator iterator(HKEY_CURRENT_USER, foo_key.c_str());
  ASSERT_TRUE(iterator.Valid());
  EXPECT_EQ(data, iterator.Name());
  EXPECT_EQ(data, iterator.Value());
  // ValueSize() is in bytes, including NUL.
  EXPECT_EQ((MAX_PATH * 2 + 1) * sizeof(char16), iterator.ValueSize());
  ++iterator;
  EXPECT_FALSE(iterator.Valid());
}

TEST_F(RegistryTest, TruncatedCharTest) {
  RegKey key;
  string16 foo_key(kRootKey);
  foo_key += STRING16_LITERAL("\\Foo");
  ASSERT_EQ(ERROR_SUCCESS, key.Create(HKEY_CURRENT_USER, foo_key.c_str(),
                                      KEY_READ));
  ASSERT_EQ(ERROR_SUCCESS, key.Open(HKEY_CURRENT_USER, foo_key.c_str(),
                                    KEY_READ | KEY_SET_VALUE));
  ASSERT_TRUE(key.Valid());

  const char16 kName[] = STRING16_LITERAL("name");
  // kData size is not a multiple of sizeof(char16).
  const uint8_t kData[] = {1, 2, 3, 4, 5};
  EXPECT_EQ(5u, size(kData));
  ASSERT_EQ(ERROR_SUCCESS,
            key.WriteValue(kName, kData, size(kData), REG_BINARY));

  RegistryValueIterator iterator(HKEY_CURRENT_USER, foo_key.c_str());
  ASSERT_TRUE(iterator.Valid());
  // Avoid having to use EXPECT_STREQ here by leveraging StringPiece's
  // operator== to perform a deep comparison.
  EXPECT_EQ(StringPiece16(kName), StringPiece16(iterator.Name()));
  // ValueSize() is in bytes.
  ASSERT_EQ(size(kData), iterator.ValueSize());
  // Value() is NUL terminated.
  int end = (iterator.ValueSize() + sizeof(char16) - 1) / sizeof(char16);
  EXPECT_NE('\0', iterator.Value()[end - 1]);
  EXPECT_EQ('\0', iterator.Value()[end]);
  EXPECT_EQ(0, std::memcmp(kData, iterator.Value(), size(kData)));
  ++iterator;
  EXPECT_FALSE(iterator.Valid());
}

TEST_F(RegistryTest, RecursiveDelete) {
  RegKey key;
  // Create kRootKey->Foo
  //                  \->Bar (TestValue)
  //                     \->Foo (TestValue)
  //                        \->Bar
  //                           \->Foo
  //                  \->Moo
  //                  \->Foo
  // and delete kRootKey->Foo
  string16 foo_key(kRootKey);
  foo_key += STRING16_LITERAL("\\Foo");
  ASSERT_EQ(ERROR_SUCCESS,
            key.Create(HKEY_CURRENT_USER, foo_key.c_str(), KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS, key.CreateKey(STRING16_LITERAL("Bar"), KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS, key.WriteValue(STRING16_LITERAL("TestValue"),
                                          STRING16_LITERAL("TestData")));
  ASSERT_EQ(ERROR_SUCCESS,
            key.Create(HKEY_CURRENT_USER, foo_key.c_str(), KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS, key.CreateKey(STRING16_LITERAL("Moo"), KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS,
            key.Create(HKEY_CURRENT_USER, foo_key.c_str(), KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS, key.CreateKey(STRING16_LITERAL("Foo"), KEY_WRITE));
  foo_key += STRING16_LITERAL("\\Bar");
  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_CURRENT_USER, foo_key.c_str(), KEY_WRITE));
  foo_key += STRING16_LITERAL("\\Foo");
  ASSERT_EQ(ERROR_SUCCESS, key.CreateKey(STRING16_LITERAL("Foo"), KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS, key.WriteValue(STRING16_LITERAL("TestValue"),
                                          STRING16_LITERAL("TestData")));
  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_CURRENT_USER, foo_key.c_str(), KEY_READ));

  ASSERT_EQ(ERROR_SUCCESS, key.Open(HKEY_CURRENT_USER, kRootKey, KEY_WRITE));
  ASSERT_NE(ERROR_SUCCESS, key.DeleteKey(STRING16_LITERAL("Bar")));
  ASSERT_NE(ERROR_SUCCESS, key.DeleteEmptyKey(STRING16_LITERAL("Foo")));
  ASSERT_NE(ERROR_SUCCESS,
            key.DeleteEmptyKey(STRING16_LITERAL("Foo\\Bar\\Foo")));
  ASSERT_NE(ERROR_SUCCESS, key.DeleteEmptyKey(STRING16_LITERAL("Foo\\Bar")));
  ASSERT_EQ(ERROR_SUCCESS, key.DeleteEmptyKey(STRING16_LITERAL("Foo\\Foo")));

  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_CURRENT_USER, foo_key.c_str(), KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS, key.CreateKey(STRING16_LITERAL("Bar"), KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS, key.CreateKey(STRING16_LITERAL("Foo"), KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_CURRENT_USER, foo_key.c_str(), KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS, key.DeleteKey(STRING16_LITERAL("")));
  ASSERT_NE(ERROR_SUCCESS,
            key.Open(HKEY_CURRENT_USER, foo_key.c_str(), KEY_READ));

  ASSERT_EQ(ERROR_SUCCESS, key.Open(HKEY_CURRENT_USER, kRootKey, KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS, key.DeleteKey(STRING16_LITERAL("Foo")));
  ASSERT_NE(ERROR_SUCCESS, key.DeleteKey(STRING16_LITERAL("Foo")));
  ASSERT_NE(ERROR_SUCCESS,
            key.Open(HKEY_CURRENT_USER, foo_key.c_str(), KEY_READ));
}

// This test requires running as an Administrator as it tests redirected
// registry writes to HKLM\Software
// http://msdn.microsoft.com/en-us/library/windows/desktop/aa384253.aspx
// TODO(wfh): flaky test on Vista.  See http://crbug.com/377917
TEST_F(RegistryTest, DISABLED_Wow64RedirectedFromNative) {
  if (!IsRedirectorPresent())
    return;

  RegKey key;

  // Test redirected key access from non-redirected.
  ASSERT_EQ(ERROR_SUCCESS,
            key.Create(HKEY_LOCAL_MACHINE,
                       foo_software_key_.c_str(),
                       KEY_WRITE | kRedirectedViewMask));
  ASSERT_NE(ERROR_SUCCESS,
            key.Open(HKEY_LOCAL_MACHINE, foo_software_key_.c_str(), KEY_READ));
  ASSERT_NE(ERROR_SUCCESS,
            key.Open(HKEY_LOCAL_MACHINE,
                     foo_software_key_.c_str(),
                     KEY_READ | kNativeViewMask));

  // Open the non-redirected view of the parent and try to delete the test key.
  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_LOCAL_MACHINE, STRING16_LITERAL("Software"),
                     KEY_SET_VALUE));
  ASSERT_NE(ERROR_SUCCESS, key.DeleteKey(kRootKey));
  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_LOCAL_MACHINE, STRING16_LITERAL("Software"),
                     KEY_SET_VALUE | kNativeViewMask));
  ASSERT_NE(ERROR_SUCCESS, key.DeleteKey(kRootKey));

  // Open the redirected view and delete the key created above.
  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_LOCAL_MACHINE, STRING16_LITERAL("Software"),
                     KEY_SET_VALUE | kRedirectedViewMask));
  ASSERT_EQ(ERROR_SUCCESS, key.DeleteKey(kRootKey));
}

// Test for the issue found in http://crbug.com/384587 where OpenKey would call
// Close() and reset wow64_access_ flag to 0 and cause a NOTREACHED to hit on a
// subsequent OpenKey call.
TEST_F(RegistryTest, SameWowFlags) {
  RegKey key;

  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_LOCAL_MACHINE, STRING16_LITERAL("Software"),
                     KEY_READ | KEY_WOW64_64KEY));
  ASSERT_EQ(ERROR_SUCCESS, key.OpenKey(STRING16_LITERAL("Microsoft"),
                                       KEY_READ | KEY_WOW64_64KEY));
  ASSERT_EQ(ERROR_SUCCESS, key.OpenKey(STRING16_LITERAL("Windows"),
                                       KEY_READ | KEY_WOW64_64KEY));
}

// TODO(wfh): flaky test on Vista.  See http://crbug.com/377917
TEST_F(RegistryTest, DISABLED_Wow64NativeFromRedirected) {
  if (!IsRedirectorPresent())
    return;
  RegKey key;

  // Test non-redirected key access from redirected.
  ASSERT_EQ(ERROR_SUCCESS,
            key.Create(HKEY_LOCAL_MACHINE,
                       foo_software_key_.c_str(),
                       KEY_WRITE | kNativeViewMask));
  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_LOCAL_MACHINE, foo_software_key_.c_str(), KEY_READ));
  ASSERT_NE(ERROR_SUCCESS,
            key.Open(HKEY_LOCAL_MACHINE,
                     foo_software_key_.c_str(),
                     KEY_READ | kRedirectedViewMask));

  // Open the redirected view of the parent and try to delete the test key
  // from the non-redirected view.
  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_LOCAL_MACHINE, STRING16_LITERAL("Software"),
                     KEY_SET_VALUE | kRedirectedViewMask));
  ASSERT_NE(ERROR_SUCCESS, key.DeleteKey(kRootKey));

  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_LOCAL_MACHINE, STRING16_LITERAL("Software"),
                     KEY_SET_VALUE | kNativeViewMask));
  ASSERT_EQ(ERROR_SUCCESS, key.DeleteKey(kRootKey));
}

TEST_F(RegistryTest, OpenSubKey) {
  RegKey key;
  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_CURRENT_USER,
                     kRootKey,
                     KEY_READ | KEY_CREATE_SUB_KEY));

  ASSERT_NE(ERROR_SUCCESS, key.OpenKey(STRING16_LITERAL("foo"), KEY_READ));
  ASSERT_EQ(ERROR_SUCCESS, key.CreateKey(STRING16_LITERAL("foo"), KEY_READ));
  ASSERT_EQ(ERROR_SUCCESS, key.Open(HKEY_CURRENT_USER, kRootKey, KEY_READ));
  ASSERT_EQ(ERROR_SUCCESS, key.OpenKey(STRING16_LITERAL("foo"), KEY_READ));

  string16 foo_key(kRootKey);
  foo_key += STRING16_LITERAL("\\Foo");
  ASSERT_EQ(ERROR_SUCCESS,
            key.Open(HKEY_CURRENT_USER, foo_key.c_str(), KEY_READ));

  ASSERT_EQ(ERROR_SUCCESS, key.Open(HKEY_CURRENT_USER, kRootKey, KEY_WRITE));
  ASSERT_EQ(ERROR_SUCCESS, key.DeleteKey(STRING16_LITERAL("foo")));
}

class TestChangeDelegate {
 public:
   TestChangeDelegate() : called_(false) {}
   ~TestChangeDelegate() {}

   void OnKeyChanged() {
     RunLoop::QuitCurrentWhenIdleDeprecated();
     called_ = true;
   }

   bool WasCalled() {
     bool was_called = called_;
     called_ = false;
     return was_called;
   }

 private:
  bool called_;
};

TEST_F(RegistryTest, ChangeCallback) {
  RegKey key;
  TestChangeDelegate delegate;
  test::ScopedTaskEnvironment scoped_task_environment;

  string16 foo_key(kRootKey);
  foo_key += STRING16_LITERAL("\\Foo");
  ASSERT_EQ(ERROR_SUCCESS, key.Create(HKEY_CURRENT_USER, foo_key.c_str(),
                                      KEY_READ));

  ASSERT_TRUE(key.StartWatching(
      BindRepeating(&TestChangeDelegate::OnKeyChanged, Unretained(&delegate))));
  EXPECT_FALSE(delegate.WasCalled());

  // Make some change.
  RegKey key2;
  ASSERT_EQ(ERROR_SUCCESS, key2.Open(HKEY_CURRENT_USER, foo_key.c_str(),
                                      KEY_READ | KEY_SET_VALUE));
  ASSERT_TRUE(key2.Valid());
  EXPECT_EQ(ERROR_SUCCESS, key2.WriteValue(STRING16_LITERAL("name"),
                                           STRING16_LITERAL("data")));

  // Allow delivery of the notification.
  EXPECT_FALSE(delegate.WasCalled());
  RunLoop().Run();

  ASSERT_TRUE(delegate.WasCalled());
  EXPECT_FALSE(delegate.WasCalled());

  ASSERT_TRUE(key.StartWatching(
      BindRepeating(&TestChangeDelegate::OnKeyChanged, Unretained(&delegate))));

  // Change something else.
  EXPECT_EQ(ERROR_SUCCESS, key2.WriteValue(STRING16_LITERAL("name2"),
                                           STRING16_LITERAL("data2")));
  RunLoop().Run();
  ASSERT_TRUE(delegate.WasCalled());

  ASSERT_TRUE(key.StartWatching(
      BindRepeating(&TestChangeDelegate::OnKeyChanged, Unretained(&delegate))));
  RunLoop().RunUntilIdle();
  EXPECT_FALSE(delegate.WasCalled());
}

}  // namespace

}  // namespace win
}  // namespace base
