// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_INTERNAL_PATH_BUILDER_H_
#define NET_CERT_INTERNAL_PATH_BUILDER_H_

#include <memory>
#include <string>
#include <vector>

#include "net/base/net_export.h"
#include "net/cert/internal/cert_errors.h"
#include "net/cert/internal/parsed_certificate.h"
#include "net/cert/internal/trust_store.h"
#include "net/cert/internal/verify_certificate_chain.h"
#include "net/der/input.h"
#include "net/der/parse_values.h"

namespace net {

namespace der {
struct GeneralizedTime;
}

class CertPathIter;
class CertIssuerSource;

// Base class for custom data that CertPathBuilderDelegate can attach to paths.
class NET_EXPORT CertPathBuilderDelegateData {
 public:
  virtual ~CertPathBuilderDelegateData() {}
};

// Represents a single candidate path that was built or is being processed.
//
// This is used both to represent valid paths, as well as invalid/partial ones.
//
// Consumers must use |IsValid()| to test whether the
// CertPathBuilderResultPath is the result of a successful certificate
// verification.
struct NET_EXPORT CertPathBuilderResultPath {
  CertPathBuilderResultPath();
  ~CertPathBuilderResultPath();

  // Returns true if the candidate path is valid. A "valid" path is one which
  // chains to a trusted root, and did not have any high severity errors added
  // to it during certificate verification.
  bool IsValid() const;

  // Returns the chain's root certificate or nullptr if the chain doesn't
  // chain to a trust anchor.
  const ParsedCertificate* GetTrustedCert() const;

  // Path in the forward direction:
  //
  //   certs[0] is the target certificate
  //   certs[i] was issued by certs[i+1]
  //   certs.back() is the root certificate (which may or may not be trusted).
  ParsedCertificateList certs;

  // Describes the trustedness of the final certificate in the chain,
  // |certs.back()|
  //
  // For result paths where |IsValid()|, the final certificate is trusted.
  // However for failed or partially constructed paths the final certificate may
  // not be a trust anchor.
  CertificateTrust last_cert_trust;

  // The set of policies that the certificate is valid for (of the
  // subset of policies user requested during verification).
  std::set<der::Input> user_constrained_policy_set;

  // Slot for per-path data that may set by CertPathBuilderDelegate. The
  // specific type is chosen by the delegate. Can be nullptr when unused.
  std::unique_ptr<CertPathBuilderDelegateData> delegate_data;

  // The set of errors and warnings associated with this path (bucketed
  // per-certificate). Note that consumers should always use |IsValid()| to
  // determine validity of the CertPathBuilderResultPath, and not just inspect
  // |errors|.
  CertPathErrors errors;
};

// CertPathBuilderDelegate controls policies for certificate verification and
// path building.
class NET_EXPORT CertPathBuilderDelegate
    : public VerifyCertificateChainDelegate {
 public:
  // This is called during path building on candidate paths which have already
  // been run through RFC 5280 verification. |path| may already have errors
  // and warnings set on it. Delegates can "reject" a candidate path from path
  // building by adding high severity errors.
  virtual void CheckPathAfterVerification(CertPathBuilderResultPath* path) = 0;
};

// Checks whether a certificate is trusted by building candidate paths to trust
// anchors and verifying those paths according to RFC 5280. Each instance of
// CertPathBuilder is used for a single verification.
//
// WARNING: This implementation is currently experimental.  Consult an OWNER
// before using it.
class NET_EXPORT CertPathBuilder {
 public:
  // Provides the overall result of path building. This includes the paths that
  // were attempted.
  struct NET_EXPORT Result {
    Result();
    ~Result();

    // Returns true if there was a valid path.
    bool HasValidPath() const;

    // Returns the CertPathBuilderResultPath for the best valid path, or nullptr
    // if there was none.
    const CertPathBuilderResultPath* GetBestValidPath() const;

    // Resets to the initial value.
    void Clear();

    // List of paths that were attempted and the result for each.
    std::vector<std::unique_ptr<CertPathBuilderResultPath>> paths;

    // Index into |paths|. Before use, |paths.empty()| must be checked.
    // NOTE: currently the definition of "best" is fairly limited. Valid is
    // better than invalid, but otherwise nothing is guaranteed.
    size_t best_result_index = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Result);
  };

  // Creates a CertPathBuilder that attempts to find a path from |cert| to a
  // trust anchor in |trust_store| and is valid at |time|. Details of attempted
  // path(s) are stored in |*result|.
  //
  // The caller must keep |trust_store|, |delegate| and |*result| valid for the
  // lifetime of the CertPathBuilder.
  //
  // See VerifyCertificateChain() for a more detailed explanation of the
  // same-named parameters not defined below.
  //
  // * |result|: Storage for the result of path building.
  // * |delegate|: Must be non-null. The delegate is called at various points in
  //               path building to verify specific parts of certificates or the
  //               final chain. See CertPathBuilderDelegate and
  //               VerifyCertificateChainDelegate for more information.
  CertPathBuilder(scoped_refptr<ParsedCertificate> cert,
                  TrustStore* trust_store,
                  CertPathBuilderDelegate* delegate,
                  const der::GeneralizedTime& time,
                  KeyPurpose key_purpose,
                  InitialExplicitPolicy initial_explicit_policy,
                  const std::set<der::Input>& user_initial_policy_set,
                  InitialPolicyMappingInhibit initial_policy_mapping_inhibit,
                  InitialAnyPolicyInhibit initial_any_policy_inhibit,
                  Result* result);
  ~CertPathBuilder();

  // Adds a CertIssuerSource to provide intermediates for use in path building.
  // Multiple sources may be added. Must not be called after Run is called.
  // The |*cert_issuer_source| must remain valid for the lifetime of the
  // CertPathBuilder.
  //
  // (If no issuer sources are added, the target certificate will only verify if
  // it is a trust anchor or is directly signed by a trust anchor.)
  void AddCertIssuerSource(CertIssuerSource* cert_issuer_source);

  // Executes verification of the target certificate.
  //
  // Upon return results are written to the |result| object passed into the
  // constructor. Run must not be called more than once on each CertPathBuilder
  // instance.
  void Run();

 private:
  void AddResultPath(std::unique_ptr<CertPathBuilderResultPath> result_path);

  std::unique_ptr<CertPathIter> cert_path_iter_;
  CertPathBuilderDelegate* delegate_;
  const der::GeneralizedTime time_;
  const KeyPurpose key_purpose_;
  const InitialExplicitPolicy initial_explicit_policy_;
  const std::set<der::Input> user_initial_policy_set_;
  const InitialPolicyMappingInhibit initial_policy_mapping_inhibit_;
  const InitialAnyPolicyInhibit initial_any_policy_inhibit_;

  Result* out_result_;

  DISALLOW_COPY_AND_ASSIGN(CertPathBuilder);
};

}  // namespace net

#endif  // NET_CERT_INTERNAL_PATH_BUILDER_H_
