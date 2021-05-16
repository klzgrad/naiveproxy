// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/device_info.h"

#include <string>

static constexpr std::string empty;

namespace base::android::device_info {
void Set(const IDeviceInfo& info) {
}

const std::string& gms_version_code() {
  return empty;
}

void set_gms_version_code_for_test(const std::string& gms_version_code) {
}

bool is_tv() {
  return false;
}
bool is_automotive() {
  return false;
}
bool is_foldable() {
  return false;
}

bool is_desktop() {
  return false;
}

int32_t vulkan_deqp_level() {
  return 0;
}

bool is_xr() {
  return false;
}

bool is_tablet() {
  return false;
}

bool was_launched_on_large_display() {
  return false;
}

std::string device_name() {
  return empty;
}

void set_is_xr_for_testing() {
}

void reset_is_xr_for_testing() {
}
}  // namespace base::android::device_info
