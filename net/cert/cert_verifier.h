// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CERT_VERIFIER_H_
#define NET_CERT_CERT_VERIFIER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/completion_callback.h"
#include "net/base/hash_value.h"
#include "net/base/net_export.h"
#include "net/cert/x509_certificate.h"

namespace net {

class CertVerifyResult;
class CRLSet;
class NetLogWithSource;

// CertVerifier represents a service for verifying certificates.
//
// CertVerifiers can handle multiple requests at a time.
class NET_EXPORT CertVerifier {
 public:
  class Request {
   public:
    Request() {}

    // Destruction of the Request cancels it.
    virtual ~Request() {}

   private:
    DISALLOW_COPY_AND_ASSIGN(Request);
  };

  enum VerifyFlags {
    // If set, enables online revocation checking via CRLs and OCSP for the
    // certificate chain.
    VERIFY_REV_CHECKING_ENABLED = 1 << 0,

    // If set, and the certificate being verified may be an EV certificate,
    // attempt to verify the certificate according to the EV processing
    // guidelines. In order to successfully verify a certificate as EV,
    // either an online or offline revocation check must be successfully
    // completed. To ensure it's possible to complete a revocation check,
    // callers should also specify either VERIFY_REV_CHECKING_ENABLED or
    // VERIFY_REV_CHECKING_ENABLED_EV_ONLY (to enable online checks), and
    // VERIFY_CERT_IO_ENABLED (to enable network fetches for online checks).
    VERIFY_EV_CERT = 1 << 1,

    // If set, permits NSS to use the network when verifying certificates,
    // such as to fetch missing intermediates or to check OCSP or CRLs.
    // TODO(rsleevi): http://crbug.com/143300 - Define this flag for all
    // verification engines with well-defined semantics, rather than being
    // NSS only.
    VERIFY_CERT_IO_ENABLED = 1 << 2,

    // If set, enables online revocation checking via CRLs or OCSP when the
    // chain is not covered by a fresh CRLSet, but only for certificates which
    // may be EV, and only when VERIFY_EV_CERT is also set.
    VERIFY_REV_CHECKING_ENABLED_EV_ONLY = 1 << 3,

    // If set, this is equivalent to VERIFY_REV_CHECKING_ENABLED, in that it
    // enables online revocation checking via CRLs or OCSP, but only
    // for certificates issued by non-public trust anchors. Failure to check
    // revocation is treated as a hard failure.
    // Note: If VERIFY_CERT_IO_ENABLE is not also supplied, certificates
    // that chain to local trust anchors will likely fail - for example, due to
    // lacking fresh cached revocation issue (Windows) or because OCSP stapling
    // can only provide information for the leaf, and not for any
    // intermediates.
    VERIFY_REV_CHECKING_REQUIRED_LOCAL_ANCHORS = 1 << 4,

    // If set, certificates with SHA-1 signatures will be allowed, but only if
    // they are issued by non-public trust anchors.
    VERIFY_ENABLE_SHA1_LOCAL_ANCHORS = 1 << 5,

    // If set, certificates which lack a subjectAltName will be allowed to
    // match against the commonName of the certificate, but only if they are
    // issued by non-public trust anchors.
    VERIFY_ENABLE_COMMON_NAME_FALLBACK_LOCAL_ANCHORS = 1 << 6,
  };

  // Parameters to verify |certificate| against the supplied
  // |hostname| as an SSL server.
  //
  // |hostname| should be a canonicalized hostname (in A-Label form) or IP
  // address in string form, following the rules of a URL host portion. In
  // the case of |hostname| being a domain name, it may contain a trailing
  // dot (e.g. "example.com."), as used to signal to DNS not to perform
  // suffix search, and it will safely be ignored. If |hostname| is an IPv6
  // address, it MUST be in URL form - that is, surrounded in square
  // brackets, such as "[::1]".
  //
  // |flags| is a bitwise OR of VerifyFlags.
  //
  // |ocsp_response| is optional, but if non-empty, should contain an OCSP
  // response obtained via OCSP stapling. It may be ignored by the
  // CertVerifier.
  //
  // |additional_trust_anchors| is optional, but if non-empty, should contain
  // additional certificates to be treated as trust anchors. It may be ignored
  // by the CertVerifier.
  class NET_EXPORT RequestParams {
   public:
    RequestParams(scoped_refptr<X509Certificate> certificate,
                  const std::string& hostname,
                  int flags,
                  const std::string& ocsp_response,
                  CertificateList additional_trust_anchors);
    RequestParams(const RequestParams& other);
    ~RequestParams();

    const scoped_refptr<X509Certificate>& certificate() const {
      return certificate_;
    }
    const std::string& hostname() const { return hostname_; }
    int flags() const { return flags_; }
    const std::string& ocsp_response() const { return ocsp_response_; }
    const CertificateList& additional_trust_anchors() const {
      return additional_trust_anchors_;
    }

    bool operator==(const RequestParams& other) const;
    bool operator<(const RequestParams& other) const;

   private:
    scoped_refptr<X509Certificate> certificate_;
    std::string hostname_;
    int flags_;
    std::string ocsp_response_;
    CertificateList additional_trust_anchors_;

    // Used to optimize sorting/indexing comparisons.
    std::string key_;
  };

  // When the verifier is destroyed, all certificate verification requests are
  // canceled, and their completion callbacks will not be called.
  virtual ~CertVerifier() {}

  // Verifies the given certificate against the given hostname as an SSL server.
  // Returns OK if successful or an error code upon failure.
  //
  // The |*verify_result| structure, including the |verify_result->cert_status|
  // bitmask, is always filled out regardless of the return value.  If the
  // certificate has multiple errors, the corresponding status flags are set in
  // |verify_result->cert_status|, and the error code for the most serious
  // error is returned.
  //
  // |crl_set| points to an optional CRLSet structure which can be used to
  // avoid revocation checks over the network.
  //
  // |callback| must not be null.  ERR_IO_PENDING is returned if the operation
  // could not be completed synchronously, in which case the result code will
  // be passed to the callback when available.
  //
  // On asynchronous completion (when Verify returns ERR_IO_PENDING) |out_req|
  // will be reset with a pointer to the request. Freeing this pointer before
  // the request has completed will cancel it.
  //
  // If Verify() completes synchronously then |out_req| *may* be reset to
  // nullptr. However it is not guaranteed that all implementations will reset
  // it in this case.
  virtual int Verify(const RequestParams& params,
                     CRLSet* crl_set,
                     CertVerifyResult* verify_result,
                     const CompletionCallback& callback,
                     std::unique_ptr<Request>* out_req,
                     const NetLogWithSource& net_log) = 0;

  // Returns true if this CertVerifier supports stapled OCSP responses.
  virtual bool SupportsOCSPStapling();

  // Creates a CertVerifier implementation that verifies certificates using
  // the preferred underlying cryptographic libraries.
  static std::unique_ptr<CertVerifier> CreateDefault();
};

}  // namespace net

#endif  // NET_CERT_CERT_VERIFIER_H_
