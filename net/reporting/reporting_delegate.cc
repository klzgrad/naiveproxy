// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_delegate.h"

#include "base/json/json_reader.h"
#include "net/base/network_delegate.h"
#include "net/url_request/url_request_context.h"

namespace net {

namespace {

const int kMaxJsonSize = 16 * 1024;
const int kMaxJsonDepth = 5;

class ReportingDelegateImpl : public ReportingDelegate {
 public:
  ReportingDelegateImpl(URLRequestContext* request_context)
      : request_context_(request_context) {
    DCHECK(request_context);
  }

  ~ReportingDelegateImpl() override = default;

  bool CanQueueReport(const url::Origin& origin) const override {
    return network_delegate() &&
           network_delegate()->CanQueueReportingReport(origin);
  }

  void CanSendReports(std::set<url::Origin> origins,
                      base::OnceCallback<void(std::set<url::Origin>)>
                          result_callback) const override {
    if (!network_delegate()) {
      origins.clear();
      std::move(result_callback).Run(std::move(origins));
      return;
    }
    network_delegate()->CanSendReportingReports(std::move(origins),
                                                std::move(result_callback));
  }

  bool CanSetClient(const url::Origin& origin,
                    const GURL& endpoint) const override {
    return network_delegate() &&
           network_delegate()->CanSetReportingClient(origin, endpoint);
  }

  bool CanUseClient(const url::Origin& origin,
                    const GURL& endpoint) const override {
    return network_delegate() &&
           network_delegate()->CanUseReportingClient(origin, endpoint);
  }

  void ParseJson(const std::string& unsafe_json,
                 const JsonSuccessCallback& success_callback,
                 const JsonFailureCallback& failure_callback) const override {
    if (unsafe_json.size() > kMaxJsonSize) {
      failure_callback.Run();
      return;
    }

    std::unique_ptr<base::Value> value = base::JSONReader::Read(
        unsafe_json, base::JSON_PARSE_RFC, kMaxJsonDepth);
    if (value)
      success_callback.Run(std::move(value));
    else
      failure_callback.Run();
  }

 private:
  const NetworkDelegate* network_delegate() const {
    return request_context_->network_delegate();
  }

  URLRequestContext* request_context_;
};

}  // namespace

// static
std::unique_ptr<ReportingDelegate> ReportingDelegate::Create(
    URLRequestContext* request_context) {
  return std::make_unique<ReportingDelegateImpl>(request_context);
}

ReportingDelegate::~ReportingDelegate() = default;

}  // namespace net
