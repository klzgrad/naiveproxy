// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/content_uri_utils.h"

namespace base {
namespace internal {

bool ContentUriExists(const FilePath&) {
  return false;
}

std::optional<std::string> TranslateOpenFlagsToJavaMode(uint32_t) {
  return {};
}

int OpenContentUri(const FilePath&, uint32_t) {
  return -1;
}

bool ContentUriGetFileInfo(const FilePath&, File::Info*) {
  return false;
}

std::vector<FileEnumerator::FileInfo> ListContentUriDirectory(const FilePath&) {
  return {};
}

bool DeleteContentUri(const FilePath& content_uri) {
  return false;
}

}  // namespace internal

std::string GetContentUriMimeType(const FilePath& content_uri) {
  return {};
}

bool MaybeGetFileDisplayName(const FilePath& content_uri,
                             std::u16string* file_display_name) {
  return false;
}

FilePath ContentUriBuildDocumentUriUsingTree(const FilePath&,
                                             const std::string&) {
  return {};
}
}  // namespace base
