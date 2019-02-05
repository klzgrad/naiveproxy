// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_handler.h"

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_task_environment.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/http_auth_handler_mock.h"
#include "net/http/http_request_info.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source_type.h"
#include "net/log/test_net_log.h"
#include "net/log/test_net_log_entry.h"
#include "net/log/test_net_log_util.h"
#include "net/ssl/ssl_info.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

TEST(HttpAuthHandlerTest, NetLog) {
  base::test::ScopedTaskEnvironment scoped_task_environment;

  GURL origin("http://www.example.com");
  std::string challenge = "Mock asdf";
  AuthCredentials credentials(base::ASCIIToUTF16("user"),
                              base::ASCIIToUTF16("pass"));
  std::string auth_token;
  HttpRequestInfo request;

  for (int i = 0; i < 2; ++i) {
    bool async = (i == 0);
    for (int j = 0; j < 2; ++j) {
      int rv = (j == 0) ? OK : ERR_UNEXPECTED;
      for (int k = 0; k < 2; ++k) {
        TestCompletionCallback test_callback;
        HttpAuth::Target target =
            (k == 0) ? HttpAuth::AUTH_PROXY : HttpAuth::AUTH_SERVER;
        NetLogEventType event_type = (k == 0) ? NetLogEventType::AUTH_PROXY
                                              : NetLogEventType::AUTH_SERVER;
        HttpAuthChallengeTokenizer tokenizer(
            challenge.begin(), challenge.end());
        HttpAuthHandlerMock mock_handler;
        TestNetLog test_net_log;
        NetLogWithSource net_log(
            NetLogWithSource::Make(&test_net_log, NetLogSourceType::NONE));

        SSLInfo empty_ssl_info;
        mock_handler.InitFromChallenge(&tokenizer, target, empty_ssl_info,
                                       origin, net_log);
        mock_handler.SetGenerateExpectation(async, rv);
        mock_handler.GenerateAuthToken(&credentials, &request,
                                       test_callback.callback(), &auth_token);
        if (async)
          test_callback.WaitForResult();

        TestNetLogEntry::List entries;
        test_net_log.GetEntries(&entries);

        EXPECT_EQ(2u, entries.size());
        EXPECT_TRUE(LogContainsBeginEvent(entries, 0, event_type));
        EXPECT_TRUE(LogContainsEndEvent(entries, 1, event_type));
      }
    }
  }
}

}  // namespace net
