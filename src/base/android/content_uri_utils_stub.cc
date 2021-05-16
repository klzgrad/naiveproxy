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

base::android::ScopedJavaLocalRef<jobject> OpenContentUri(const FilePath&, uint32_t) {
  return {};
}

int ContentUriGetFd(const base::android::JavaRef<jobject>&) {
  return -1;
}

void ContentUriClose(const base::android::JavaRef<jobject>&) {
}

bool ContentUriGetFileInfo(const FilePath&, FileEnumerator::FileInfo*) {
  return false;
}

std::vector<FileEnumerator::FileInfo> ListContentUriDirectory(const FilePath&) {
  return {};
}

bool DeleteContentUri(const FilePath& content_uri) {
  return false;
}

bool IsDocumentUri(const FilePath& content_uri) {
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

FilePath
ContentUriGetChildDocumentOrQuery(const FilePath&,
                                  const std::string&,
                                  const std::string&,
                                  bool,
                                  bool) {
  return {};
}

bool ContentUriIsCreateChildDocumentQuery(const FilePath&) {
  return false;
}

FilePath
ContentUriGetDocumentFromQuery(const FilePath&, bool) {
  return {};
}
}  // namespace base
