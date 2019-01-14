// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_version_info_win.h"

#include <windows.h>

#include <stddef.h>

#include <memory>

#include "base/file_version_info.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/path_service.h"
#include "base/scoped_native_library.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::FilePath;

namespace {

FilePath GetTestDataPath() {
  FilePath path;
  base::PathService::Get(base::DIR_SOURCE_ROOT, &path);
  path = path.AppendASCII("base");
  path = path.AppendASCII("test");
  path = path.AppendASCII("data");
  path = path.AppendASCII("file_version_info_unittest");
  return path;
}

class FileVersionInfoFactory {
 public:
  explicit FileVersionInfoFactory(const FilePath& path) : path_(path) {}

  std::unique_ptr<FileVersionInfo> Create() const {
    return base::WrapUnique(FileVersionInfo::CreateFileVersionInfo(path_));
  }

 private:
  const FilePath path_;

  DISALLOW_COPY_AND_ASSIGN(FileVersionInfoFactory);
};

class FileVersionInfoForModuleFactory {
 public:
  explicit FileVersionInfoForModuleFactory(const FilePath& path)
      // Load the library with LOAD_LIBRARY_AS_IMAGE_RESOURCE since it shouldn't
      // be executed.
      : library_(::LoadLibraryEx(path.value().c_str(),
                                 nullptr,
                                 LOAD_LIBRARY_AS_IMAGE_RESOURCE)) {
    EXPECT_TRUE(library_.is_valid());
  }

  std::unique_ptr<FileVersionInfo> Create() const {
    return base::WrapUnique(
        FileVersionInfo::CreateFileVersionInfoForModule(library_.get()));
  }

 private:
  const base::ScopedNativeLibrary library_;

  DISALLOW_COPY_AND_ASSIGN(FileVersionInfoForModuleFactory);
};

template <typename T>
class FileVersionInfoTest : public testing::Test {};

using FileVersionInfoFactories =
    ::testing::Types<FileVersionInfoFactory, FileVersionInfoForModuleFactory>;

}  // namespace

TYPED_TEST_CASE(FileVersionInfoTest, FileVersionInfoFactories);

TYPED_TEST(FileVersionInfoTest, HardCodedProperties) {
  const wchar_t kDLLName[] = {L"FileVersionInfoTest1.dll"};

  const wchar_t* const kExpectedValues[15] = {
      // FileVersionInfoTest.dll
      L"Goooooogle",                                  // company_name
      L"Google",                                      // company_short_name
      L"This is the product name",                    // product_name
      L"This is the product short name",              // product_short_name
      L"The Internal Name",                           // internal_name
      L"4.3.2.1",                                     // product_version
      L"Private build property",                      // private_build
      L"Special build property",                      // special_build
      L"This is a particularly interesting comment",  // comments
      L"This is the original filename",               // original_filename
      L"This is my file description",                 // file_description
      L"1.2.3.4",                                     // file_version
      L"This is the legal copyright",                 // legal_copyright
      L"This is the legal trademarks",                // legal_trademarks
      L"This is the last change",                     // last_change
  };

  FilePath dll_path = GetTestDataPath();
  dll_path = dll_path.Append(kDLLName);

  TypeParam factory(dll_path);
  std::unique_ptr<FileVersionInfo> version_info(factory.Create());
  ASSERT_TRUE(version_info);

  int j = 0;
  EXPECT_EQ(kExpectedValues[j++], version_info->company_name());
  EXPECT_EQ(kExpectedValues[j++], version_info->company_short_name());
  EXPECT_EQ(kExpectedValues[j++], version_info->product_name());
  EXPECT_EQ(kExpectedValues[j++], version_info->product_short_name());
  EXPECT_EQ(kExpectedValues[j++], version_info->internal_name());
  EXPECT_EQ(kExpectedValues[j++], version_info->product_version());
  EXPECT_EQ(kExpectedValues[j++], version_info->private_build());
  EXPECT_EQ(kExpectedValues[j++], version_info->special_build());
  EXPECT_EQ(kExpectedValues[j++], version_info->comments());
  EXPECT_EQ(kExpectedValues[j++], version_info->original_filename());
  EXPECT_EQ(kExpectedValues[j++], version_info->file_description());
  EXPECT_EQ(kExpectedValues[j++], version_info->file_version());
  EXPECT_EQ(kExpectedValues[j++], version_info->legal_copyright());
  EXPECT_EQ(kExpectedValues[j++], version_info->legal_trademarks());
  EXPECT_EQ(kExpectedValues[j++], version_info->last_change());
}

TYPED_TEST(FileVersionInfoTest, IsOfficialBuild) {
  constexpr struct {
    const wchar_t* const dll_name;
    const bool is_official_build;
  } kTestItems[]{
      {L"FileVersionInfoTest1.dll", true}, {L"FileVersionInfoTest2.dll", false},
  };

  for (const auto& test_item : kTestItems) {
    const FilePath dll_path = GetTestDataPath().Append(test_item.dll_name);

    TypeParam factory(dll_path);
    std::unique_ptr<FileVersionInfo> version_info(factory.Create());
    ASSERT_TRUE(version_info);

    EXPECT_EQ(test_item.is_official_build, version_info->is_official_build());
  }
}

TYPED_TEST(FileVersionInfoTest, CustomProperties) {
  FilePath dll_path = GetTestDataPath();
  dll_path = dll_path.AppendASCII("FileVersionInfoTest1.dll");

  TypeParam factory(dll_path);
  std::unique_ptr<FileVersionInfo> version_info(factory.Create());
  ASSERT_TRUE(version_info);

  // Test few existing properties.
  std::wstring str;
  FileVersionInfoWin* version_info_win =
      static_cast<FileVersionInfoWin*>(version_info.get());
  EXPECT_TRUE(version_info_win->GetValue(L"Custom prop 1", &str));
  EXPECT_EQ(L"Un", str);
  EXPECT_EQ(L"Un", version_info_win->GetStringValue(L"Custom prop 1"));

  EXPECT_TRUE(version_info_win->GetValue(L"Custom prop 2", &str));
  EXPECT_EQ(L"Deux", str);
  EXPECT_EQ(L"Deux", version_info_win->GetStringValue(L"Custom prop 2"));

  EXPECT_TRUE(version_info_win->GetValue(L"Custom prop 3", &str));
  EXPECT_EQ(L"1600 Amphitheatre Parkway Mountain View, CA 94043", str);
  EXPECT_EQ(L"1600 Amphitheatre Parkway Mountain View, CA 94043",
            version_info_win->GetStringValue(L"Custom prop 3"));

  // Test an non-existing property.
  EXPECT_FALSE(version_info_win->GetValue(L"Unknown property", &str));
  EXPECT_EQ(L"", version_info_win->GetStringValue(L"Unknown property"));
}
