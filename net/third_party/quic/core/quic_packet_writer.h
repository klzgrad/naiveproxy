// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_PACKET_WRITER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_PACKET_WRITER_H_

#include <cstddef>

#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_socket_address.h"

namespace quic {

struct WriteResult;

class QUIC_EXPORT_PRIVATE PerPacketOptions {
 public:
  PerPacketOptions() = default;
  virtual ~PerPacketOptions() {}

  // Returns a heap-allocated copy of |this|.
  virtual PerPacketOptions* Clone() const = 0;

  // Sets release time delay in ns for this packet.
  virtual void SetReleaseTimeDelay(uint64_t release_time_delay_ns) = 0;

 private:
  PerPacketOptions(PerPacketOptions&& other) = delete;
  PerPacketOptions(const PerPacketOptions&) = delete;
  PerPacketOptions& operator=(const PerPacketOptions&) = delete;
  PerPacketOptions& operator=(PerPacketOptions&& other) = delete;
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

  // Returns true if the socket supports release timestamp.
  virtual bool SupportsReleaseTime() const = 0;

  // True=Batch mode. False=PassThrough mode.
  virtual bool IsBatchMode() const = 0;

  // PassThrough mode: Return nullptr.
  //
  // Batch mode:
  // Return the starting address for the next packet's data. A minimum of
  // kMaxPacketSize is guaranteed to be available from the returned address. If
  // the internal buffer does not have enough space, nullptr is returned.
  virtual char* GetNextWriteLocation() const = 0;

  // PassThrough mode: Return WriteResult(WRITE_STATUS_OK, 0).
  //
  // Batch mode:
  // Try send all buffered packets.
  // - Return WriteResult(WRITE_STATUS_OK, <bytes_flushed>) if all buffered
  //   packets were sent successfully.
  // - Return WRITE_STATUS_BLOCKED, or an error status, if the underlying socket
  //   is blocked or returned an error while sending. Some packets may have been
  //   sent, packets not sent will stay in the internal buffer.
  virtual WriteResult Flush() = 0;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_PACKET_WRITER_H_
