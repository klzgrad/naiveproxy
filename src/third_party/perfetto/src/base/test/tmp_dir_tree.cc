/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/base/test/tmp_dir_tree.h"

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"

namespace perfetto {
namespace base {

TmpDirTree::TmpDirTree() : tmp_dir_(base::TempDir::Create()) {}

TmpDirTree::~TmpDirTree() {
  for (; !files_to_remove_.empty(); files_to_remove_.pop()) {
    PERFETTO_CHECK(remove(AbsolutePath(files_to_remove_.top()).c_str()) == 0);
  }
  for (; !dirs_to_remove_.empty(); dirs_to_remove_.pop()) {
    base::Rmdir(AbsolutePath(dirs_to_remove_.top()));
  }
}

std::string TmpDirTree::AbsolutePath(const std::string& relative_path) const {
  return path() + "/" + relative_path;
}

void TmpDirTree::AddDir(const std::string& relative_path) {
  dirs_to_remove_.push(relative_path);
  PERFETTO_CHECK(base::Mkdir(AbsolutePath(relative_path)));
}

void TmpDirTree::AddFile(const std::string& relative_path,
                         const std::string& content) {
  TrackFile(relative_path);
  base::ScopedFile fd(base::OpenFile(AbsolutePath(relative_path),
                                     O_WRONLY | O_CREAT | O_TRUNC, 0600));
  PERFETTO_CHECK(base::WriteAll(fd.get(), content.c_str(), content.size()) ==
                 static_cast<ssize_t>(content.size()));
}

void TmpDirTree::TrackFile(const std::string& relative_path) {
  files_to_remove_.push(relative_path);
}

}  // namespace base
}  // namespace perfetto
