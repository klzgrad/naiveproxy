// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_PACKET_WRITER_H_
#define QUICHE_QUIC_CORE_QUIC_PACKET_WRITER_H_

#include <cstddef>
#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"

namespace quic {

struct WriteResult;

struct QUIC_EXPORT_PRIVATE PerPacketOptions {
  virtual ~PerPacketOptions() {}

  // Returns a heap-allocated copy of |this|.
  //
  // The subclass implementation of this method should look like this:
  //   return std::make_unique<MyAwesomePerPacketOptions>(*this);
  //
  // This method is declared pure virtual in order to ensure the subclasses
  // would not forget to override it.
  virtual std::unique_ptr<PerPacketOptions> Clone() const = 0;

  // Specifies release time delay for this packet.
  QuicTime::Delta release_time_delay = QuicTime::Delta::Zero();
};

// An interface between writers and the entity managing the
// socket (in our case the QuicDispatcher).  This allows the Dispatcher to
// control writes, and manage any writers who end up write blocked.
// A concrete writer works in one of the two modes:
// - PassThrough mode. This is the default mode. Caller calls WritePacket with
//   caller-allocated packet buffer. Unless the writer is blocked, each call to
//   WritePacket triggers a write using the underlying socket API.
//
// - Batch mode. In this mode, a call to WritePacket may not cause a packet to
//   be sent using the underlying socket API. Instead, multiple packets are
//   saved in the writer's internal buffer until they are flushed. The flush can
//   be explicit, by calling Flush, or implicit, e.g. by calling
//   WritePacket when the internal buffer is near full.
//
// Buffer management:
// In Batch mode, a writer manages an internal buffer, which is large enough to
// hold multiple packets' data. If the caller calls WritePacket with a
// caller-allocated packet buffer, the writer will memcpy the buffer into the
// internal buffer. Caller can also avoid this memcpy by:
// 1. Call GetNextWriteLocation to get a pointer P into the internal buffer.
// 2. Serialize the packet directly to P.
// 3. Call WritePacket with P as the |buffer|.
class QUIC_EXPORT_PRIVATE QuicPacketWriter {
 public:
  virtual ~QuicPacketWriter() {}

  // PassThrough mode:
  // Sends the packet out to the peer, with some optional per-packet options.
  // If the write succeeded, the result's status is WRITE_STATUS_OK and
  // bytes_written is populated. If the write failed, the result's status is
  // WRITE_STATUS_BLOCKED or WRITE_STATUS_ERROR and error_code is populated.
  //
  // Batch mode:
  // If the writer is blocked, return WRITE_STATUS_BLOCKED immediately.
  // If the packet can be batched with other buffered packets, save the packet
  // to the internal buffer.
  // If the packet can not be batched, or the internal buffer is near full after
  // it is buffered, the internal buffer is flushed to free up space.
  // Return WriteResult(WRITE_STATUS_OK, <bytes_flushed>) on success. When
  // <bytes_flushed> is zero, it means the packet is buffered and not flushed.
  // Return WRITE_STATUS_BLOCKED if the packet is not buffered and the socket is
  // blocked while flushing.
  // Otherwise return an error status.
  //
  // Options must be either null, or created for the particular QuicPacketWriter
  // implementation. Options may be ignored, depending on the implementation.
  //
  // Some comment about memory management if |buffer| was previously acquired
  // by a call to "GetNextWriteLocation()":
  //
  // a) When WRITE_STATUS_OK is returned, the caller expects the writer owns the
  // packet buffers and they will be released when the write finishes.
  //
  // b) When this function returns any status >= WRITE_STATUS_ERROR, the caller
  // expects the writer releases the buffer (if needed) before the function
  // returns.
  //
  // c) When WRITE_STATUS_BLOCKED is returned, the caller makes a copy of the
  // buffer and will retry after unblock, so if |payload| is allocated from
  // GetNextWriteLocation(), it
  //    1) needs to be released before return, and
  //    2) the content of |payload| should not change after return.
  //
  // d) When WRITE_STATUS_BLOCKED_DATA_BUFFERED is returned, the caller expects
  // 1) the writer owns the packet buffers, and 2) the writer will re-send the
  // packet when it unblocks.
  virtual WriteResult WritePacket(const char* buffer,
                                  size_t buf_len,
                                  const QuicIpAddress& self_address,
                                  const QuicSocketAddress& peer_address,
                                  PerPacketOptions* options) = 0;

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

  // Returns true if the socket supports release timestamp.
  virtual bool SupportsReleaseTime() const = 0;

  // True=Batch mode. False=PassThrough mode.
  virtual bool IsBatchMode() const = 0;

  // PassThrough mode: Return nullptr.
  //
  // Batch mode:
  // Return the starting address for the next packet's data. A minimum of
  // kMaxOutgoingPacketSize is guaranteed to be available from the returned
  // address. If the internal buffer does not have enough space, nullptr is
  // returned. All arguments should be identical to the follow-up call to
  // |WritePacket|, they are here to allow advanced packet memory management in
  // packet writers, e.g. one packet buffer pool per |peer_address|.
  virtual char* GetNextWriteLocation(const QuicIpAddress& self_address,
                                     const QuicSocketAddress& peer_address) = 0;

  // PassThrough mode: Return WriteResult(WRITE_STATUS_OK, 0).
  //
  // Batch mode:
  // Try send all buffered packets.
  // - Return WriteResult(WRITE_STATUS_OK, <bytes_flushed>) if all buffered
  //   packets were sent successfully.
  // - Return WRITE_STATUS_BLOCKED if the underlying socket is blocked while
  //   sending. Some packets may have been sent, packets not sent will stay in
  //   the internal buffer.
  // - Return a status >= WRITE_STATUS_ERROR if an error was encuontered while
  //   sending. As this is not a re-tryable error, any batched packets which
  //   were on memory acquired via GetNextWriteLocation() should be released and
  //   the batch should be dropped.
  virtual WriteResult Flush() = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_PACKET_WRITER_H_
