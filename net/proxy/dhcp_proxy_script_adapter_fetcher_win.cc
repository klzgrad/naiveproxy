// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/dhcp_proxy_script_adapter_fetcher_win.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/free_deleter.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task_runner.h"
#include "base/time/time.h"
#include "net/base/net_errors.h"
#include "net/proxy/dhcpcsvc_init_win.h"
#include "net/proxy/proxy_script_fetcher_impl.h"
#include "net/url_request/url_request_context.h"

#include <windows.h>
#include <winsock2.h>
#include <dhcpcsdk.h>

namespace {

// Maximum amount of time to wait for response from the Win32 DHCP API.
const int kTimeoutMs = 2000;

}  // namespace

namespace net {

DhcpProxyScriptAdapterFetcher::DhcpProxyScriptAdapterFetcher(
    URLRequestContext* url_request_context,
    scoped_refptr<base::TaskRunner> task_runner)
    : task_runner_(task_runner),
      state_(STATE_START),
      result_(ERR_IO_PENDING),
      url_request_context_(url_request_context) {
  DCHECK(url_request_context_);
}

DhcpProxyScriptAdapterFetcher::~DhcpProxyScriptAdapterFetcher() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  Cancel();
}

void DhcpProxyScriptAdapterFetcher::Fetch(
    const std::string& adapter_name, const CompletionCallback& callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK_EQ(state_, STATE_START);
  result_ = ERR_IO_PENDING;
  pac_script_ = base::string16();
  state_ = STATE_WAIT_DHCP;
  callback_ = callback;

  wait_timer_.Start(FROM_HERE, ImplGetTimeout(),
                    this, &DhcpProxyScriptAdapterFetcher::OnTimeout);
  scoped_refptr<DhcpQuery> dhcp_query(ImplCreateDhcpQuery());
  task_runner_->PostTaskAndReply(
      FROM_HERE,
      base::Bind(
          &DhcpProxyScriptAdapterFetcher::DhcpQuery::GetPacURLForAdapter,
          dhcp_query.get(),
          adapter_name),
      base::Bind(
          &DhcpProxyScriptAdapterFetcher::OnDhcpQueryDone,
          AsWeakPtr(),
          dhcp_query));
}

void DhcpProxyScriptAdapterFetcher::Cancel() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  callback_.Reset();
  wait_timer_.Stop();
  script_fetcher_.reset();

  switch (state_) {
    case STATE_WAIT_DHCP:
      // Nothing to do here, we let the worker thread run to completion,
      // the task it posts back when it completes will check the state.
      break;
    case STATE_WAIT_URL:
      break;
    case STATE_START:
    case STATE_FINISH:
    case STATE_CANCEL:
      break;
  }

  if (state_ != STATE_FINISH) {
    result_ = ERR_ABORTED;
    state_ = STATE_CANCEL;
  }
}

bool DhcpProxyScriptAdapterFetcher::DidFinish() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return state_ == STATE_FINISH;
}

int DhcpProxyScriptAdapterFetcher::GetResult() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return result_;
}

base::string16 DhcpProxyScriptAdapterFetcher::GetPacScript() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return pac_script_;
}

GURL DhcpProxyScriptAdapterFetcher::GetPacURL() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return pac_url_;
}

DhcpProxyScriptAdapterFetcher::DhcpQuery::DhcpQuery() {
}

void DhcpProxyScriptAdapterFetcher::DhcpQuery::GetPacURLForAdapter(
    const std::string& adapter_name) {
  url_ = ImplGetPacURLFromDhcp(adapter_name);
}

const std::string& DhcpProxyScriptAdapterFetcher::DhcpQuery::url() const {
  return url_;
}

std::string
    DhcpProxyScriptAdapterFetcher::DhcpQuery::ImplGetPacURLFromDhcp(
        const std::string& adapter_name) {
  return DhcpProxyScriptAdapterFetcher::GetPacURLFromDhcp(adapter_name);
}

DhcpProxyScriptAdapterFetcher::DhcpQuery::~DhcpQuery() {
}

void DhcpProxyScriptAdapterFetcher::OnDhcpQueryDone(
    scoped_refptr<DhcpQuery> dhcp_query) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // Because we can't cancel the call to the Win32 API, we can expect
  // it to finish while we are in a few different states.  The expected
  // one is WAIT_DHCP, but it could be in CANCEL if Cancel() was called,
  // or FINISH if timeout occurred.
  DCHECK(state_ == STATE_WAIT_DHCP || state_ == STATE_CANCEL ||
         state_ == STATE_FINISH);
  if (state_ != STATE_WAIT_DHCP)
    return;

  wait_timer_.Stop();

  pac_url_ = GURL(dhcp_query->url());
  if (pac_url_.is_empty() || !pac_url_.is_valid()) {
    result_ = ERR_PAC_NOT_IN_DHCP;
    TransitionToFinish();
  } else {
    state_ = STATE_WAIT_URL;
    script_fetcher_.reset(ImplCreateScriptFetcher());
    script_fetcher_->Fetch(
        pac_url_, &pac_script_,
        base::Bind(&DhcpProxyScriptAdapterFetcher::OnFetcherDone,
                   base::Unretained(this)));
  }
}

void DhcpProxyScriptAdapterFetcher::OnTimeout() {
  DCHECK_EQ(state_, STATE_WAIT_DHCP);
  result_ = ERR_TIMED_OUT;
  TransitionToFinish();
}

void DhcpProxyScriptAdapterFetcher::OnFetcherDone(int result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(state_ == STATE_WAIT_URL || state_ == STATE_CANCEL);
  if (state_ == STATE_CANCEL)
    return;

  // At this point, pac_script_ has already been written to.
  script_fetcher_.reset();
  result_ = result;
  TransitionToFinish();
}

void DhcpProxyScriptAdapterFetcher::TransitionToFinish() {
  DCHECK(state_ == STATE_WAIT_DHCP || state_ == STATE_WAIT_URL);
  state_ = STATE_FINISH;
  CompletionCallback callback = callback_;
  callback_.Reset();

  // Be careful not to touch any member state after this, as the client
  // may delete us during this callback.
  callback.Run(result_);
}

DhcpProxyScriptAdapterFetcher::State
    DhcpProxyScriptAdapterFetcher::state() const {
  return state_;
}

ProxyScriptFetcher* DhcpProxyScriptAdapterFetcher::ImplCreateScriptFetcher() {
  return new ProxyScriptFetcherImpl(url_request_context_);
}

DhcpProxyScriptAdapterFetcher::DhcpQuery*
    DhcpProxyScriptAdapterFetcher::ImplCreateDhcpQuery() {
  return new DhcpQuery();
}

base::TimeDelta DhcpProxyScriptAdapterFetcher::ImplGetTimeout() const {
  return base::TimeDelta::FromMilliseconds(kTimeoutMs);
}

// static
std::string DhcpProxyScriptAdapterFetcher::GetPacURLFromDhcp(
    const std::string& adapter_name) {
  EnsureDhcpcsvcInit();

  std::wstring adapter_name_wide = base::SysMultiByteToWide(adapter_name,
                                                            CP_ACP);

  DHCPCAPI_PARAMS_ARRAY send_params = { 0, NULL };

  DHCPCAPI_PARAMS wpad_params = { 0 };
  wpad_params.OptionId = 252;
  wpad_params.IsVendor = FALSE;  // Surprising, but intentional.

  DHCPCAPI_PARAMS_ARRAY request_params = { 0 };
  request_params.nParams = 1;
  request_params.Params = &wpad_params;

  // The maximum message size is typically 4096 bytes on Windows per
  // http://support.microsoft.com/kb/321592
  DWORD result_buffer_size = 4096;
  std::unique_ptr<BYTE, base::FreeDeleter> result_buffer;
  int retry_count = 0;
  DWORD res = NO_ERROR;
  do {
    result_buffer.reset(static_cast<BYTE*>(malloc(result_buffer_size)));

    // Note that while the DHCPCAPI_REQUEST_SYNCHRONOUS flag seems to indicate
    // there might be an asynchronous mode, there seems to be (at least in
    // terms of well-documented use of this API) only a synchronous mode, with
    // an optional "async notifications later if the option changes" mode.
    // Even IE9, which we hope to emulate as IE is the most widely deployed
    // previous implementation of the DHCP aspect of WPAD and the only one
    // on Windows (Konqueror is the other, on Linux), uses this API with the
    // synchronous flag.  There seem to be several Microsoft Knowledge Base
    // articles about calls to this function failing when other flags are used
    // (e.g. http://support.microsoft.com/kb/885270) so we won't take any
    // chances on non-standard, poorly documented usage.
    res = ::DhcpRequestParams(DHCPCAPI_REQUEST_SYNCHRONOUS,
                              NULL,
                              const_cast<LPWSTR>(adapter_name_wide.c_str()),
                              NULL,
                              send_params, request_params,
                              result_buffer.get(), &result_buffer_size,
                              NULL);
    ++retry_count;
  } while (res == ERROR_MORE_DATA && retry_count <= 3);

  if (res != NO_ERROR) {
    VLOG(1) << "Error fetching PAC URL from DHCP: " << res;
  } else if (wpad_params.nBytesData) {
    return SanitizeDhcpApiString(
        reinterpret_cast<const char*>(wpad_params.Data),
        wpad_params.nBytesData);
  }

  return "";
}

// static
std::string DhcpProxyScriptAdapterFetcher::SanitizeDhcpApiString(
    const char* data, size_t count_bytes) {
  // The result should be ASCII, not wide character.  Some DHCP
  // servers appear to count the trailing NULL in nBytesData, others
  // do not.  A few (we've had one report, http://crbug.com/297810)
  // do not NULL-terminate but may \n-terminate.
  //
  // Belt and suspenders and elastic waistband: First, ensure we
  // NULL-terminate after nBytesData; this is the inner constructor
  // with nBytesData as a parameter.  Then, return only up to the
  // first null in case of embedded NULLs; this is the outer
  // constructor that takes the result of c_str() on the inner.  If
  // the server is giving us back a buffer with embedded NULLs,
  // something is broken anyway.  Finally, trim trailing whitespace.
  std::string result(std::string(data, count_bytes).c_str());
  base::TrimWhitespaceASCII(result, base::TRIM_TRAILING, &result);
  return result;
}

}  // namespace net
