// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/content_uri_utils.h"

namespace base {

bool ContentUriExists(const FilePath& content_uri) {
  return false;
}

File OpenContentUriForRead(const FilePath& content_uri) {
  return {};
}

std::string GetContentUriMimeType(const FilePath& content_uri) {
  return {};
}

bool MaybeGetFileDisplayName(const FilePath& content_uri,
                             std::u16string* file_display_name) {
  return false;
}

bool DeleteContentUri(const FilePath& content_uri) {
  return false;
}

FilePath GetContentUriFromFilePath(const FilePath& file_path) {
  return {};
}

}  // namespace base
