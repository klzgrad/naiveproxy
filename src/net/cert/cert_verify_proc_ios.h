// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CERT_VERIFY_PROC_IOS_H_
#define NET_CERT_CERT_VERIFY_PROC_IOS_H_

#include "net/cert/cert_verify_proc.h"

#include <Security/Security.h>

#include "net/cert/cert_status_flags.h"

namespace net {

class CRLSet;

// Performs certificate path construction and validation using iOS's
// Security.framework.
class CertVerifyProcIOS : public CertVerifyProc {
 public:
  explicit CertVerifyProcIOS(scoped_refptr<CRLSet> crl_set);

  // Maps a CFError result from SecTrustEvaluateWithError to CertStatus flags.
  // This should only be called if the SecTrustEvaluateWithError return value
  // indicated that the certificate is not trusted.
  static CertStatus GetCertFailureStatusFromError(CFErrorRef error);

 protected:
  ~CertVerifyProcIOS() override;

 private:
  int VerifyInternal(X509Certificate* cert,
                     const std::string& hostname,
                     const std::string& ocsp_response,
                     const std::string& sct_list,
                     int flags,
                     CertVerifyResult* verify_result,
                     const NetLogWithSource& net_log) override;
};

}  // namespace net

#endif  // NET_CERT_CERT_VERIFY_PROC_IOS_H_
