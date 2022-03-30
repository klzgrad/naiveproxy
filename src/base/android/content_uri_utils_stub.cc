// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/content_uri_utils.h"

namespace base {

std::optional<std::string> TranslateOpenFlagsToJavaMode(uint32_t) {
  return {};
}

File OpenContentUri(const FilePath&, uint32_t) {
  return {};
}

int64_t GetContentUriFileSize(const FilePath&) {
  return -1;
}

bool ContentUriExists(const FilePath&) {
  return false;
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

}  // namespace base
