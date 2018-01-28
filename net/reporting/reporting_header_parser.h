// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_HEADER_PARSER_H_
#define NET_REPORTING_REPORTING_HEADER_PARSER_H_

#include <string>

#include "base/macros.h"
#include "net/base/net_export.h"

class GURL;

namespace net {

class ReportingContext;

class NET_EXPORT ReportingHeaderParser {
 public:
  static void RecordHeaderDiscardedForNoReportingService();
  static void RecordHeaderDiscardedForInvalidSSLInfo();
  static void RecordHeaderDiscardedForCertStatusError();

  static void ParseHeader(ReportingContext* context,
                          const GURL& url,
                          const std::string& json_value);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ReportingHeaderParser);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_HEADER_PARSER_H_
