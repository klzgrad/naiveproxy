// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"

#include "base/android/jni_string.h"
#include "base/android/virtual_document_path.h"
#include "base/files/file_path.h"
#include "base/path_service.h"

namespace base {

bool GetShmemTempDir(bool executable, base::FilePath* path) {
  return PathService::Get(base::DIR_CACHE, path);
}

std::optional<FilePath> ResolveToContentUri(const base::FilePath& path) {
  if (path.IsContentUri()) {
    return path;
  }
  if (!path.IsVirtualDocumentPath()) {
    return std::nullopt;
  }

  std::optional<files_internal::VirtualDocumentPath> vp =
      files_internal::VirtualDocumentPath::Parse(path.value());
  if (!vp) {
    return std::nullopt;
  }

  std::optional<std::string> uri = vp->ResolveToContentUri();
  if (!uri) {
    return std::nullopt;
  }

  return FilePath(*uri);
}

std::optional<FilePath> ResolveToVirtualDocumentPath(const FilePath& path) {
  std::optional<files_internal::VirtualDocumentPath> vp =
      files_internal::VirtualDocumentPath::Parse(path.value());
  if (!vp) {
    return std::nullopt;
  }
  return FilePath(vp->ToString());
}

}  // namespace base
