// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "base/time/time.h"
#include "net/base/hash_value.h"
#include "net/http/http_security_headers.h"
#include "net/ssl/ssl_info.h"
#include "url/gurl.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string input(data, data + size);
  base::TimeDelta max_age;
  bool include_subdomains;
  net::HashValueVector spki_hashes;
  GURL report_uri;

  net::HashValue hash;
  hash.FromString("sha256/1111111111111111111111111111111111111111111=");

  net::SSLInfo ssl_info;
  ssl_info.public_key_hashes.push_back(hash);

  net::ParseHPKPHeader(input, ssl_info.public_key_hashes, &max_age,
                       &include_subdomains, &spki_hashes, &report_uri);
  return 0;
}
