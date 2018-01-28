// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CT_SIGNED_CERTIFICATE_TIMESTAMP_LOG_PARAM_H_
#define NET_CERT_CT_SIGNED_CERTIFICATE_TIMESTAMP_LOG_PARAM_H_

#include <memory>

#include "base/strings/string_piece.h"
#include "net/cert/signed_certificate_timestamp_and_status.h"

namespace base {
class Value;
}

namespace net {

class NetLogCaptureMode;

// Creates a dictionary of processed Signed Certificate Timestamps to be
// logged in the NetLog.
// See the documentation for SIGNED_CERTIFICATE_TIMESTAMPS_CHECKED
// in net/log/net_log_event_type_list.h
std::unique_ptr<base::Value> NetLogSignedCertificateTimestampCallback(
    const SignedCertificateTimestampAndStatusList* scts,
    NetLogCaptureMode capture_mode);

// Creates a dictionary of raw Signed Certificate Timestamps to be logged
// in the NetLog.
// See the documentation for SIGNED_CERTIFICATE_TIMESTAMPS_RECEIVED
// in net/log/net_log_event_type_list.h
std::unique_ptr<base::Value> NetLogRawSignedCertificateTimestampCallback(
    base::StringPiece embedded_scts,
    base::StringPiece sct_list_from_ocsp,
    base::StringPiece sct_list_from_tls_extension,
    NetLogCaptureMode capture_mode);

}  // namespace net

#endif  // NET_CERT_CT_SIGNED_CERTIFICATE_TIMESTAMP_LOG_PARAM_H_
