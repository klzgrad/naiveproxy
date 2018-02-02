// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/mock_proxy_script_fetcher.h"

#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "net/base/net_errors.h"

namespace net {

MockProxyScriptFetcher::MockProxyScriptFetcher()
    : pending_request_text_(NULL),
      waiting_for_fetch_(false),
      is_shutdown_(false) {}

MockProxyScriptFetcher::~MockProxyScriptFetcher() {}

// ProxyScriptFetcher implementation.
int MockProxyScriptFetcher::Fetch(const GURL& url, base::string16* text,
                                  const CompletionCallback& callback) {
  DCHECK(!has_pending_request());

  if (waiting_for_fetch_)
    base::RunLoop::QuitCurrentWhenIdleDeprecated();

  if (is_shutdown_)
    return ERR_CONTEXT_SHUT_DOWN;

  // Save the caller's information, and have them wait.
  pending_request_url_ = url;
  pending_request_callback_ = callback;
  pending_request_text_ = text;

  return ERR_IO_PENDING;
}

void MockProxyScriptFetcher::NotifyFetchCompletion(
    int result, const std::string& ascii_text) {
  DCHECK(has_pending_request());
  *pending_request_text_ = base::ASCIIToUTF16(ascii_text);
  CompletionCallback callback = pending_request_callback_;
  pending_request_callback_.Reset();
  callback.Run(result);
}

void MockProxyScriptFetcher::Cancel() {
  pending_request_callback_.Reset();
}

void MockProxyScriptFetcher::OnShutdown() {
  is_shutdown_ = true;
  if (pending_request_callback_) {
    base::ResetAndReturn(&pending_request_callback_).Run(ERR_CONTEXT_SHUT_DOWN);
  }
}

URLRequestContext* MockProxyScriptFetcher::GetRequestContext() const {
  return NULL;
}

const GURL& MockProxyScriptFetcher::pending_request_url() const {
  return pending_request_url_;
}

bool MockProxyScriptFetcher::has_pending_request() const {
  return !pending_request_callback_.is_null();
}

void MockProxyScriptFetcher::WaitUntilFetch() {
  DCHECK(!has_pending_request());
  waiting_for_fetch_ = true;
  base::RunLoop().Run();
  waiting_for_fetch_ = false;
}

}  // namespace net
