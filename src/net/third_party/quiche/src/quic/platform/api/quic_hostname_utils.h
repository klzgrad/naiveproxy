// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_HOSTNAME_UTILS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_HOSTNAME_UTILS_H_

#include <string>

#include "absl/strings/string_view.h"
#include "quic/platform/api/quic_export.h"
#include "net/quic/platform/impl/quic_hostname_utils_impl.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QuicHostnameUtils {
 public:
  QuicHostnameUtils() = delete;

  // Returns true if the sni is valid, false otherwise.
  //  (1) disallow IP addresses;
  //  (2) check that the hostname contains valid characters only; and
  //  (3) contains at least one dot.
  static bool IsValidSNI(absl::string_view sni);

  // Canonicalizes the specified hostname.  This involves a wide variety of
  // transformations, including lowercasing, removing trailing dots and IDNA
  // conversion.
  static std::string NormalizeHostname(absl::string_view hostname);
};

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_HOSTNAME_UTILS_H_
