// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_DELEGATE_H_
#define NET_REPORTING_REPORTING_DELEGATE_H_

#include <memory>
#include <set>

#include "base/callback.h"
#include "base/macros.h"
#include "base/values.h"
#include "net/base/net_export.h"

class GURL;

namespace url {
class Origin;
}  // namespace url

namespace net {

class URLRequestContext;

class NET_EXPORT ReportingDelegate {
 public:
  virtual ~ReportingDelegate();

  // Checks whether |origin| is allowed to queue reports for future delivery.
  virtual bool CanQueueReport(const url::Origin& origin) const = 0;

  // Checks whether |origins| are allowed to receive reports, either in real
  // time or that were queued earlier, removing any that aren't.
  virtual void CanSendReports(std::set<url::Origin> origins,
                              base::OnceCallback<void(std::set<url::Origin>)>
                                  result_callback) const = 0;

  // Checks whether |origin| can set |endpoint| to be used for future report
  // deliveries.
  virtual bool CanSetClient(const url::Origin& origin,
                            const GURL& endpoint) const = 0;

  // Checks whether |origin| can use |endpoint| for a report delivery right now.
  virtual bool CanUseClient(const url::Origin& origin,
                            const GURL& endpoint) const = 0;

  // TODO(crbug.com/811485): Use OnceCallback/Closure.
  using JsonSuccessCallback =
      base::RepeatingCallback<void(std::unique_ptr<base::Value>)>;
  using JsonFailureCallback = base::RepeatingClosure;

  // Parses JSON. How safely, and using what mechanism, is up to the embedder,
  // but //components/data_decoder is recommended if available.
  //
  // Exactly one callback should be made, either to |success_callback| (with the
  // parsed value) if parsing succeeded or to |failure_callback| if parsing
  // failed. The callbacks may be called either synchronously or
  // asynchronously.
  virtual void ParseJson(const std::string& unsafe_json,
                         const JsonSuccessCallback& success_callback,
                         const JsonFailureCallback& failure_callback) const = 0;

  static std::unique_ptr<ReportingDelegate> Create(
      URLRequestContext* request_context);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_DELEGATE_H_
