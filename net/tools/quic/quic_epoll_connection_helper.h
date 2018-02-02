// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The Google-specific helper for QuicConnection which uses
// EpollAlarm for alarms, and used an int fd_ for writing data.

#ifndef NET_TOOLS_QUIC_QUIC_EPOLL_CONNECTION_HELPER_H_
#define NET_TOOLS_QUIC_QUIC_EPOLL_CONNECTION_HELPER_H_

#include <sys/types.h>
#include <set>

#include "base/macros.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_packet_writer.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_simple_buffer_allocator.h"
#include "net/quic/core/quic_time.h"
#include "net/tools/quic/platform/impl/quic_epoll_clock.h"
#include "net/tools/quic/quic_default_packet_writer.h"

namespace net {

class EpollServer;
class QuicRandom;

using QuicStreamBufferAllocator = SimpleBufferAllocator;

enum class QuicAllocator { SIMPLE, BUFFER_POOL };

class QuicEpollConnectionHelper : public QuicConnectionHelperInterface {
 public:
  QuicEpollConnectionHelper(EpollServer* eps, QuicAllocator allocator);
  ~QuicEpollConnectionHelper() override;

  // QuicEpollConnectionHelperInterface
  const QuicClock* GetClock() const override;
  QuicRandom* GetRandomGenerator() override;
  QuicBufferAllocator* GetStreamSendBufferAllocator() override;

 private:
  friend class QuicConnectionPeer;

  const QuicEpollClock clock_;
  QuicRandom* random_generator_;
  // Set up allocators.  They take up minimal memory before use.
  // Allocator for stream send buffers.
  QuicStreamBufferAllocator stream_buffer_allocator_;
  SimpleBufferAllocator simple_buffer_allocator_;
  QuicAllocator allocator_type_;

  DISALLOW_COPY_AND_ASSIGN(QuicEpollConnectionHelper);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_EPOLL_CONNECTION_HELPER_H_
