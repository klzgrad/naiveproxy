// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_ENDPOINT_MANAGER_H_
#define NET_REPORTING_REPORTING_ENDPOINT_MANAGER_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/base/rand_callback.h"
#include "net/reporting/reporting_context.h"

class GURL;

namespace url {
class Origin;
}  // namespace url

namespace net {

struct ReportingEndpoint;

// Keeps track of which endpoints are pending (have active delivery attempts to
// them) or in exponential backoff after one or more failures, and chooses an
// endpoint from an endpoint group to receive reports for an origin.
class NET_EXPORT ReportingEndpointManager {
 public:
  // |context| must outlive the ReportingEndpointManager.
  static std::unique_ptr<ReportingEndpointManager> Create(
      ReportingContext* context,
      const RandIntCallback& rand_callback);

  virtual ~ReportingEndpointManager();

  // Finds an endpoint that applies to deliveries to |group| for |origin| that
  // are not expired or in exponential backoff from failed requests.
  // The returned endpoint may have been configured by a superdomain of
  // |origin|. Deliberately chooses an endpoint randomly to ensure sites aren't
  // relying on any sort of fallback ordering.
  // If no suitable endpoint was found, returns an endpoint with is_valid()
  // false.
  virtual const ReportingEndpoint FindEndpointForDelivery(
      const url::Origin& origin,
      const std::string& group) = 0;

  // Informs the EndpointManager of a successful or unsuccessful request made to
  // |endpoint| so it can manage exponential backoff of failing endpoints.
  virtual void InformOfEndpointRequest(const GURL& endpoint,
                                       bool succeeded) = 0;
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_ENDPOINT_MANAGER_H_
