// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/platform/internet_checksum.h"

#include <stdint.h>
#include <string.h>

namespace quic {

void InternetChecksum::Update(const char* data, size_t size) {
  const char* current;
  for (current = data; current + 1 < data + size; current += 2) {
    uint16_t v;
    memcpy(&v, current, sizeof(v));
    accumulator_ += v;
  }
  if (current < data + size) {
    accumulator_ += *reinterpret_cast<const unsigned char*>(current);
  }
}

void InternetChecksum::Update(const uint8_t* data, size_t size) {
  Update(reinterpret_cast<const char*>(data), size);
}

uint16_t InternetChecksum::Value() const {
  uint32_t total = accumulator_;
  while (total & 0xffff0000u) {
    total = (total >> 16u) + (total & 0xffffu);
  }
  return ~static_cast<uint16_t>(total);
}

}  // namespace quic
