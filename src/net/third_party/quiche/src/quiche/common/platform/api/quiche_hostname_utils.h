// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_HOSTNAME_UTILS_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_HOSTNAME_UTILS_H_

#include <string>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

class QUICHE_EXPORT QuicheHostnameUtils {
 public:
  QuicheHostnameUtils() = delete;

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

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_HOSTNAME_UTILS_H_
