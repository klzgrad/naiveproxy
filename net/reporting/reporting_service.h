// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_SERVICE_H_
#define NET_REPORTING_REPORTING_SERVICE_H_

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/macros.h"
#include "net/base/net_export.h"

class GURL;

namespace base {
class Value;
}  // namespace base

namespace net {

class ReportingContext;
struct ReportingPolicy;
class URLRequest;
class URLRequestContext;

// The external interface to the Reporting system, used by the embedder of //net
// and also other parts of //net.
class NET_EXPORT ReportingService {
 public:
  virtual ~ReportingService();

  // Creates a ReportingService. |policy| will be copied. |request_context| must
  // outlive the ReportingService.
  static std::unique_ptr<ReportingService> Create(
      const ReportingPolicy& policy,
      URLRequestContext* request_context);

  // Creates a ReportingService for testing purposes using an
  // already-constructed ReportingContext. The ReportingService will take
  // ownership of |reporting_context| and destroy it when the service is
  // destroyed.
  static std::unique_ptr<ReportingService> CreateForTesting(
      std::unique_ptr<ReportingContext> reporting_context);

  // Queues a report for delivery. |url| is the URL that originated the report.
  // |user_agent| is the User-Agent header that was used for the request.
  // |group| is the endpoint group to which the report should be delivered.
  // |type| is the type of the report. |body| is the body of the report.
  //
  // The Reporting system will take ownership of |body|; all other parameters
  // will be copied.
  virtual void QueueReport(const GURL& url,
                           const std::string& user_agent,
                           const std::string& group,
                           const std::string& type,
                           std::unique_ptr<const base::Value> body,
                           int depth) = 0;

  // Processes a Report-To header. |url| is the URL that originated the header;
  // |header_value| is the normalized value of the Report-To header.
  virtual void ProcessHeader(const GURL& url,
                             const std::string& header_value) = 0;

  // Removes browsing data from the Reporting system. See
  // ReportingBrowsingDataRemover for more details.
  virtual void RemoveBrowsingData(
      int data_type_mask,
      const base::RepeatingCallback<bool(const GURL&)>& origin_filter) = 0;

  // Like RemoveBrowsingData except removes data for all origins without a
  // filter.
  virtual void RemoveAllBrowsingData(int data_type_mask) = 0;

  // Checks how many uploads deep |request| is: 0 if it's not an upload, n+1 if
  // it's an upload reporting on requests of at most depth n.
  virtual int GetUploadDepth(const URLRequest& request) = 0;

  virtual const ReportingPolicy& GetPolicy() const = 0;

  virtual base::Value StatusAsValue() const;

 protected:
  ReportingService() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ReportingService);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_SERVICE_H_
