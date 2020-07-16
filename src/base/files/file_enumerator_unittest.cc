// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_enumerator.h"

#include <utility>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "build/build_config.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::ElementsAre;
using testing::IsEmpty;
using testing::UnorderedElementsAre;

namespace base {
namespace {

const FilePath::StringType kEmptyPattern;

const std::vector<FileEnumerator::FolderSearchPolicy> kFolderSearchPolicies{
    FileEnumerator::FolderSearchPolicy::MATCH_ONLY,
    FileEnumerator::FolderSearchPolicy::ALL};

circular_deque<FilePath> RunEnumerator(
    const FilePath& root_path,
    bool recursive,
    int file_type,
    const FilePath::StringType& pattern,
    FileEnumerator::FolderSearchPolicy folder_search_policy) {
  circular_deque<FilePath> rv;
  FileEnumerator enumerator(root_path, recursive, file_type, pattern,
                            folder_search_policy,
                            FileEnumerator::ErrorPolicy::IGNORE_ERRORS);
  for (auto file = enumerator.Next(); !file.empty(); file = enumerator.Next())
    rv.emplace_back(std::move(file));
  return rv;
}

bool CreateDummyFile(const FilePath& path) {
  return WriteFile(path, "42", sizeof("42")) == sizeof("42");
}

}  // namespace

TEST(FileEnumerator, NotExistingPath) {
  const FilePath path = FilePath::FromUTF8Unsafe("some_not_existing_path");
  ASSERT_FALSE(PathExists(path));

  for (auto policy : kFolderSearchPolicies) {
    const auto files = RunEnumerator(
        path, true, FileEnumerator::FILES | FileEnumerator::DIRECTORIES,
        FILE_PATH_LITERAL(""), policy);
    EXPECT_THAT(files, IsEmpty());
  }
}

TEST(FileEnumerator, EmptyFolder) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  for (auto policy : kFolderSearchPolicies) {
    const auto files =
        RunEnumerator(temp_dir.GetPath(), true,
                      FileEnumerator::FILES | FileEnumerator::DIRECTORIES,
                      kEmptyPattern, policy);
    EXPECT_THAT(files, IsEmpty());
  }
}

TEST(FileEnumerator, SingleFileInFolderForFileSearch) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath& path = temp_dir.GetPath();
  const FilePath file = path.AppendASCII("test.txt");
  ASSERT_TRUE(CreateDummyFile(file));

  for (auto policy : kFolderSearchPolicies) {
    const auto files = RunEnumerator(
        temp_dir.GetPath(), true, FileEnumerator::FILES, kEmptyPattern, policy);
    EXPECT_THAT(files, ElementsAre(file));
  }
}

TEST(FileEnumerator, SingleFileInFolderForDirSearch) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath& path = temp_dir.GetPath();
  ASSERT_TRUE(CreateDummyFile(path.AppendASCII("test.txt")));

  for (auto policy : kFolderSearchPolicies) {
    const auto files = RunEnumerator(path, true, FileEnumerator::DIRECTORIES,
                                     kEmptyPattern, policy);
    EXPECT_THAT(files, IsEmpty());
  }
}

TEST(FileEnumerator, SingleFileInFolderWithFiltering) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath& path = temp_dir.GetPath();
  const FilePath file = path.AppendASCII("test.txt");
  ASSERT_TRUE(CreateDummyFile(file));

  for (auto policy : kFolderSearchPolicies) {
    auto files = RunEnumerator(path, true, FileEnumerator::FILES,
                               FILE_PATH_LITERAL("*.txt"), policy);
    EXPECT_THAT(files, ElementsAre(file));

    files = RunEnumerator(path, true, FileEnumerator::FILES,
                          FILE_PATH_LITERAL("*.pdf"), policy);
    EXPECT_THAT(files, IsEmpty());
  }
}

TEST(FileEnumerator, TwoFilesInFolder) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath& path = temp_dir.GetPath();
  const FilePath foo_txt = path.AppendASCII("foo.txt");
  const FilePath bar_txt = path.AppendASCII("bar.txt");
  ASSERT_TRUE(CreateDummyFile(foo_txt));
  ASSERT_TRUE(CreateDummyFile(bar_txt));

  for (auto policy : kFolderSearchPolicies) {
    auto files = RunEnumerator(path, true, FileEnumerator::FILES,
                               FILE_PATH_LITERAL("*.txt"), policy);
    EXPECT_THAT(files, UnorderedElementsAre(foo_txt, bar_txt));

    files = RunEnumerator(path, true, FileEnumerator::FILES,
                          FILE_PATH_LITERAL("foo*"), policy);
    EXPECT_THAT(files, ElementsAre(foo_txt));

    files = RunEnumerator(path, true, FileEnumerator::FILES,
                          FILE_PATH_LITERAL("*.pdf"), policy);
    EXPECT_THAT(files, IsEmpty());

    files =
        RunEnumerator(path, true, FileEnumerator::FILES, kEmptyPattern, policy);
    EXPECT_THAT(files, UnorderedElementsAre(foo_txt, bar_txt));
  }
}

TEST(FileEnumerator, SingleFolderInFolderForFileSearch) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath& path = temp_dir.GetPath();

  ScopedTempDir temp_subdir;
  ASSERT_TRUE(temp_subdir.CreateUniqueTempDirUnderPath(path));

  for (auto policy : kFolderSearchPolicies) {
    const auto files =
        RunEnumerator(path, true, FileEnumerator::FILES, kEmptyPattern, policy);
    EXPECT_THAT(files, IsEmpty());
  }
}

TEST(FileEnumerator, SingleFolderInFolderForDirSearch) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath& path = temp_dir.GetPath();

  ScopedTempDir temp_subdir;
  ASSERT_TRUE(temp_subdir.CreateUniqueTempDirUnderPath(path));

  for (auto policy : kFolderSearchPolicies) {
    const auto files = RunEnumerator(path, true, FileEnumerator::DIRECTORIES,
                                     kEmptyPattern, policy);
    EXPECT_THAT(files, ElementsAre(temp_subdir.GetPath()));
  }
}

TEST(FileEnumerator, TwoFoldersInFolder) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath& path = temp_dir.GetPath();

  const FilePath subdir_foo = path.AppendASCII("foo");
  const FilePath subdir_bar = path.AppendASCII("bar");
  ASSERT_TRUE(CreateDirectory(subdir_foo));
  ASSERT_TRUE(CreateDirectory(subdir_bar));

  for (auto policy : kFolderSearchPolicies) {
    auto files = RunEnumerator(path, true, FileEnumerator::DIRECTORIES,
                               kEmptyPattern, policy);
    EXPECT_THAT(files, UnorderedElementsAre(subdir_foo, subdir_bar));

    files = RunEnumerator(path, true, FileEnumerator::DIRECTORIES,
                          FILE_PATH_LITERAL("foo"), policy);
    EXPECT_THAT(files, ElementsAre(subdir_foo));
  }
}

TEST(FileEnumerator, FolderAndFileInFolder) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath& path = temp_dir.GetPath();

  ScopedTempDir temp_subdir;
  ASSERT_TRUE(temp_subdir.CreateUniqueTempDirUnderPath(path));
  const FilePath file = path.AppendASCII("test.txt");
  ASSERT_TRUE(CreateDummyFile(file));

  for (auto policy : kFolderSearchPolicies) {
    auto files =
        RunEnumerator(path, true, FileEnumerator::FILES, kEmptyPattern, policy);
    EXPECT_THAT(files, ElementsAre(file));

    files = RunEnumerator(path, true, FileEnumerator::DIRECTORIES,
                          kEmptyPattern, policy);
    EXPECT_THAT(files, ElementsAre(temp_subdir.GetPath()));

    files = RunEnumerator(path, true,
                          FileEnumerator::FILES | FileEnumerator::DIRECTORIES,
                          kEmptyPattern, policy);
    EXPECT_THAT(files, UnorderedElementsAre(file, temp_subdir.GetPath()));
  }
}

TEST(FileEnumerator, FilesInParentFolderAlwaysFirst) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath& path = temp_dir.GetPath();

  ScopedTempDir temp_subdir;
  ASSERT_TRUE(temp_subdir.CreateUniqueTempDirUnderPath(path));
  const FilePath foo_txt = path.AppendASCII("foo.txt");
  const FilePath bar_txt = temp_subdir.GetPath().AppendASCII("bar.txt");
  ASSERT_TRUE(CreateDummyFile(foo_txt));
  ASSERT_TRUE(CreateDummyFile(bar_txt));

  for (auto policy : kFolderSearchPolicies) {
    const auto files =
        RunEnumerator(path, true, FileEnumerator::FILES, kEmptyPattern, policy);
    EXPECT_THAT(files, ElementsAre(foo_txt, bar_txt));
  }
}

TEST(FileEnumerator, FileInSubfolder) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath subdir = temp_dir.GetPath().AppendASCII("subdir");
  ASSERT_TRUE(CreateDirectory(subdir));

  const FilePath file = subdir.AppendASCII("test.txt");
  ASSERT_TRUE(CreateDummyFile(file));

  for (auto policy : kFolderSearchPolicies) {
    auto files = RunEnumerator(temp_dir.GetPath(), true, FileEnumerator::FILES,
                               kEmptyPattern, policy);
    EXPECT_THAT(files, ElementsAre(file));

    files = RunEnumerator(temp_dir.GetPath(), false, FileEnumerator::FILES,
                          kEmptyPattern, policy);
    EXPECT_THAT(files, IsEmpty());
  }
}

TEST(FileEnumerator, FilesInSubfoldersWithFiltering) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath test_txt = temp_dir.GetPath().AppendASCII("test.txt");
  const FilePath subdir_foo = temp_dir.GetPath().AppendASCII("foo_subdir");
  const FilePath subdir_bar = temp_dir.GetPath().AppendASCII("bar_subdir");
  const FilePath foo_test = subdir_foo.AppendASCII("test.txt");
  const FilePath foo_foo = subdir_foo.AppendASCII("foo.txt");
  const FilePath foo_bar = subdir_foo.AppendASCII("bar.txt");
  const FilePath bar_test = subdir_bar.AppendASCII("test.txt");
  const FilePath bar_foo = subdir_bar.AppendASCII("foo.txt");
  const FilePath bar_bar = subdir_bar.AppendASCII("bar.txt");
  ASSERT_TRUE(CreateDummyFile(test_txt));
  ASSERT_TRUE(CreateDirectory(subdir_foo));
  ASSERT_TRUE(CreateDirectory(subdir_bar));
  ASSERT_TRUE(CreateDummyFile(foo_test));
  ASSERT_TRUE(CreateDummyFile(foo_foo));
  ASSERT_TRUE(CreateDummyFile(foo_bar));
  ASSERT_TRUE(CreateDummyFile(bar_test));
  ASSERT_TRUE(CreateDummyFile(bar_foo));
  ASSERT_TRUE(CreateDummyFile(bar_bar));

  auto files =
      RunEnumerator(temp_dir.GetPath(), true,
                    FileEnumerator::FILES | FileEnumerator::DIRECTORIES,
                    FILE_PATH_LITERAL("foo*"),
                    FileEnumerator::FolderSearchPolicy::MATCH_ONLY);
  EXPECT_THAT(files,
              UnorderedElementsAre(subdir_foo, foo_test, foo_foo, foo_bar));

  files = RunEnumerator(temp_dir.GetPath(), true,
                        FileEnumerator::FILES | FileEnumerator::DIRECTORIES,
                        FILE_PATH_LITERAL("foo*"),
                        FileEnumerator::FolderSearchPolicy::ALL);
  EXPECT_THAT(files, UnorderedElementsAre(subdir_foo, foo_foo, bar_foo));
}

TEST(FileEnumerator, InvalidDirectory) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath test_file = temp_dir.GetPath().AppendASCII("test_file");
  ASSERT_TRUE(CreateDummyFile(test_file));

  // Attempt to enumerate entries at a regular file path.
  FileEnumerator enumerator(test_file, /*recursive=*/true,
                            FileEnumerator::FILES, kEmptyPattern,
                            FileEnumerator::FolderSearchPolicy::ALL,
                            FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  FilePath path = enumerator.Next();
  EXPECT_TRUE(path.empty());

  // Slightly different outcomes between Windows and POSIX.
#if defined(OS_WIN)
  EXPECT_EQ(File::Error::FILE_ERROR_FAILED, enumerator.GetError());
#else
  EXPECT_EQ(File::Error::FILE_ERROR_NOT_A_DIRECTORY, enumerator.GetError());
#endif
}

#if defined(OS_POSIX)
TEST(FileEnumerator, SymLinkLoops) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const FilePath subdir = temp_dir.GetPath().AppendASCII("subdir");
  ASSERT_TRUE(CreateDirectory(subdir));

  const FilePath file = subdir.AppendASCII("test.txt");
  ASSERT_TRUE(CreateDummyFile(file));

  const FilePath link = subdir.AppendASCII("link");
  ASSERT_TRUE(CreateSymbolicLink(temp_dir.GetPath(), link));

  auto files = RunEnumerator(
      temp_dir.GetPath(), true,
      FileEnumerator::FILES | FileEnumerator::DIRECTORIES, kEmptyPattern,
      FileEnumerator::FolderSearchPolicy::MATCH_ONLY);

  EXPECT_THAT(files, UnorderedElementsAre(subdir, link, file));

  files = RunEnumerator(subdir, true,
                        FileEnumerator::FILES | FileEnumerator::DIRECTORIES |
                            FileEnumerator::SHOW_SYM_LINKS,
                        kEmptyPattern,
                        FileEnumerator::FolderSearchPolicy::MATCH_ONLY);

  EXPECT_THAT(files, UnorderedElementsAre(link, file));
}
#endif

}  // namespace base
