// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NETWORK_ERROR_LOGGING_NETWORK_ERROR_LOGGING_SERVICE_H_
#define NET_NETWORK_ERROR_LOGGING_NETWORK_ERROR_LOGGING_SERVICE_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include "base/feature_list.h"
#include "base/macros.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace base {
class Value;
}  // namespace base

namespace net {
class ReportingService;
}  // namespace net

namespace url {
class Origin;
}  // namespace url

namespace features {
extern const base::Feature NET_EXPORT kNetworkErrorLogging;
}  // namespace features

namespace net {

class NetworkErrorLoggingDelegate;

class NET_EXPORT NetworkErrorLoggingService {
 public:
  // The details of a network error that are included in an NEL report.
  //
  // See http://wicg.github.io/network-error-logging/#dfn-network-error-object
  // for details on the semantics of each field.
  struct NET_EXPORT RequestDetails {
    RequestDetails();
    RequestDetails(const RequestDetails& other);
    ~RequestDetails();

    GURL uri;
    GURL referrer;
    std::string user_agent;
    IPAddress server_ip;
    std::string protocol;
    std::string method;
    int status_code;
    base::TimeDelta elapsed_time;
    Error type;

    // Upload nesting depth of this request.
    //
    // If the request is not a Reporting upload, the depth is 0.
    //
    // If the request is a Reporting upload, the depth is the max of the depth
    // of the requests reported within it plus 1. (Non-NEL reports are
    // considered to have depth 0.)
    int reporting_upload_depth;
  };

  static const char kHeaderName[];

  static const char kReportType[];

  static const int kMaxNestedReportDepth;

  // Keys for data included in report bodies. Exposed for tests.

  static const char kReferrerKey[];
  static const char kSamplingFractionKey[];
  static const char kServerIpKey[];
  static const char kProtocolKey[];
  static const char kMethodKey[];
  static const char kStatusCodeKey[];
  static const char kElapsedTimeKey[];
  static const char kPhaseKey[];
  static const char kTypeKey[];

  // Histograms.  These are mainly used in test cases to verify that interesting
  // events occurred.

  static const char kHeaderOutcomeHistogram[];
  static const char kRequestOutcomeHistogram[];

  enum class HeaderOutcome {
    DISCARDED_NO_NETWORK_ERROR_LOGGING_SERVICE = 0,
    DISCARDED_INVALID_SSL_INFO = 1,
    DISCARDED_CERT_STATUS_ERROR = 2,

    DISCARDED_INSECURE_ORIGIN = 3,

    DISCARDED_JSON_TOO_BIG = 4,
    DISCARDED_JSON_INVALID = 5,
    DISCARDED_NOT_DICTIONARY = 6,
    DISCARDED_TTL_MISSING = 7,
    DISCARDED_TTL_NOT_INTEGER = 8,
    DISCARDED_TTL_NEGATIVE = 9,
    DISCARDED_REPORT_TO_MISSING = 10,
    DISCARDED_REPORT_TO_NOT_STRING = 11,

    REMOVED = 12,
    SET = 13,

    DISCARDED_MISSING_REMOTE_ENDPOINT = 14,

    MAX
  };

  enum class RequestOutcome {
    DISCARDED_NO_NETWORK_ERROR_LOGGING_SERVICE = 0,

    DISCARDED_NO_REPORTING_SERVICE = 1,
    DISCARDED_INSECURE_ORIGIN = 2,
    DISCARDED_NO_ORIGIN_POLICY = 3,
    DISCARDED_UNMAPPED_ERROR = 4,
    DISCARDED_REPORTING_UPLOAD = 5,
    DISCARDED_UNSAMPLED_SUCCESS = 6,
    DISCARDED_UNSAMPLED_FAILURE = 7,
    QUEUED = 8,
    DISCARDED_NON_DNS_SUBDOMAIN_REPORT = 9,

    MAX
  };

  static void RecordHeaderDiscardedForNoNetworkErrorLoggingService();
  static void RecordHeaderDiscardedForInvalidSSLInfo();
  static void RecordHeaderDiscardedForCertStatusError();
  static void RecordHeaderDiscardedForMissingRemoteEndpoint();

  static void RecordRequestDiscardedForNoNetworkErrorLoggingService();
  static void RecordRequestDiscardedForInsecureOrigin();

  static std::unique_ptr<NetworkErrorLoggingService> Create(
      std::unique_ptr<NetworkErrorLoggingDelegate> delegate);

  virtual ~NetworkErrorLoggingService();

  // Ingests a "NEL:" header received for |origin| from |received_ip_address|
  // with normalized value |value|. May or may not actually set a policy for
  // that origin.
  virtual void OnHeader(const url::Origin& origin,
                        const IPAddress& received_ip_address,
                        const std::string& value) = 0;

  // Considers queueing a network error report for the request described in
  // |details|.  The contents of |details| might be changed, depending on the
  // NEL policy associated with the request's origin.  Note that |details| is
  // passed by value, so that it doesn't need to be copied in this function if
  // it needs to be changed.  Consider using std::move to pass this parameter if
  // the caller doesn't need to access it after this method call.
  //
  // Note that Network Error Logging can report a fraction of successful
  // requests as well (to calculate error rates), so this should be called on
  // *all* secure requests. NEL is only available to secure origins, so this is
  // not called on any insecure requests.
  virtual void OnRequest(RequestDetails details) = 0;

  // Removes browsing data (origin policies) associated with any origin for
  // which |origin_filter| returns true.
  virtual void RemoveBrowsingData(
      const base::RepeatingCallback<bool(const GURL&)>& origin_filter) = 0;

  // Removes browsing data (origin policies) for all origins. Allows slight
  // optimization over passing an always-true filter to RemoveBrowsingData.
  virtual void RemoveAllBrowsingData() = 0;

  // Sets the ReportingService that will be used to queue network error reports.
  // If |nullptr| is passed, reports will be queued locally or discarded.
  // |reporting_service| must outlive the NetworkErrorLoggingService.
  void SetReportingService(ReportingService* reporting_service);

  // Sets a base::TickClock (used to track policy expiration) for tests.
  // |tick_clock| must outlive the NetworkErrorLoggingService, and cannot be
  // nullptr.
  void SetTickClockForTesting(const base::TickClock* tick_clock);

  virtual base::Value StatusAsValue() const;

  virtual std::set<url::Origin> GetPolicyOriginsForTesting();

 protected:
  NetworkErrorLoggingService();

  // Unowned:
  const base::TickClock* tick_clock_;
  ReportingService* reporting_service_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkErrorLoggingService);
};

}  // namespace net

#endif  // NET_NETWORK_ERROR_LOGGING_NETWORK_ERROR_LOGGING_SERVICE_H_
