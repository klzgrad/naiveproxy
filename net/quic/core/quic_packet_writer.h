// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_PACKET_WRITER_H_
#define NET_QUIC_CORE_QUIC_PACKET_WRITER_H_

#include <cstddef>

#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_socket_address.h"

namespace net {

struct WriteResult;

class QUIC_EXPORT_PRIVATE PerPacketOptions {
 public:
  PerPacketOptions() = default;
  virtual ~PerPacketOptions() {}

  // Returns a heap-allocated copy of |this|.
  virtual PerPacketOptions* Clone() const = 0;

 private:
  PerPacketOptions(PerPacketOptions&& other) = delete;
  PerPacketOptions& operator=(PerPacketOptions&& other) = delete;

  DISALLOW_COPY_AND_ASSIGN(PerPacketOptions);
};

// An interface between writers and the entity managing the
// socket (in our case the QuicDispatcher).  This allows the Dispatcher to
// control writes, and manage any writers who end up write blocked.
class QUIC_EXPORT_PRIVATE QuicPacketWriter {
 public:
  virtual ~QuicPacketWriter() {}

  // Sends the packet out to the peer, with some optional per-packet options.
  // If the write succeeded, the result's status is WRITE_STATUS_OK and
  // bytes_written is populated. If the write failed, the result's status is
  // WRITE_STATUS_BLOCKED or WRITE_STATUS_ERROR and error_code is populated.
  // Options must be either null, or created for the particular QuicPacketWriter
  // implementation. Options may be ignored, depending on the implementation.
  virtual WriteResult WritePacket(const char* buffer,
                                  size_t buf_len,
                                  const QuicIpAddress& self_address,
                                  const QuicSocketAddress& peer_address,
                                  PerPacketOptions* options) = 0;

  // Returns true if the writer buffers and subsequently rewrites data
  // when an attempt to write results in the underlying socket becoming
  // write blocked.
  virtual bool IsWriteBlockedDataBuffered() const = 0;

  // Returns true if the network socket is not writable.
  virtual bool IsWriteBlocked() const = 0;

  // Records that the socket has become writable, for example when an EPOLLOUT
  // is received or an asynchronous write completes.
  virtual void SetWritable() = 0;

  // Returns the maximum size of the packet which can be written using this
  // writer for the supplied peer address.  This size may actually exceed the
  // size of a valid QUIC packet.
  virtual QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& peer_address) const = 0;
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_PACKET_WRITER_H_
