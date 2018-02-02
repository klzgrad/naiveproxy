// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is an interface for all objects that want to be notified that
// the underlying UDP socket is available for writing (not write blocked
// anymore).

#ifndef NET_QUIC_CORE_QUIC_BLOCKED_WRITER_INTERFACE_H_
#define NET_QUIC_CORE_QUIC_BLOCKED_WRITER_INTERFACE_H_

#include "net/quic/platform/api/quic_export.h"

namespace net {

class QUIC_EXPORT_PRIVATE QuicBlockedWriterInterface {
 public:
  virtual ~QuicBlockedWriterInterface() {}

  // Called by the PacketWriter when the underlying socket becomes writable
  // so that the BlockedWriter can go ahead and try writing.
  virtual void OnBlockedWriterCanWrite() = 0;
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_BLOCKED_WRITER_INTERFACE_H_
