// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CT_KNOWN_LOGS_H_
#define NET_CERT_CT_KNOWN_LOGS_H_

#include <vector>

#include "base/memory/ref_counted.h"
#include "base/strings/string_piece.h"
#include "build/build_config.h"
#include "net/base/net_export.h"

namespace base {
class Time;
}  // namespace base

namespace net {

class CTLogVerifier;

namespace ct {

#if !defined(OS_NACL)
// CreateLogVerifiersForKnownLogs returns a vector of CT logs for all the known
// logs. This set includes logs that are presently qualified for inclusion and
// logs which were previously qualifying, but have since been disqualified. To
// determine the status of a given log, use |IsLogDisqualified()|.
NET_EXPORT std::vector<scoped_refptr<const CTLogVerifier>>
CreateLogVerifiersForKnownLogs();
#endif

// Returns true if the log identified by |log_id| (the SHA-256 hash of the
// log's DER-encoded SPKI) is operated by Google.
NET_EXPORT bool IsLogOperatedByGoogle(base::StringPiece log_id);

// Returns true if the log identified by |log_id| (the SHA-256 hash of the
// log's DER-encoded SPKI) has been disqualified, and sets
// |*disqualification_date| to the date of disqualification. Any SCTs that
// are embedded in certificates issued after |*disqualification_date| should
// not be trusted, nor contribute to any uniqueness or freshness
// requirements.
NET_EXPORT bool IsLogDisqualified(base::StringPiece log_id,
                                  base::Time* disqualification_date);

}  // namespace ct

}  // namespace net

#endif  // NET_CERT_CT_KNOWN_LOGS_H_
