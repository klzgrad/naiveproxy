// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ech_mode_getter_android.h"

#include <string_view>

#include "net/android/network_library.h"
#include "net/base/ech_mode.h"

namespace net {

EchModeGetterAndroid::EchModeGetterAndroid() = default;
EchModeGetterAndroid::~EchModeGetterAndroid() = default;

EchMode EchModeGetterAndroid::GetEchMode(std::string_view hostname) const {
  return net::android::GetEchMode(hostname);
}

}  // namespace net
