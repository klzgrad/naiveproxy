// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_URL_UTILS_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_URL_UTILS_IMPL_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Produces concrete URLs in |target| from templated ones in |uri_template|.
// Parameters are URL-encoded. Collects the names of any expanded variables in
// |vars_found|. Supports level 1 templates as specified in RFC 6570. Returns
// true if the template was parseable, false if it was malformed.
QUICHE_EXPORT bool ExpandURITemplateImpl(
    const std::string& uri_template,
    const absl::flat_hash_map<std::string, std::string>& parameters,
    std::string* target,
    absl::flat_hash_set<std::string>* vars_found = nullptr);

// Decodes a URL-encoded string and converts it to ASCII. If the decoded input
// contains non-ASCII characters, decoding fails and absl::nullopt is returned.
QUICHE_EXPORT absl::optional<std::string> AsciiUrlDecodeImpl(
    absl::string_view input);

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_URL_UTILS_IMPL_H_
