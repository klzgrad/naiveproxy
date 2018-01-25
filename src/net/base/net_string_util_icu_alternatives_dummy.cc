// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string_view>

#include "net/base/net_string_util.h"

namespace net {

const char* const kCharsetLatin1 = "";

bool ConvertToUtf8(std::string_view text, const char* charset,
                   std::string* output) {
  return false;
}

bool ConvertToUtf8AndNormalize(std::string_view text, const char* charset,
                               std::string* output) {
  return false;
}

bool ConvertToUTF16(std::string_view text, const char* charset,
                    std::u16string* output) {
  return false;
}

bool ConvertToUTF16WithSubstitutions(std::string_view text,
                                     const char* charset,
                                     std::u16string* output) {
  return false;
}

bool ToUpperUsingLocale(std::u16string_view str, std::u16string* output) {
  return false;
}
}  // namespace net
