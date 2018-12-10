// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_HEADER_PARSER_H_
#define NET_REPORTING_REPORTING_HEADER_PARSER_H_

#include <memory>

#include "base/macros.h"
#include "net/base/net_export.h"

class GURL;

namespace base {
class Value;
}  // namespace base

namespace net {

class ReportingContext;

class NET_EXPORT ReportingHeaderParser {
 public:
  static void RecordHeaderDiscardedForNoReportingService();
  static void RecordHeaderDiscardedForInvalidSSLInfo();
  static void RecordHeaderDiscardedForCertStatusError();
  static void RecordHeaderDiscardedForJsonInvalid();
  static void RecordHeaderDiscardedForJsonTooBig();

  static void ParseHeader(ReportingContext* context,
                          const GURL& url,
                          std::unique_ptr<base::Value> value);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ReportingHeaderParser);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_HEADER_PARSER_H_
