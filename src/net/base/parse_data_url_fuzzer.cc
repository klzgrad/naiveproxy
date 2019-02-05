// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "net/base/data_url.h"
#include "url/gurl.h"

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string input(data, data + size);
  std::string mime_type;
  std::string charset;
  std::string urldata;
  net::DataURL::Parse(GURL(input), &mime_type, &charset, &urldata);
  return 0;
}
