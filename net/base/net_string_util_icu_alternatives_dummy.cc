// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/net_string_util.h"

#include "base/logging.h"

namespace net {

const char* const kCharsetLatin1 = "";

bool ConvertToUtf8(const std::string& text, const char* charset,
                   std::string* output) {
  NOTIMPLEMENTED();
  return false;
}

bool ConvertToUtf8AndNormalize(const std::string& text, const char* charset,
                               std::string* output) {
  NOTIMPLEMENTED();
  return false;
}

bool ConvertToUTF16(const std::string& text, const char* charset,
                    base::string16* output) {
  NOTIMPLEMENTED();
  return false;
}

bool ConvertToUTF16WithSubstitutions(const std::string& text,
                                     const char* charset,
                                     base::string16* output) {
  NOTIMPLEMENTED();
  return false;
}

bool ToUpper(const base::string16& str, base::string16* output) {
  NOTIMPLEMENTED();
  return false;
}

}  // namespace net
