// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_proxy_client_socket.h"

#include "build/build_config.h"
#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/log/test_net_log.h"
#include "net/socket/next_proto.h"
#include "net/socket/socket_tag.h"
#include "net/socket/socket_test_util.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

TEST(HttpProxyClientSocketTest, Tag) {
  StaticSocketDataProvider data;
  TestNetLog log;
  MockTaggingStreamSocket* tagging_sock =
      new MockTaggingStreamSocket(std::unique_ptr<StreamSocket>(
          new MockTCPClientSocket(AddressList(), &log, &data)));

  std::unique_ptr<ClientSocketHandle> connection(new ClientSocketHandle);
  // |connection| takes ownership of |tagging_sock|, but keep a
  // non-owning pointer to it.
  connection->SetSocket(std::unique_ptr<StreamSocket>(tagging_sock));
  HttpProxyClientSocket socket(std::move(connection), "", HostPortPair(),
                               nullptr, false, false, NextProto(), false,
                               TRAFFIC_ANNOTATION_FOR_TESTS);

  EXPECT_EQ(tagging_sock->tag(), SocketTag());
#if defined(OS_ANDROID)
  SocketTag tag(0x12345678, 0x87654321);
  socket.ApplySocketTag(tag);
  EXPECT_EQ(tagging_sock->tag(), tag);
#endif  // OS_ANDROID
}

}  // namespace

}  // namespace net
