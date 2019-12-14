// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_DEFAULT_PACKET_WRITER_H_
#define QUICHE_QUIC_CORE_QUIC_DEFAULT_PACKET_WRITER_H_

#include <cstddef>

#include "net/third_party/quiche/src/quic/core/quic_packet_writer.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"

namespace quic {

struct WriteResult;

// Default packet writer which wraps QuicSocketUtils WritePacket.
class QUIC_EXPORT_PRIVATE QuicDefaultPacketWriter : public QuicPacketWriter {
 public:
  explicit QuicDefaultPacketWriter(int fd);
  QuicDefaultPacketWriter(const QuicDefaultPacketWriter&) = delete;
  QuicDefaultPacketWriter& operator=(const QuicDefaultPacketWriter&) = delete;
  ~QuicDefaultPacketWriter() override;

  // QuicPacketWriter
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

  void set_fd(int fd) { fd_ = fd; }

 protected:
  void set_write_blocked(bool is_blocked);
  int fd() { return fd_; }

 private:
  int fd_;
  bool write_blocked_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_DEFAULT_PACKET_WRITER_H_
