// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CORE_SPDY_TEST_UTILS_H_
#define NET_SPDY_CORE_SPDY_TEST_UTILS_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>

#include "net/spdy/chromium/server_push_delegate.h"
#include "net/spdy/core/spdy_bug_tracker.h"
#include "net/spdy/core/spdy_header_block.h"
#include "net/spdy/core/spdy_headers_handler_interface.h"
#include "net/spdy/core/spdy_protocol.h"
#include "net/spdy/platform/api/spdy_string.h"
#include "net/spdy/platform/api/spdy_string_piece.h"
#include "net/test/gtest_util.h"

#define EXPECT_SPDY_BUG EXPECT_DFATAL

namespace net {

class HashValue;
class TransportSecurityState;

inline bool operator==(SpdyStringPiece x,
                       const SpdyHeaderBlock::ValueProxy& y) {
  return x == y.as_string();
}

namespace test {

SpdyString HexDumpWithMarks(const unsigned char* data,
                            int length,
                            const bool* marks,
                            int mark_length);

void CompareCharArraysWithHexError(const SpdyString& description,
                                   const unsigned char* actual,
                                   const int actual_len,
                                   const unsigned char* expected,
                                   const int expected_len);

void SetFrameFlags(SpdySerializedFrame* frame, uint8_t flags);

void SetFrameLength(SpdySerializedFrame* frame, size_t length);

// Returns a SHA1 HashValue in which each byte has the value |label|.
HashValue GetTestHashValue(uint8_t label);

// Returns SHA1 pinning header for the of the base64 encoding of
// GetTestHashValue(|label|).
SpdyString GetTestPin(uint8_t label);

// Adds a pin for |host| to |state|.
void AddPin(TransportSecurityState* state,
            const SpdyString& host,
            uint8_t primary_label,
            uint8_t backup_label);

// A test implementation of SpdyHeadersHandlerInterface that correctly
// reconstructs multiple header values for the same name.
class TestHeadersHandler : public SpdyHeadersHandlerInterface {
 public:
  TestHeadersHandler() {}

  void OnHeaderBlockStart() override;

  void OnHeader(SpdyStringPiece name, SpdyStringPiece value) override;

  void OnHeaderBlockEnd(size_t header_bytes_parsed,
                        size_t compressed_header_bytes_parsed) override;

  const SpdyHeaderBlock& decoded_block() const { return block_; }
  size_t header_bytes_parsed() const { return header_bytes_parsed_; }
  size_t compressed_header_bytes_parsed() const {
    return compressed_header_bytes_parsed_;
  }

 private:
  SpdyHeaderBlock block_;
  size_t header_bytes_parsed_ = 0;
  size_t compressed_header_bytes_parsed_ = 0;

  DISALLOW_COPY_AND_ASSIGN(TestHeadersHandler);
};

// A test implementation of ServerPushDelegate that caches all the pushed
// request and provides a interface to cancel the push given url.
class TestServerPushDelegate : public ServerPushDelegate {
 public:
  TestServerPushDelegate();
  ~TestServerPushDelegate() override;

  void OnPush(std::unique_ptr<ServerPushHelper> push_helper,
              const NetLogWithSource& session_net_log) override;

  bool CancelPush(GURL url);

 private:
  std::map<GURL, std::unique_ptr<ServerPushHelper>> push_helpers;
};

}  // namespace test
}  // namespace net

#endif  // NET_SPDY_CORE_SPDY_TEST_UTILS_H_
