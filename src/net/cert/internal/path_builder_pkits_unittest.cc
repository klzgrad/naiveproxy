// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/path_builder.h"

#include "net/base/net_errors.h"
#include "net/cert/internal/cert_issuer_source_static.h"
#include "net/cert/internal/parse_certificate.h"
#include "net/cert/internal/parsed_certificate.h"
#include "net/cert/internal/simple_path_builder_delegate.h"
#include "net/cert/internal/trust_store_in_memory.h"
#include "net/cert/internal/verify_certificate_chain.h"
#include "net/der/input.h"
#include "third_party/boringssl/src/include/openssl/pool.h"


// TODO(mattm): these require CRL support:
#define Section7InvalidkeyUsageCriticalcRLSignFalseTest4 \
  DISABLED_Section7InvalidkeyUsageCriticalcRLSignFalseTest4
#define Section7InvalidkeyUsageNotCriticalcRLSignFalseTest5 \
  DISABLED_Section7InvalidkeyUsageNotCriticalcRLSignFalseTest5

#include "net/cert/internal/nist_pkits_unittest.h"

namespace net {

namespace {

class PathBuilderPkitsTestDelegate {
 public:
  static void RunTest(std::vector<std::string> cert_ders,
                      std::vector<std::string> crl_ders,
                      const PkitsTestInfo& info) {
    ASSERT_FALSE(cert_ders.empty());
    ParsedCertificateList certs;
    for (const std::string& der : cert_ders) {
      CertErrors errors;
      ASSERT_TRUE(ParsedCertificate::CreateAndAddToVector(
          bssl::UniquePtr<CRYPTO_BUFFER>(
              CRYPTO_BUFFER_new(reinterpret_cast<const uint8_t*>(der.data()),
                                der.size(), nullptr)),
          {}, &certs, &errors))
          << errors.ToDebugString();
    }
    // First entry in the PKITS chain is the trust anchor.
    // TODO(mattm): test with all possible trust anchors in the trust store?
    TrustStoreInMemory trust_store;

    trust_store.AddTrustAnchor(certs[0]);

    // TODO(mattm): test with other irrelevant certs in cert_issuer_sources?
    CertIssuerSourceStatic cert_issuer_source;
    for (size_t i = 1; i < cert_ders.size() - 1; ++i)
      cert_issuer_source.AddCert(certs[i]);

    scoped_refptr<ParsedCertificate> target_cert(certs.back());

    SimplePathBuilderDelegate path_builder_delegate(
        1024, SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1);

    CertPathBuilder::Result result;
    CertPathBuilder path_builder(
        std::move(target_cert), &trust_store, &path_builder_delegate, info.time,
        KeyPurpose::ANY_EKU, info.initial_explicit_policy,
        info.initial_policy_set, info.initial_policy_mapping_inhibit,
        info.initial_inhibit_any_policy, &result);
    path_builder.AddCertIssuerSource(&cert_issuer_source);

    path_builder.Run();

    ASSERT_EQ(info.should_validate, result.HasValidPath());

    if (result.HasValidPath()) {
      EXPECT_EQ(info.user_constrained_policy_set,
                result.GetBestValidPath()->user_constrained_policy_set);
    }
  }
};

}  // namespace

INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest01SignatureVerification,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest02ValidityPeriods,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest03VerifyingNameChaining,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest06VerifyingBasicConstraints,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest07KeyUsage,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest08CertificatePolicies,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest09RequireExplicitPolicy,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest10PolicyMappings,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest11InhibitPolicyMapping,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest12InhibitAnyPolicy,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest13NameConstraints,
                               PathBuilderPkitsTestDelegate);
INSTANTIATE_TYPED_TEST_SUITE_P(PathBuilder,
                               PkitsTest16PrivateCertificateExtensions,
                               PathBuilderPkitsTestDelegate);

// TODO(mattm): CRL support: PkitsTest04BasicCertificateRevocationTests,
// PkitsTest05VerifyingPathswithSelfIssuedCertificates,
// PkitsTest14DistributionPoints, PkitsTest15DeltaCRLs

}  // namespace net
