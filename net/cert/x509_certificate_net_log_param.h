// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_X509_CERTIFICATE_NET_LOG_PARAM_H_
#define NET_CERT_X509_CERTIFICATE_NET_LOG_PARAM_H_

#include <memory>

namespace base {
class Value;
}

namespace net {

class NetLogCaptureMode;
class X509Certificate;

// Creates NetLog parameter to describe an X509Certificate.
std::unique_ptr<base::Value> NetLogX509CertificateCallback(
    const X509Certificate* certificate,
    NetLogCaptureMode capture_mode);

}  // namespace net

#endif  // NET_CERT_X509_CERTIFICATE_NET_LOG_PARAM_H_
