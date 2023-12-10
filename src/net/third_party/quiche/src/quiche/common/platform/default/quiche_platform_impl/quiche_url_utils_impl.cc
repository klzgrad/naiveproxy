// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche_platform_impl/quiche_url_utils_impl.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "quiche_platform_impl/quiche_googleurl_impl.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace quiche {

bool ExpandURITemplateImpl(
    const std::string& uri_template,
    const absl::flat_hash_map<std::string, std::string>& parameters,
    std::string* target, absl::flat_hash_set<std::string>* vars_found) {
  absl::flat_hash_set<std::string> found;
  std::string result = uri_template;
  for (const auto& pair : parameters) {
    const std::string& name = pair.first;
    const std::string& value = pair.second;
    std::string name_input = absl::StrCat("{", name, "}");
    url::RawCanonOutputT<char> canon_value;
    url::EncodeURIComponent(value.c_str(), value.length(), &canon_value);
    std::string encoded_value(canon_value.data(), canon_value.length());
    int num_replaced =
        absl::StrReplaceAll({{name_input, encoded_value}}, &result);
    if (num_replaced > 0) {
      found.insert(name);
    }
  }
  // Remove any remaining variables that were not present in |parameters|.
  while (true) {
    size_t start = result.find('{');
    if (start == std::string::npos) {
      break;
    }
    size_t end = result.find('}');
    if (end == std::string::npos || end <= start) {
      return false;
    }
    result.erase(start, (end - start) + 1);
  }
  if (vars_found != nullptr) {
    *vars_found = found;
  }
  *target = result;
  return true;
}

absl::optional<std::string> AsciiUrlDecodeImpl(absl::string_view input) {
  std::string input_encoded = std::string(input);
  url::RawCanonOutputW<1024> canon_output;
  url::DecodeURLEscapeSequences(input_encoded.c_str(), input_encoded.length(),
                                url::DecodeURLMode::kUTF8,
                                &canon_output);
  std::string output;
  output.reserve(canon_output.length());
  for (int i = 0; i < canon_output.length(); i++) {
    const uint16_t c = reinterpret_cast<uint16_t*>(canon_output.data())[i];
    if (c > std::numeric_limits<signed char>::max()) {
      return absl::nullopt;
    }
    output += static_cast<char>(c);
  }
  return output;
}

}  // namespace quiche
