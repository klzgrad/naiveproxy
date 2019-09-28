// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/tools/quic_client.h"

#include <dirent.h>
#include <sys/types.h>

#include <memory>

#include "net/third_party/quiche/src/quic/platform/api/quic_epoll.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_port_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_loopback.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_client_peer.h"

namespace quic {
namespace test {
namespace {

const char* kPathToFds = "/proc/self/fd";

std::string ReadLink(const std::string& path) {
  std::string result(PATH_MAX, '\0');
  ssize_t result_size = readlink(path.c_str(), &result[0], result.size());
  CHECK(result_size > 0 && static_cast<size_t>(result_size) < result.size());
  result.resize(result_size);
  return result;
}

// Counts the number of open sockets for the current process.
size_t NumOpenSocketFDs() {
  size_t socket_count = 0;
  dirent* file;
  std::unique_ptr<DIR, int (*)(DIR*)> fd_directory(opendir(kPathToFds),
                                                   closedir);
  while ((file = readdir(fd_directory.get())) != nullptr) {
    QuicStringPiece name(file->d_name);
    if (name == "." || name == "..") {
      continue;
    }

    std::string fd_path = ReadLink(QuicStrCat(kPathToFds, "/", name));
    if (QuicTextUtils::StartsWith(fd_path, "socket:")) {
      socket_count++;
    }
  }
  return socket_count;
}

// Creates a new QuicClient and Initializes it. Caller is responsible for
// deletion.
std::unique_ptr<QuicClient> CreateAndInitializeQuicClient(QuicEpollServer* eps,
                                                          uint16_t port) {
  QuicSocketAddress server_address(QuicSocketAddress(TestLoopback(), port));
  QuicServerId server_id("hostname", server_address.port(), false);
  ParsedQuicVersionVector versions = AllSupportedVersions();
  auto client =
      QuicMakeUnique<QuicClient>(server_address, server_id, versions, eps,
                                 crypto_test_utils::ProofVerifierForTesting());
  EXPECT_TRUE(client->Initialize());
  return client;
}

class QuicClientTest : public QuicTest {};

TEST_F(QuicClientTest, DoNotLeakSocketFDs) {
  // Make sure that the QuicClient doesn't leak socket FDs. Doing so could cause
  // port exhaustion in long running processes which repeatedly create clients.

  // Record initial number of FDs, after creating EpollServer and creating and
  // destroying a single client (the latter is needed since initializing
  // platform dependencies like certificate verifier may open a persistent
  // socket).
  QuicEpollServer eps;
  CreateAndInitializeQuicClient(&eps, QuicPickUnusedPortOrDie());
  size_t number_of_open_fds = NumOpenSocketFDs();

  // Create a number of clients, initialize them, and verify this has resulted
  // in additional FDs being opened.
  const int kNumClients = 50;
  for (int i = 0; i < kNumClients; ++i) {
    std::unique_ptr<QuicClient> client(
        CreateAndInitializeQuicClient(&eps, QuicPickUnusedPortOrDie()));

    // Initializing the client will create a new FD.
    EXPECT_LT(number_of_open_fds, NumOpenSocketFDs());
  }

  // The FDs created by the QuicClients should now be closed.
  EXPECT_EQ(number_of_open_fds, NumOpenSocketFDs());
}

TEST_F(QuicClientTest, CreateAndCleanUpUDPSockets) {
  QuicEpollServer eps;
  size_t number_of_open_fds = NumOpenSocketFDs();

  std::unique_ptr<QuicClient> client(
      CreateAndInitializeQuicClient(&eps, QuicPickUnusedPortOrDie()));
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
