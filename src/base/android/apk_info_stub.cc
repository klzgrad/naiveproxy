// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/apk_info.h"

#include <string>

static constexpr std::string empty;

namespace base::android::apk_info {

void Set(const IApkInfo& info) {
}

const std::string& host_package_name() {
  return empty;
}

const std::string& host_version_code() {
  return empty;
}

const std::string& host_package_label() {
  return empty;
}

const std::string& package_version_code() {
  return empty;
}

const std::string& package_version_name() {
  return empty;
}

const std::string& package_name() {
  return empty;
}

const std::string& resources_version() {
  return empty;
}

const std::string& installer_package_name() {
  return empty;
}

bool is_debug_app() {
  return false;
}

int target_sdk_version() {
  return 0;
}

std::string host_signing_cert_sha256() {
  return empty;
}
}  // namespace base::android::apk_info
