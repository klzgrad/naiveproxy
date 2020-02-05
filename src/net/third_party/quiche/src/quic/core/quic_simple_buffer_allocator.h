// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_SIMPLE_BUFFER_ALLOCATOR_H_
#define QUICHE_QUIC_CORE_QUIC_SIMPLE_BUFFER_ALLOCATOR_H_

#include "net/third_party/quiche/src/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class QUIC_EXPORT_PRIVATE SimpleBufferAllocator : public QuicBufferAllocator {
 public:
  char* New(size_t size) override;
  char* New(size_t size, bool flag_enable) override;
  void Delete(char* buffer) override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_SIMPLE_BUFFER_ALLOCATOR_H_
