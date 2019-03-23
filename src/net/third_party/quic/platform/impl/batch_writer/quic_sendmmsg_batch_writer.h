// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_SENDMMSG_BATCH_WRITER_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_SENDMMSG_BATCH_WRITER_H_

#include "net/third_party/quic/platform/impl/batch_writer/quic_batch_writer_base.h"
#include "net/third_party/quic/platform/impl/quic_linux_socket_utils.h"

namespace quic {

class QuicSendmmsgBatchWriter : public QuicUdpBatchWriter {
 public:
  QuicSendmmsgBatchWriter(std::unique_ptr<QuicBatchWriterBuffer> batch_buffer,
                          int fd);

  CanBatchResult CanBatch(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          const PerPacketOptions* options) const override;

  FlushImplResult FlushImpl() override;

 protected:
  typedef QuicMMsgHdr::ControlBufferInitializer CmsgBuilder;
  FlushImplResult InternalFlushImpl(size_t cmsg_space,
                                    const CmsgBuilder& cmsg_builder);
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_SENDMMSG_BATCH_WRITER_H_
