// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/tools/quic_client.h"

#include <dirent.h>
#include <stdio.h>

#include <memory>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_test_loopback.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quic/test_tools/quic_client_peer.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"
#include "net/tools/epoll_server/epoll_server.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace quic {
namespace test {
namespace {

const char* kPathToFds = "/proc/self/fd";

// Counts the number of open sockets for the current process.
size_t NumOpenSocketFDs() {
  base::FileEnumerator fd_entries(
      base::FilePath(kPathToFds), false,
      base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS);

  size_t socket_count = 0;
  for (base::FilePath entry = fd_entries.Next(); !entry.empty();
       entry = fd_entries.Next()) {
    base::FilePath fd_path;
    if (!base::ReadSymbolicLink(entry, &fd_path)) {
      continue;
    }
    if (QuicTextUtils::StartsWith(fd_path.value(), "socket:")) {
      socket_count++;
    }
  }

  return socket_count;
}

// Creates a new QuicClient and Initializes it. Caller is responsible for
// deletion.
QuicClient* CreateAndInitializeQuicClient(net::EpollServer* eps,
                                          uint16_t port) {
  QuicSocketAddress server_address(QuicSocketAddress(TestLoopback(), port));
  QuicServerId server_id("hostname", server_address.port(), false);
  ParsedQuicVersionVector versions = AllSupportedVersions();
  QuicClient* client =
      new QuicClient(server_address, server_id, versions, eps,
                     crypto_test_utils::ProofVerifierForTesting());
  EXPECT_TRUE(client->Initialize());
  return client;
}

class QuicClientTest : public QuicTest {};

TEST_F(QuicClientTest, DoNotLeakSocketFDs) {
  // Make sure that the QuicClient doesn't leak socket FDs. Doing so could cause
  // port exhaustion in long running processes which repeatedly create clients.

  // Create a ProofVerifier before counting the number of open FDs to work
  // around some memory corruption detector weirdness.
  crypto_test_utils::ProofVerifierForTesting().reset();

  // Record initial number of FDs, after creation of net::EpollServer.
  net::EpollServer eps;
  size_t number_of_open_fds = NumOpenSocketFDs();

  // Create a number of clients, initialize them, and verify this has resulted
  // in additional FDs being opened.
  const int kNumClients = 50;
  for (int i = 0; i < kNumClients; ++i) {
    std::unique_ptr<QuicClient> client(
        CreateAndInitializeQuicClient(&eps, kTestPort + i));

    // Initializing the client will create a new FD.
    EXPECT_LT(number_of_open_fds, NumOpenSocketFDs());
  }

  // The FDs created by the QuicClients should now be closed.
  EXPECT_EQ(number_of_open_fds, NumOpenSocketFDs());
}

TEST_F(QuicClientTest, CreateAndCleanUpUDPSockets) {
  // Create a ProofVerifier before counting the number of open FDs to work
  // around some memory corruption detector weirdness.
  crypto_test_utils::ProofVerifierForTesting().reset();

  net::EpollServer eps;
  size_t number_of_open_fds = NumOpenSocketFDs();

  std::unique_ptr<QuicClient> client(
      CreateAndInitializeQuicClient(&eps, kTestPort));
  EXPECT_EQ(number_of_open_fds + 1, NumOpenSocketFDs());
  // Create more UDP sockets.
  EXPECT_TRUE(QuicClientPeer::CreateUDPSocketAndBind(client.get()));
  EXPECT_EQ(number_of_open_fds + 2, NumOpenSocketFDs());
  EXPECT_TRUE(QuicClientPeer::CreateUDPSocketAndBind(client.get()));
  EXPECT_EQ(number_of_open_fds + 3, NumOpenSocketFDs());

  // Clean up UDP sockets.
  QuicClientPeer::CleanUpUDPSocket(client.get(), client->GetLatestFD());
  EXPECT_EQ(number_of_open_fds + 2, NumOpenSocketFDs());
  QuicClientPeer::CleanUpUDPSocket(client.get(), client->GetLatestFD());
  EXPECT_EQ(number_of_open_fds + 1, NumOpenSocketFDs());
}

}  // namespace
}  // namespace test
}  // namespace quic
