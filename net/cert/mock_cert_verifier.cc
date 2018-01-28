// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/mock_cert_verifier.h"

#include <memory>
#include <utility>

#include "base/memory/ref_counted.h"
#include "base/strings/pattern.h"
#include "base/strings/string_util.h"
#include "net/base/net_errors.h"
#include "net/cert/cert_status_flags.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/x509_certificate.h"

namespace net {

struct MockCertVerifier::Rule {
  Rule(scoped_refptr<X509Certificate> cert_arg,
       const std::string& hostname_arg,
       const CertVerifyResult& result_arg,
       int rv_arg)
      : cert(std::move(cert_arg)),
        hostname(hostname_arg),
        result(result_arg),
        rv(rv_arg) {
    DCHECK(cert);
    DCHECK(result.verified_cert);
  }

  scoped_refptr<X509Certificate> cert;
  std::string hostname;
  CertVerifyResult result;
  int rv;
};

MockCertVerifier::MockCertVerifier() : default_result_(ERR_CERT_INVALID) {}

MockCertVerifier::~MockCertVerifier() = default;

int MockCertVerifier::Verify(const RequestParams& params,
                             CRLSet* crl_set,
                             CertVerifyResult* verify_result,
                             const CompletionCallback& callback,
                             std::unique_ptr<Request>* out_req,
                             const NetLogWithSource& net_log) {
  RuleList::const_iterator it;
  for (it = rules_.begin(); it != rules_.end(); ++it) {
    // Check just the server cert. Intermediates will be ignored.
    if (!it->cert->Equals(params.certificate().get()))
      continue;
    if (!base::MatchPattern(params.hostname(), it->hostname))
      continue;
    *verify_result = it->result;
    return it->rv;
  }

  // Fall through to the default.
  verify_result->verified_cert = params.certificate();
  verify_result->cert_status = MapNetErrorToCertStatus(default_result_);
  return default_result_;
}

void MockCertVerifier::AddResultForCert(scoped_refptr<X509Certificate> cert,
                                        const CertVerifyResult& verify_result,
                                        int rv) {
  AddResultForCertAndHost(std::move(cert), "*", verify_result, rv);
}

void MockCertVerifier::AddResultForCertAndHost(
    scoped_refptr<X509Certificate> cert,
    const std::string& host_pattern,
    const CertVerifyResult& verify_result,
    int rv) {
  rules_.push_back(Rule(std::move(cert), host_pattern, verify_result, rv));
}

}  // namespace net
