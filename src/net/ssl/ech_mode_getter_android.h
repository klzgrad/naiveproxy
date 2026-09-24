// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_ECH_MODE_GETTER_ANDROID_H_
#define NET_SSL_ECH_MODE_GETTER_ANDROID_H_

#include <string_view>

#include "net/base/ech_mode.h"
#include "net/base/net_export.h"
#include "net/ssl/ech_mode_getter.h"

namespace net {

// Android-specific implementation of EchModeGetter.
// This class uses Android's NetworkSecurityPolicy to determine the ECHMode
// for a given hostname.
class NET_EXPORT EchModeGetterAndroid : public EchModeGetter {
 public:
  EchModeGetterAndroid();
  ~EchModeGetterAndroid() override;

  EchMode GetEchMode(std::string_view hostname) const override;
};

}  // namespace net

#endif  // NET_SSL_ECH_MODE_GETTER_ANDROID_H_
