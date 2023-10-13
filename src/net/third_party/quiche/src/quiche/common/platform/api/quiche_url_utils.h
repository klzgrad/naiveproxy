// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_URL_UTILS_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_URL_UTILS_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "quiche_platform_impl/quiche_url_utils_impl.h"

namespace quiche {

// Produces concrete URLs in |target| from templated ones in |uri_template|.
// Parameters are URL-encoded. Collects the names of any expanded variables in
// |vars_found|. Returns true if the template was parseable, false if it was
// malformed.
inline bool ExpandURITemplate(
    const std::string& uri_template,
    const absl::flat_hash_map<std::string, std::string>& parameters,
    std::string* target,
    absl::flat_hash_set<std::string>* vars_found = nullptr) {
  return ExpandURITemplateImpl(uri_template, parameters, target, vars_found);
}

// Decodes a URL-encoded string and converts it to ASCII. If the decoded input
// contains non-ASCII characters, decoding fails and absl::nullopt is returned.
inline absl::optional<std::string> AsciiUrlDecode(absl::string_view input) {
  return AsciiUrlDecodeImpl(input);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_URL_UTILS_H_
