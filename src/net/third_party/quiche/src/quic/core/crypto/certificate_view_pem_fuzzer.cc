// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>
#include <string>

#include "quic/core/crypto/certificate_view.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string input(reinterpret_cast<const char*>(data), size);
  std::stringstream stream(input);

  quic::CertificateView::LoadPemFromStream(&stream);
  stream.seekg(0);
  quic::CertificatePrivateKey::LoadPemFromStream(&stream);
  return 0;
}
