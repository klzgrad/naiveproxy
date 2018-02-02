// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_MOCK_PROXY_SCRIPT_FETCHER_H_
#define NET_PROXY_MOCK_PROXY_SCRIPT_FETCHER_H_

#include "base/compiler_specific.h"
#include "net/proxy/proxy_script_fetcher.h"
#include "url/gurl.h"

#include <string>

namespace net {

class URLRequestContext;

// A mock ProxyScriptFetcher. No result will be returned to the fetch client
// until we call NotifyFetchCompletion() to set the results.
class MockProxyScriptFetcher : public ProxyScriptFetcher {
 public:
  MockProxyScriptFetcher();
  ~MockProxyScriptFetcher() override;

  // ProxyScriptFetcher implementation.
  int Fetch(const GURL& url,
            base::string16* text,
            const CompletionCallback& callback) override;
  void Cancel() override;
  void OnShutdown() override;
  URLRequestContext* GetRequestContext() const override;

  void NotifyFetchCompletion(int result, const std::string& ascii_text);
  const GURL& pending_request_url() const;
  bool has_pending_request() const;

  // Spins the message loop until this->Fetch() is invoked.
  void WaitUntilFetch();

 private:
  GURL pending_request_url_;
  CompletionCallback pending_request_callback_;
  base::string16* pending_request_text_;
  bool waiting_for_fetch_;
  bool is_shutdown_;
};

}  // namespace net

#endif  // NET_PROXY_MOCK_PROXY_SCRIPT_FETCHER_H_
