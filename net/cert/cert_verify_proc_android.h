// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CERT_VERIFY_PROC_ANDROID_H_
#define NET_CERT_CERT_VERIFY_PROC_ANDROID_H_

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/cert/cert_verify_proc.h"

namespace net {

// Performs certificate verification on Android by calling the platform
// TrustManager through JNI.
class NET_EXPORT CertVerifyProcAndroid : public CertVerifyProc {
 public:
  CertVerifyProcAndroid();

  bool SupportsAdditionalTrustAnchors() const override;
  bool SupportsOCSPStapling() const override;

 protected:
  ~CertVerifyProcAndroid() override;

 private:
  int VerifyInternal(X509Certificate* cert,
                     const std::string& hostname,
                     const std::string& ocsp_response,
                     int flags,
                     CRLSet* crl_set,
                     const CertificateList& additional_trust_anchors,
                     CertVerifyResult* verify_result) override;

  DISALLOW_COPY_AND_ASSIGN(CertVerifyProcAndroid);
};

}  // namespace net

#endif  // NET_CERT_CERT_VERIFY_PROC_ANDROID_H_
