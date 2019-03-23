// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_BATCH_WRITER_TEST_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_BATCH_WRITER_TEST_H_

#include <linux/sock_diag.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <iostream>

#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/impl/batch_writer/quic_batch_writer_base.h"
#include "net/third_party/quic/platform/impl/quic_socket_utils.h"

namespace quic {
namespace test {

static bool IsAddressFamilySupported(int address_family) {
  static auto check_function = [](int address_family) {
    int fd = socket(address_family, SOCK_STREAM, 0);
    if (fd < 0) {
      QUIC_LOG(ERROR) << "address_family not supported: " << address_family
                      << ", error: " << strerror(errno);
      EXPECT_EQ(EAFNOSUPPORT, errno);
      return false;
    }
    close(fd);
    return true;
  };

  if (address_family == AF_INET) {
    static const bool ipv4_supported = check_function(AF_INET);
    return ipv4_supported;
  }

  static const bool ipv6_supported = check_function(AF_INET6);
  return ipv6_supported;
}

static bool CreateSocket(int family, QuicSocketAddress* address, int* fd) {
  if (family == AF_INET) {
    *address = QuicSocketAddress(QuicIpAddress::Loopback4(), 0);
  } else {
    DCHECK_EQ(family, AF_INET6);
    *address = QuicSocketAddress(QuicIpAddress::Loopback6(), 0);
  }

  bool overflow_supported = false;
  *fd = QuicSocketUtils::CreateUDPSocket(
      *address, /*receive_buffer_size=*/kDefaultSocketReceiveBuffer,
      /*send_buffer_size=*/kDefaultSocketReceiveBuffer, &overflow_supported);
  if (*fd < 0) {
    QUIC_LOG(ERROR) << "CreateSocket() failed: " << strerror(errno);
    return false;
  }

  sockaddr_storage addr = address->generic_address();
  int rc = bind(*fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (rc < 0) {
    QUIC_LOG(ERROR) << "Bind failed: " << strerror(errno);
    return false;
  }
  if (address->FromSocket(*fd) != 0) {
    QUIC_LOG(ERROR) << "Unable to get self address.  Error: "
                    << strerror(errno);
    return false;
  }
  return true;
}

struct QuicUdpBatchWriterIOTestParams;
class QuicUdpBatchWriterIOTestDelegate {
 public:
  virtual ~QuicUdpBatchWriterIOTestDelegate() {}

  virtual bool ShouldSkip(const QuicUdpBatchWriterIOTestParams& params) {
    return false;
  }

  virtual void ResetWriter(int fd) = 0;

  virtual QuicUdpBatchWriter* GetWriter() = 0;
};

struct QuicUdpBatchWriterIOTestParams {
  // Use shared_ptr because gtest makes copies of test params.
  std::shared_ptr<QuicUdpBatchWriterIOTestDelegate> delegate;
  int address_family;
  int data_size;
  int packet_size;

  friend std::ostream& operator<<(std::ostream& os,
                                  const QuicUdpBatchWriterIOTestParams& p) {
    os << "{ address_family: " << p.address_family
       << " data_size: " << p.data_size << " packet_size: " << p.packet_size
       << " }";
    return os;
  }
};

template <class QuicUdpBatchWriterIOTestDelegateT>
static std::vector<QuicUdpBatchWriterIOTestParams>
MakeQuicBatchWriterTestParams() {
  static_assert(std::is_base_of<QuicUdpBatchWriterIOTestDelegate,
                                QuicUdpBatchWriterIOTestDelegateT>::value,
                "<QuicUdpBatchWriterIOTestDelegateT> needs to derive from "
                "QuicUdpBatchWriterIOTestDelegate");

  std::vector<QuicUdpBatchWriterIOTestParams> params;
  for (int address_family : {AF_INET, AF_INET6}) {
    for (int data_size : {1, 150, 1500, 15000, 64000, 200 * 1024}) {
      for (int packet_size : {1, 50, 1350, 1452}) {
        if (packet_size <= data_size && (data_size / packet_size < 200)) {
          params.push_back({QuicMakeUnique<QuicUdpBatchWriterIOTestDelegateT>(),
                            address_family, data_size, packet_size});
          params.push_back({QuicMakeUnique<QuicUdpBatchWriterIOTestDelegateT>(),
                            address_family, data_size, packet_size});
        }
      }
    }
  }
  return params;
}

// QuicUdpBatchWriterIOTest is a value parameterized test fixture that can be
// used by tests of derived classes of QuicUdpBatchWriter, to verify basic
// packet IO capabilities.
class QuicUdpBatchWriterIOTest
    : public QuicTestWithParam<QuicUdpBatchWriterIOTestParams> {
 protected:
  QuicUdpBatchWriterIOTest()
      : address_family_(GetParam().address_family),
        data_size_(GetParam().data_size),
        packet_size_(GetParam().packet_size),
        self_socket_(-1),
        peer_socket_(-1) {
    QUIC_LOG(INFO) << "QuicUdpBatchWriterIOTestParams: " << GetParam();
    EXPECT_TRUE(address_family_ == AF_INET || address_family_ == AF_INET6);
    EXPECT_LE(packet_size_, data_size_);
    EXPECT_LE(packet_size_, static_cast<int>(sizeof(packet_buffer_)));
  }

  ~QuicUdpBatchWriterIOTest() override {
    if (self_socket_ > 0) {
      close(self_socket_);
    }
    if (peer_socket_ > 0) {
      close(peer_socket_);
    }
  }

  // Whether this test should be skipped. A test is passed if skipped.
  // A test can be skipped when e.g. it exercises a kernel feature that is not
  // available on the system.
  bool ShouldSkip() {
    if (!IsAddressFamilySupported(address_family_)) {
      QUIC_LOG(WARNING)
          << "Test skipped since address_family is not supported.";
      return true;
    }

    return GetParam().delegate->ShouldSkip(GetParam());
  }

  // Initialize a test.
  // To fail the test in Initialize, use ASSERT_xx macros.
  void Initialize() {
    ASSERT_TRUE(CreateSocket(address_family_, &self_address_, &self_socket_));
    ASSERT_TRUE(CreateSocket(address_family_, &peer_address_, &peer_socket_));

    int rc = QuicSocketUtils::SetGetAddressInfo(peer_socket_, address_family_);
    ASSERT_EQ(rc, 0) << "Configuring peer socket failed: " << strerror(errno);

    QUIC_DLOG(INFO) << "Self address: " << self_address_.ToString() << ", fd "
                    << self_socket_;
    QUIC_DLOG(INFO) << "Peer address: " << peer_address_.ToString() << ", fd "
                    << peer_socket_;
    GetParam().delegate->ResetWriter(self_socket_);
  }

  QuicUdpBatchWriter* GetWriter() { return GetParam().delegate->GetWriter(); }

  void ValidateWrite() {
    char this_packet_content = '\0';
    int this_packet_size;
    int num_writes = 0;
    int bytes_flushed = 0;
    WriteResult result;

    for (int bytes_sent = 0; bytes_sent < data_size_;
         bytes_sent += this_packet_size, ++this_packet_content) {
      this_packet_size = std::min(packet_size_, data_size_ - bytes_sent);
      memset(&packet_buffer_[0], this_packet_content, this_packet_size);

      result = GetWriter()->WritePacket(&packet_buffer_[0], this_packet_size,
                                        self_address_.host(), peer_address_,
                                        nullptr);

      ASSERT_EQ(WRITE_STATUS_OK, result.status) << strerror(result.error_code);
      bytes_flushed += result.bytes_written;
      ++num_writes;

      QUIC_DVLOG(1) << "[write #" << num_writes
                    << "] this_packet_size: " << this_packet_size
                    << ", total_bytes_sent: " << bytes_sent + this_packet_size
                    << ", bytes_flushed: " << bytes_flushed
                    << ", pkt content:" << std::hex << int(this_packet_content);
    }

    result = GetWriter()->Flush();
    ASSERT_EQ(WRITE_STATUS_OK, result.status) << strerror(result.error_code);
    bytes_flushed += result.bytes_written;
    ASSERT_EQ(data_size_, bytes_flushed);

    QUIC_LOG(INFO) << "Sent " << data_size_ << " bytes in " << num_writes
                   << " writes.";
  }

  void ValidateRead() {
    char this_packet_content = '\0';
    int this_packet_size;
    int packets_received = 0;
    for (int bytes_received = 0; bytes_received < data_size_;
         bytes_received += this_packet_size, ++this_packet_content) {
      this_packet_size = std::min(packet_size_, data_size_ - bytes_received);
      SCOPED_TRACE(testing::Message()
                   << "Before ReadPacket: bytes_received=" << bytes_received
                   << ", this_packet_size=" << this_packet_size);

      QuicIpAddress read_self_address;
      QuicSocketAddress read_peer_address;
      ASSERT_EQ(this_packet_size,
                QuicSocketUtils::ReadPacket(
                    peer_socket_, &packet_buffer_[0], sizeof(packet_buffer_),
                    nullptr, &read_self_address, nullptr, &read_peer_address))
          << "ReadPacket result: errno=" << errno
          << ", dropped_packets=" << GetPacketDropCount(peer_socket_);

      EXPECT_EQ(read_self_address, peer_address_.host());
      EXPECT_EQ(read_peer_address, self_address_);
      for (int i = 0; i < this_packet_size; ++i) {
        EXPECT_EQ(this_packet_content, packet_buffer_[i]);
      }
      packets_received += this_packet_size;
    }

    EXPECT_EQ(0u, GetPacketDropCount(peer_socket_));
    QUIC_LOG(INFO) << "Received " << data_size_ << " bytes in "
                   << packets_received << " packets.";
  }

  uint32_t GetPacketDropCount(int fd) const {
    uint32_t meminfo[SK_MEMINFO_VARS];
    socklen_t len = sizeof(meminfo);

    if (getsockopt(fd, SOL_SOCKET, SO_MEMINFO, meminfo, &len) != 0) {
      LOG(WARNING)
          << "getsockopt failed. Assuming there's no packet drop on fd " << fd;
      return 0;
    }

    if (len != sizeof(meminfo)) {
      LOG(WARNING)
          << "Bad meminfo length. Assuming there's no packet drop on fd " << fd;
      return 0;
    }

    return meminfo[SK_MEMINFO_DROPS];
  }

  QuicSocketAddress self_address_;
  QuicSocketAddress peer_address_;
  char packet_buffer_[1500];
  int address_family_;
  const int data_size_;
  const int packet_size_;
  int self_socket_;
  int peer_socket_;
};

namespace {

TEST_P(QuicUdpBatchWriterIOTest, WriteAndRead) {
  if (ShouldSkip()) {
    return;
  }

  Initialize();

  ValidateWrite();
  ValidateRead();
}

}  // namespace
}  // namespace test
}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_BATCH_WRITER_TEST_H_
