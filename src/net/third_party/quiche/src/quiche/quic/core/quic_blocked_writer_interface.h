// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is an interface for all objects that want to be notified that
// the underlying UDP socket is available for writing (not write blocked
// anymore).

#ifndef QUICHE_QUIC_CORE_QUIC_BLOCKED_WRITER_INTERFACE_H_
#define QUICHE_QUIC_CORE_QUIC_BLOCKED_WRITER_INTERFACE_H_

#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QUICHE_EXPORT QuicBlockedWriterInterface {
 public:
  virtual ~QuicBlockedWriterInterface() {}

  // Called by the PacketWriter when the underlying socket becomes writable
  // so that the BlockedWriter can go ahead and try writing.
  virtual void OnBlockedWriterCanWrite() = 0;

  virtual bool IsWriterBlocked() const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_BLOCKED_WRITER_INTERFACE_H_
