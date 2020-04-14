// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_PACKET_WRITER_WRAPPER_H_
#define QUICHE_QUIC_CORE_QUIC_PACKET_WRITER_WRAPPER_H_

#include <cstddef>
#include <memory>

#include "net/third_party/quiche/src/quic/core/quic_packet_writer.h"

namespace quic {

// Wraps a writer object to allow dynamically extending functionality. Use
// cases: replace writer while dispatcher and connections hold on to the
// wrapper; mix in monitoring; mix in mocks in unit tests.
class QUIC_NO_EXPORT QuicPacketWriterWrapper : public QuicPacketWriter {
 public:
  QuicPacketWriterWrapper();
  QuicPacketWriterWrapper(const QuicPacketWriterWrapper&) = delete;
  QuicPacketWriterWrapper& operator=(const QuicPacketWriterWrapper&) = delete;
  ~QuicPacketWriterWrapper() override;

  // Default implementation of the QuicPacketWriter interface. Passes everything
  // to |writer_|.
  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override;
  bool IsWriteBlocked() const override;
  void SetWritable() override;
  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& peer_address) const override;
  bool SupportsReleaseTime() const override;
  bool IsBatchMode() const override;
  char* GetNextWriteLocation(const QuicIpAddress& self_address,
                             const QuicSocketAddress& peer_address) override;
  WriteResult Flush() override;

  // Takes ownership of |writer|.
  void set_writer(QuicPacketWriter* writer);

  // Does not take ownership of |writer|.
  void set_non_owning_writer(QuicPacketWriter* writer);

  virtual void set_peer_address(const QuicSocketAddress& /*peer_address*/) {}

  QuicPacketWriter* writer() { return writer_; }

 private:
  void unset_writer();

  QuicPacketWriter* writer_ = nullptr;
  bool owns_writer_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_PACKET_WRITER_WRAPPER_H_
