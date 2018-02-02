// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_CLIENT_H_
#define NET_REPORTING_REPORTING_CLIENT_H_

#include <string>

#include "base/macros.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {

// The configuration by an origin to use an endpoint for report delivery.
struct NET_EXPORT ReportingClient {
 public:
  enum class Subdomains { EXCLUDE = 0, INCLUDE = 1 };

  ReportingClient(const url::Origin& origin,
                  const GURL& endpoint,
                  Subdomains subdomains,
                  const std::string& group,
                  base::TimeTicks expires);
  ~ReportingClient();

  // The origin from which reports will be delivered.
  url::Origin origin;

  // The endpoint to which reports may be delivered. (Origins may configure
  // many.)
  GURL endpoint;

  // Whether subdomains of the host of |origin| should also be handled by this
  // client.
  Subdomains subdomains = Subdomains::EXCLUDE;

  // The endpoint group to which this client belongs.
  std::string group = "default";

  // When this client's max-age has expired.
  base::TimeTicks expires;

 private:
  DISALLOW_COPY_AND_ASSIGN(ReportingClient);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_CLIENT_H_
