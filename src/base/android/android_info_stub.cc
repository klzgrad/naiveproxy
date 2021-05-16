// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/android_info.h"

#include <string>

static constexpr std::string empty;

namespace base::android::android_info {
void Set(const IAndroidInfo& info) {
}

const std::string& device() {
  return empty;
}

const std::string& manufacturer() {
  return empty;
}

const std::string& model() {
  return empty;
}

const std::string& brand() {
  return empty;
}

const std::string& android_build_id() {
  return empty;
}

const std::string& build_type() {
  return empty;
}

const std::string& board() {
  return empty;
}

const std::string& android_build_fp() {
  return empty;
}

int sdk_int() {
  return 0;
}

bool is_debug_android() {
  return false;
}

const std::string& version_incremental() {
  return empty;
}

const std::string& hardware() {
  return empty;
}

const std::string& codename() {
  return empty;
}

const std::string& soc_manufacturer() {
  return empty;
}

const std::string& abi_name() {
  return empty;
}

const std::string& security_patch() {
  return empty;
}

}  // namespace base::android::android_info
