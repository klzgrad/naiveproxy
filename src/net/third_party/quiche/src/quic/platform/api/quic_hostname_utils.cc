// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/platform/api/quic_hostname_utils.h"
#include "absl/strings/string_view.h"

namespace quic {

// static
bool QuicHostnameUtils::IsValidSNI(absl::string_view sni) {
  return QuicHostnameUtilsImpl::IsValidSNI(sni);
}

// static
std::string QuicHostnameUtils::NormalizeHostname(absl::string_view hostname) {
  return QuicHostnameUtilsImpl::NormalizeHostname(hostname);
}

}  // namespace quic
