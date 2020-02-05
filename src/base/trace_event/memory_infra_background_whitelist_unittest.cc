// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/memory_infra_background_whitelist.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace base {

namespace trace_event {

TEST(MemoryInfraBackgroundWhitelist, Whitelist) {
  // Global dumps that are of hex digits are all whitelisted for background use.
  EXPECT_TRUE(IsMemoryAllocatorDumpNameWhitelisted("global/01234ABCDEF"));
  EXPECT_TRUE(
      IsMemoryAllocatorDumpNameWhitelisted("shared_memory/01234ABCDEF"));

  // Global dumps that contain non-hex digits are not whitelisted.
  EXPECT_FALSE(IsMemoryAllocatorDumpNameWhitelisted("global/GHIJK"));
  EXPECT_FALSE(IsMemoryAllocatorDumpNameWhitelisted("shared_memory/GHIJK"));

  // Test a couple that contain pointer values.
  EXPECT_TRUE(IsMemoryAllocatorDumpNameWhitelisted("net/url_request_context"));
  EXPECT_TRUE(IsMemoryAllocatorDumpNameWhitelisted(
      "net/url_request_context/app_request/0x123/cookie_monster"));
  EXPECT_TRUE(
      IsMemoryAllocatorDumpNameWhitelisted("net/http_network_session_0x123"));
  EXPECT_FALSE(
      IsMemoryAllocatorDumpNameWhitelisted("net/http_network_session/0x123"));
  EXPECT_TRUE(IsMemoryAllocatorDumpNameWhitelisted(
      "net/http_network_session_0x123/quic_stream_factory"));
}

}  // namespace trace_event

}  // namespace base
