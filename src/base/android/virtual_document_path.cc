// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/virtual_document_path.h"

#include <optional>

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"

namespace base::files_internal {

VirtualDocumentPath::VirtualDocumentPath(
    const base::android::JavaRef<jobject>& obj) {
  obj_.Reset(obj);
}

VirtualDocumentPath::VirtualDocumentPath(const VirtualDocumentPath& path) =
    default;
VirtualDocumentPath& VirtualDocumentPath::operator=(
    const VirtualDocumentPath& path) = default;

VirtualDocumentPath::~VirtualDocumentPath() = default;

std::optional<VirtualDocumentPath> VirtualDocumentPath::Parse(
    const std::string& path) {
  return std::nullopt;
}

std::optional<std::string> VirtualDocumentPath::ResolveToContentUri() const {
  return std::nullopt;
}

std::string VirtualDocumentPath::ToString() const {
  return {};
}

bool VirtualDocumentPath::Mkdir(mode_t mode) const {
  return false;
}

bool VirtualDocumentPath::WriteFile(span<const uint8_t> data) const {
  return false;
}

std::optional<std::pair<std::string, bool>> VirtualDocumentPath::CreateOrOpen()
    const {
  return std::nullopt;
}

}  // namespace base::files_internal
