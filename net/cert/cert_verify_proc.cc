// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/cert_verify_proc.h"

#include <stdint.h>

#include <algorithm>

#include "base/metrics/histogram.h"
#include "base/metrics/histogram_macros.h"
#include "base/sha1.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "crypto/sha2.h"
#include "net/base/net_errors.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "net/cert/asn1_util.h"
#include "net/cert/cert_status_flags.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/crl_set.h"
#include "net/cert/internal/ocsp.h"
#include "net/cert/internal/signature_algorithm.h"
#include "net/cert/ocsp_revocation_status.h"
#include "net/cert/x509_certificate.h"
#include "net/der/encode_values.h"
#include "url/url_canon.h"

#if defined(USE_NSS_CERTS)
#include "net/cert/cert_verify_proc_nss.h"
#elif defined(OS_ANDROID)
#include "net/cert/cert_verify_proc_android.h"
#elif defined(OS_IOS)
#include "net/cert/cert_verify_proc_ios.h"
#elif defined(OS_MACOSX)
#include "net/cert/cert_verify_proc_mac.h"
#elif defined(OS_WIN)
#include "base/win/windows_version.h"
#include "net/cert/cert_verify_proc_win.h"
#elif defined(OS_FUCHSIA)
#include "net/cert/cert_verify_proc_builtin.h"
#else
#error Implement certificate verification.
#endif

namespace net {

namespace {

// Constants used to build histogram names
const char kLeafCert[] = "Leaf";
const char kIntermediateCert[] = "Intermediate";
const char kRootCert[] = "Root";
// Matches the order of X509Certificate::PublicKeyType
const char* const kCertTypeStrings[] = {
    "Unknown",
    "RSA",
    "DSA",
    "ECDSA",
    "DH",
    "ECDH"
};
// Histogram buckets for RSA/DSA/DH key sizes.
const int kRsaDsaKeySizes[] = {512, 768, 1024, 1536, 2048, 3072, 4096, 8192,
                               16384};
// Histogram buckets for ECDSA/ECDH key sizes. The list is based upon the FIPS
// 186-4 approved curves.
const int kEccKeySizes[] = {163, 192, 224, 233, 256, 283, 384, 409, 521, 571};

const char* CertTypeToString(int cert_type) {
  if (cert_type < 0 ||
      static_cast<size_t>(cert_type) >= arraysize(kCertTypeStrings)) {
    return "Unsupported";
  }
  return kCertTypeStrings[cert_type];
}

void RecordPublicKeyHistogram(const char* chain_position,
                              bool baseline_keysize_applies,
                              size_t size_bits,
                              X509Certificate::PublicKeyType cert_type) {
  std::string histogram_name =
      base::StringPrintf("CertificateType2.%s.%s.%s",
                         baseline_keysize_applies ? "BR" : "NonBR",
                         chain_position,
                         CertTypeToString(cert_type));
  // Do not use UMA_HISTOGRAM_... macros here, as it caches the Histogram
  // instance and thus only works if |histogram_name| is constant.
  base::HistogramBase* counter = NULL;

  // Histogram buckets are contingent upon the underlying algorithm being used.
  if (cert_type == X509Certificate::kPublicKeyTypeECDH ||
      cert_type == X509Certificate::kPublicKeyTypeECDSA) {
    // Typical key sizes match SECP/FIPS 186-3 recommendations for prime and
    // binary curves - which range from 163 bits to 571 bits.
    counter = base::CustomHistogram::FactoryGet(
        histogram_name,
        base::CustomHistogram::ArrayToCustomRanges(kEccKeySizes,
                                                   arraysize(kEccKeySizes)),
        base::HistogramBase::kUmaTargetedHistogramFlag);
  } else {
    // Key sizes < 1024 bits should cause errors, while key sizes > 16K are not
    // uniformly supported by the underlying cryptographic libraries.
    counter = base::CustomHistogram::FactoryGet(
        histogram_name,
        base::CustomHistogram::ArrayToCustomRanges(kRsaDsaKeySizes,
                                                   arraysize(kRsaDsaKeySizes)),
        base::HistogramBase::kUmaTargetedHistogramFlag);
  }
  counter->Add(size_bits);
}

// Returns true if |type| is |kPublicKeyTypeRSA| or |kPublicKeyTypeDSA|, and
// if |size_bits| is < 1024. Note that this means there may be false
// negatives: keys for other algorithms and which are weak will pass this
// test.
bool IsWeakKey(X509Certificate::PublicKeyType type, size_t size_bits) {
  switch (type) {
    case X509Certificate::kPublicKeyTypeRSA:
    case X509Certificate::kPublicKeyTypeDSA:
      return size_bits < 1024;
    default:
      return false;
  }
}

// Returns true if |cert| contains a known-weak key. Additionally, histograms
// the observed keys for future tightening of the definition of what
// constitutes a weak key.
bool ExaminePublicKeys(const scoped_refptr<X509Certificate>& cert,
                       bool should_histogram) {
  // The effective date of the CA/Browser Forum's Baseline Requirements -
  // 2012-07-01 00:00:00 UTC.
  const base::Time kBaselineEffectiveDate =
      base::Time::FromInternalValue(INT64_C(12985574400000000));
  // The effective date of the key size requirements from Appendix A, v1.1.5
  // 2014-01-01 00:00:00 UTC.
  const base::Time kBaselineKeysizeEffectiveDate =
      base::Time::FromInternalValue(INT64_C(13033008000000000));

  size_t size_bits = 0;
  X509Certificate::PublicKeyType type = X509Certificate::kPublicKeyTypeUnknown;
  bool weak_key = false;
  bool baseline_keysize_applies =
      cert->valid_start() >= kBaselineEffectiveDate &&
      cert->valid_expiry() >= kBaselineKeysizeEffectiveDate;

  X509Certificate::GetPublicKeyInfo(cert->os_cert_handle(), &size_bits, &type);
  if (should_histogram) {
    RecordPublicKeyHistogram(kLeafCert, baseline_keysize_applies, size_bits,
                             type);
  }
  if (IsWeakKey(type, size_bits))
    weak_key = true;

  const X509Certificate::OSCertHandles& intermediates =
      cert->GetIntermediateCertificates();
  for (size_t i = 0; i < intermediates.size(); ++i) {
    X509Certificate::GetPublicKeyInfo(intermediates[i], &size_bits, &type);
    if (should_histogram) {
      RecordPublicKeyHistogram(
          (i < intermediates.size() - 1) ? kIntermediateCert : kRootCert,
          baseline_keysize_applies,
          size_bits,
          type);
    }
    if (!weak_key && IsWeakKey(type, size_bits))
      weak_key = true;
  }

  return weak_key;
}

// Beginning with Ballot 118, ratified in the Baseline Requirements v1.2.1,
// CAs MUST NOT issue SHA-1 certificates beginning on 1 January 2016.
bool IsPastSHA1DeprecationDate(const X509Certificate& cert) {
  const base::Time& start = cert.valid_start();
  if (start.is_max() || start.is_null())
    return true;
  // 2016-01-01 00:00:00 UTC.
  const base::Time kSHA1DeprecationDate =
      base::Time::FromInternalValue(INT64_C(13096080000000000));
  return start >= kSHA1DeprecationDate;
}

void BestEffortCheckOCSP(const std::string& raw_response,
                         const X509Certificate& certificate,
                         OCSPVerifyResult* verify_result) {
  if (raw_response.empty()) {
    *verify_result = OCSPVerifyResult();
    verify_result->response_status = OCSPVerifyResult::MISSING;
    return;
  }

  std::string cert_der;
  if (!X509Certificate::GetDEREncoded(certificate.os_cert_handle(),
                                      &cert_der)) {
    *verify_result = OCSPVerifyResult();
    return;
  }

  // Try to get the certificate that signed |certificate|. This will run into
  // problems if the CertVerifyProc implementation doesn't return the ordered
  // certificates. If that happens the OCSP verification may be incorrect.
  std::string issuer_der;
  const X509Certificate::OSCertHandles& intermediates =
      certificate.GetIntermediateCertificates();
  if (intermediates.empty()) {
    if (X509Certificate::IsSelfSigned(certificate.os_cert_handle())) {
      issuer_der = cert_der;
    } else {
      // A valid cert chain wasn't provided.
      *verify_result = OCSPVerifyResult();
      return;
    }
  } else if (!X509Certificate::GetDEREncoded(intermediates.front(),
                                             &issuer_der)) {
    *verify_result = OCSPVerifyResult();
    return;
  }

  verify_result->revocation_status =
      CheckOCSP(raw_response, cert_der, issuer_der, base::Time::Now(),
                &verify_result->response_status);
}

// Records histograms indicating whether the certificate |cert|, which
// is assumed to have been validated chaining to a private root,
// contains the TLS Feature Extension (https://tools.ietf.org/html/rfc7633) and
// has valid OCSP information stapled.
void RecordTLSFeatureExtensionWithPrivateRoot(
    X509Certificate* cert,
    const OCSPVerifyResult& ocsp_result) {
  std::string cert_der;
  if (!X509Certificate::GetDEREncoded(cert->os_cert_handle(), &cert_der))
    return;

  // This checks only for the presence of the TLS Feature Extension, but
  // does not check the feature list, and in particular does not verify that
  // its value is 'status_request' or 'status_request2'. In practice the
  // only use of the TLS feature extension is for OCSP stapling, so
  // don't bother to check the value.
  bool has_extension = asn1::HasTLSFeatureExtension(cert_der);

  UMA_HISTOGRAM_BOOLEAN("Net.Certificate.TLSFeatureExtensionWithPrivateRoot",
                        has_extension);
  if (!has_extension)
    return;

  UMA_HISTOGRAM_BOOLEAN(
      "Net.Certificate.TLSFeatureExtensionWithPrivateRootHasOCSP",
      (ocsp_result.response_status != OCSPVerifyResult::MISSING));
}

// Comparison functor used for binary searching whether a given HashValue,
// which MUST be a SHA-256 hash, is contained with an array of SHA-256
// hashes.
struct HashToArrayComparator {
  template <size_t N>
  bool operator()(const uint8_t(&lhs)[N], const HashValue& rhs) const {
    static_assert(N == crypto::kSHA256Length,
                  "Only SHA-256 hashes are supported");
    return memcmp(lhs, rhs.data(), crypto::kSHA256Length) < 0;
  }

  template <size_t N>
  bool operator()(const HashValue& lhs, const uint8_t(&rhs)[N]) const {
    static_assert(N == crypto::kSHA256Length,
                  "Only SHA-256 hashes are supported");
    return memcmp(lhs.data(), rhs, crypto::kSHA256Length) < 0;
  }
};

bool AreSHA1IntermediatesAllowed() {
#if defined(OS_WIN)
  // TODO(rsleevi): Remove this once https://crbug.com/588789 is resolved
  // for Windows 7/2008 users.
  // Note: This must be kept in sync with cert_verify_proc_unittest.cc
  return base::win::GetVersion() < base::win::VERSION_WIN8;
#else
  return false;
#endif
};

// Sets the "has_*" boolean members in |verify_result| that correspond with
// the the presence of |hash| somewhere in the certificate chain (excluding the
// trust anchor).
void MapAlgorithmToBool(DigestAlgorithm hash, CertVerifyResult* verify_result) {
  switch (hash) {
    case DigestAlgorithm::Md2:
      verify_result->has_md2 = true;
      break;
    case DigestAlgorithm::Md4:
      verify_result->has_md4 = true;
      break;
    case DigestAlgorithm::Md5:
      verify_result->has_md5 = true;
      break;
    case DigestAlgorithm::Sha1:
      verify_result->has_sha1 = true;
      break;
    case DigestAlgorithm::Sha256:
    case DigestAlgorithm::Sha384:
    case DigestAlgorithm::Sha512:
      break;
  }
}

// Inspects the signature algorithms in a single certificate |cert|.
//
//   * Sets |verify_result->has_md2| to true if the certificate uses MD2.
//   * Sets |verify_result->has_md4| to true if the certificate uses MD4.
//   * Sets |verify_result->has_md5| to true if the certificate uses MD5.
//   * Sets |verify_result->has_sha1| to true if the certificate uses SHA1.
//
// Returns false if the signature algorithm was unknown or mismatched.
WARN_UNUSED_RESULT bool InspectSignatureAlgorithmForCert(
    X509Certificate::OSCertHandle cert,
    CertVerifyResult* verify_result) {
  std::string cert_der;
  base::StringPiece cert_algorithm_sequence;
  base::StringPiece tbs_algorithm_sequence;

  // Extract the AlgorithmIdentifier SEQUENCEs
  if (!X509Certificate::GetDEREncoded(cert, &cert_der) ||
      !asn1::ExtractSignatureAlgorithmsFromDERCert(
          cert_der, &cert_algorithm_sequence, &tbs_algorithm_sequence)) {
    return false;
  }

  if (!SignatureAlgorithm::IsEquivalent(der::Input(cert_algorithm_sequence),
                                        der::Input(tbs_algorithm_sequence))) {
    return false;
  }

  std::unique_ptr<SignatureAlgorithm> algorithm =
      SignatureAlgorithm::Create(der::Input(cert_algorithm_sequence), nullptr);
  if (!algorithm)
    return false;

  MapAlgorithmToBool(algorithm->digest(), verify_result);

  // Check algorithm-specific parameters.
  switch (algorithm->algorithm()) {
    case SignatureAlgorithmId::Dsa:
    case SignatureAlgorithmId::RsaPkcs1:
    case SignatureAlgorithmId::Ecdsa:
      DCHECK(!algorithm->has_params());
      break;
    case SignatureAlgorithmId::RsaPss:
      MapAlgorithmToBool(algorithm->ParamsForRsaPss()->mgf1_hash(),
                         verify_result);
      break;
  }

  return true;
}

// InspectSignatureAlgorithmsInChain() sets |verify_result->has_*| based on
// the signature algorithms used in the chain, and also checks that certificates
// don't have contradictory signature algorithms.
//
// Returns false if any signature algorithm in the chain is unknown or
// mismatched.
//
// Background:
//
// X.509 certificates contain two redundant descriptors for the signature
// algorithm; one is covered by the signature, but in order to verify the
// signature, the other signature algorithm is untrusted.
//
// RFC 5280 states that the two should be equal, in order to mitigate risk of
// signature substitution attacks, but also discourages verifiers from enforcing
// the profile of RFC 5280.
//
// System verifiers are inconsistent - some use the unsigned signature, some use
// the signed signature, and they generally do not enforce that both match. This
// creates confusion, as it's possible that the signature itself may be checked
// using algorithm A, but if subsequent consumers report the certificate
// algorithm, they may end up reporting algorithm B, which was not used to
// verify the certificate. This function enforces that the two signatures match
// in order to prevent such confusion.
WARN_UNUSED_RESULT bool InspectSignatureAlgorithmsInChain(
    CertVerifyResult* verify_result) {
  const X509Certificate::OSCertHandles& intermediates =
      verify_result->verified_cert->GetIntermediateCertificates();

  // If there are no intermediates, then the leaf is trusted or verification
  // failed.
  if (intermediates.empty())
    return true;

  DCHECK(!verify_result->has_sha1);

  // Fill in hash algorithms for the leaf certificate.
  if (!InspectSignatureAlgorithmForCert(
          verify_result->verified_cert->os_cert_handle(), verify_result)) {
    return false;
  }

  verify_result->has_sha1_leaf = verify_result->has_sha1;

  // Fill in hash algorithms for the intermediate cerificates, excluding the
  // final one (which is presumably the trust anchor; may be incorrect for
  // partial chains).
  for (size_t i = 0; i + 1 < intermediates.size(); ++i) {
    if (!InspectSignatureAlgorithmForCert(intermediates[i], verify_result))
      return false;
  }

  return true;
}

}  // namespace

// static
scoped_refptr<CertVerifyProc> CertVerifyProc::CreateDefault() {
#if defined(USE_NSS_CERTS)
  return new CertVerifyProcNSS();
#elif defined(OS_ANDROID)
  return new CertVerifyProcAndroid();
#elif defined(OS_IOS)
  return new CertVerifyProcIOS();
#elif defined(OS_MACOSX)
  return new CertVerifyProcMac();
#elif defined(OS_WIN)
  return new CertVerifyProcWin();
#elif defined(OS_FUCHSIA)
  return CreateCertVerifyProcBuiltin();
#else
#error Unsupported platform
#endif
}

CertVerifyProc::CertVerifyProc()
    : sha1_legacy_mode_enabled(base::FeatureList::IsEnabled(kSHA1LegacyMode)) {}

CertVerifyProc::~CertVerifyProc() {}

int CertVerifyProc::Verify(X509Certificate* cert,
                           const std::string& hostname,
                           const std::string& ocsp_response,
                           int flags,
                           CRLSet* crl_set,
                           const CertificateList& additional_trust_anchors,
                           CertVerifyResult* verify_result) {
  // CertVerifyProc's contract allows ::VerifyInternal() to wait on File I/O
  // (such as the Windows registry or smart cards on all platforms) or may re-
  // enter this code via extension hooks (such as smart card UI). To ensure
  // threads are not starved or deadlocked, the base::ScopedBlockingCall below
  // increments the thread pool capacity when this method takes too much time to
  // run.
  base::ScopedBlockingCall scoped_blocking_call(base::BlockingType::MAY_BLOCK);

  verify_result->Reset();
  verify_result->verified_cert = cert;

  if (IsBlacklisted(cert)) {
    verify_result->cert_status |= CERT_STATUS_REVOKED;
    return ERR_CERT_REVOKED;
  }

  // We do online revocation checking for EV certificates that aren't covered
  // by a fresh CRLSet.
  // TODO(rsleevi): http://crbug.com/142974 - Allow preferences to fully
  // disable revocation checking.
  if (flags & CertVerifier::VERIFY_EV_CERT)
    flags |= CertVerifier::VERIFY_REV_CHECKING_ENABLED_EV_ONLY;

  int rv = VerifyInternal(cert, hostname, ocsp_response, flags, crl_set,
                          additional_trust_anchors, verify_result);

  // Check for mismatched signature algorithms and unknown signature algorithms
  // in the chain. Also fills in the has_* booleans for the digest algorithms
  // present in the chain.
  if (!InspectSignatureAlgorithmsInChain(verify_result)) {
    verify_result->cert_status |= CERT_STATUS_INVALID;
    rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  bool allow_common_name_fallback =
      !verify_result->is_issued_by_known_root &&
      (flags & CertVerifier::VERIFY_ENABLE_COMMON_NAME_FALLBACK_LOCAL_ANCHORS);
  if (!cert->VerifyNameMatch(hostname, allow_common_name_fallback)) {
    verify_result->cert_status |= CERT_STATUS_COMMON_NAME_INVALID;
    rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  BestEffortCheckOCSP(ocsp_response, *verify_result->verified_cert,
                      &verify_result->ocsp_result);

  // This check is done after VerifyInternal so that VerifyInternal can fill
  // in the list of public key hashes.
  if (IsPublicKeyBlacklisted(verify_result->public_key_hashes)) {
    verify_result->cert_status |= CERT_STATUS_REVOKED;
    rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  std::vector<std::string> dns_names, ip_addrs;
  cert->GetSubjectAltName(&dns_names, &ip_addrs);
  if (HasNameConstraintsViolation(verify_result->public_key_hashes,
                                  cert->subject().common_name,
                                  dns_names,
                                  ip_addrs)) {
    verify_result->cert_status |= CERT_STATUS_NAME_CONSTRAINT_VIOLATION;
    rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  // Check for weak keys in the entire verified chain.
  bool weak_key = ExaminePublicKeys(verify_result->verified_cert,
                                    verify_result->is_issued_by_known_root);

  if (weak_key) {
    verify_result->cert_status |= CERT_STATUS_WEAK_KEY;
    // Avoid replacing a more serious error, such as an OS/library failure,
    // by ensuring that if verification failed, it failed with a certificate
    // error.
    if (rv == OK || IsCertificateError(rv))
      rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  // Treat certificates signed using broken signature algorithms as invalid.
  if (verify_result->has_md2 || verify_result->has_md4) {
    verify_result->cert_status |= CERT_STATUS_INVALID;
    rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  if (verify_result->has_sha1)
    verify_result->cert_status |= CERT_STATUS_SHA1_SIGNATURE_PRESENT;

  // Flag certificates using weak signature algorithms.

  // Legacy SHA-1 behaviour:
  // - Reject all publicly trusted SHA-1 leaf certs issued after
  //   2016-01-01.
  bool legacy_sha1_issue = verify_result->has_sha1_leaf &&
                           verify_result->is_issued_by_known_root &&
                           IsPastSHA1DeprecationDate(*cert);

  // Current SHA-1 behaviour:
  // - Reject all SHA-1
  // - ... unless it's not publicly trusted and SHA-1 is allowed
  // - ... or SHA-1 is in the intermediate and SHA-1 intermediates are
  //   allowed for that platform. See https://crbug.com/588789
  bool current_sha1_issue =
      (verify_result->is_issued_by_known_root ||
       !(flags & CertVerifier::VERIFY_ENABLE_SHA1_LOCAL_ANCHORS)) &&
      (verify_result->has_sha1_leaf ||
       (verify_result->has_sha1 && !AreSHA1IntermediatesAllowed()));

  if (verify_result->has_md5 ||
      (sha1_legacy_mode_enabled && legacy_sha1_issue) ||
      (!sha1_legacy_mode_enabled && current_sha1_issue)) {
    verify_result->cert_status |= CERT_STATUS_WEAK_SIGNATURE_ALGORITHM;
    // Avoid replacing a more serious error, such as an OS/library failure,
    // by ensuring that if verification failed, it failed with a certificate
    // error.
    if (rv == OK || IsCertificateError(rv))
      rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  // Flag certificates from publicly-trusted CAs that are issued to intranet
  // hosts. While the CA/Browser Forum Baseline Requirements (v1.1) permit
  // these to be issued until 1 November 2015, they represent a real risk for
  // the deployment of gTLDs and are being phased out ahead of the hard
  // deadline.
  if (verify_result->is_issued_by_known_root && IsHostnameNonUnique(hostname)) {
    verify_result->cert_status |= CERT_STATUS_NON_UNIQUE_NAME;
    // CERT_STATUS_NON_UNIQUE_NAME will eventually become a hard error. For
    // now treat it as a warning and do not map it to an error return value.
  }

  // Flag certificates using too long validity periods.
  if (verify_result->is_issued_by_known_root && HasTooLongValidity(*cert)) {
    verify_result->cert_status |= CERT_STATUS_VALIDITY_TOO_LONG;
    if (rv == OK)
      rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  // Record a histogram for the presence of the TLS feature extension in
  // a certificate chaining to a private root.
  if (rv == OK && !verify_result->is_issued_by_known_root)
    RecordTLSFeatureExtensionWithPrivateRoot(cert, verify_result->ocsp_result);

  return rv;
}

// static
bool CertVerifyProc::IsBlacklisted(X509Certificate* cert) {
  // CloudFlare revoked all certificates issued prior to April 2nd, 2014. Thus
  // all certificates where the CN ends with ".cloudflare.com" with a prior
  // issuance date are rejected.
  //
  // The old certs had a lifetime of five years, so this can be removed April
  // 2nd, 2019.
  const std::string& cn = cert->subject().common_name;
  static const char kCloudFlareCNSuffix[] = ".cloudflare.com";
  // kCloudFlareEpoch is the base::Time internal value for midnight at the
  // beginning of April 2nd, 2014, UTC.
  static const int64_t kCloudFlareEpoch = INT64_C(13040870400000000);
  if (cn.size() > arraysize(kCloudFlareCNSuffix) - 1 &&
      cn.compare(cn.size() - (arraysize(kCloudFlareCNSuffix) - 1),
                 arraysize(kCloudFlareCNSuffix) - 1,
                 kCloudFlareCNSuffix) == 0 &&
      cert->valid_start() < base::Time::FromInternalValue(kCloudFlareEpoch)) {
    return true;
  }

  return false;
}

// static
bool CertVerifyProc::IsPublicKeyBlacklisted(
    const HashValueVector& public_key_hashes) {
// Defines kBlacklistedSPKIs.
#include "net/cert/cert_verify_proc_blacklist.inc"
  for (const auto& hash : public_key_hashes) {
    if (hash.tag != HASH_VALUE_SHA256)
      continue;
    if (std::binary_search(std::begin(kBlacklistedSPKIs),
                           std::end(kBlacklistedSPKIs), hash,
                           HashToArrayComparator())) {
      return true;
    }
  }
  return false;
}

static const size_t kMaxDomainLength = 18;

// CheckNameConstraints verifies that every name in |dns_names| is in one of
// the domains specified by |domains|. The |domains| array is terminated by an
// empty string.
static bool CheckNameConstraints(const std::vector<std::string>& dns_names,
                                 const char domains[][kMaxDomainLength]) {
  for (std::vector<std::string>::const_iterator i = dns_names.begin();
       i != dns_names.end(); ++i) {
    bool ok = false;
    url::CanonHostInfo host_info;
    const std::string dns_name = CanonicalizeHost(*i, &host_info);
    if (host_info.IsIPAddress())
      continue;

    // If the name is not in a known TLD, ignore it. This permits internal
    // names.
    if (!registry_controlled_domains::HostHasRegistryControlledDomain(
            dns_name, registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES,
            registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES))
      continue;

    for (size_t j = 0; domains[j][0]; ++j) {
      const size_t domain_length = strlen(domains[j]);
      // The DNS name must have "." + domains[j] as a suffix.
      if (i->size() <= (1 /* period before domain */ + domain_length))
        continue;

      std::string suffix =
          base::ToLowerASCII(&(*i)[i->size() - domain_length - 1]);
      if (suffix[0] != '.')
        continue;
      if (memcmp(&suffix[1], domains[j], domain_length) != 0)
        continue;
      ok = true;
      break;
    }

    if (!ok)
      return false;
  }

  return true;
}

// PublicKeyDomainLimitation contains SHA-256(SPKI) and a pointer to an array of
// fixed-length strings that contain the domains that the SPKI is allowed to
// issue for.
struct PublicKeyDomainLimitation {
  uint8_t public_key[crypto::kSHA256Length];
  const char (*domains)[kMaxDomainLength];
};

// static
bool CertVerifyProc::HasNameConstraintsViolation(
    const HashValueVector& public_key_hashes,
    const std::string& common_name,
    const std::vector<std::string>& dns_names,
    const std::vector<std::string>& ip_addrs) {
  static const char kDomainsANSSI[][kMaxDomainLength] = {
    "fr",  // France
    "gp",  // Guadeloupe
    "gf",  // Guyane
    "mq",  // Martinique
    "re",  // Réunion
    "yt",  // Mayotte
    "pm",  // Saint-Pierre et Miquelon
    "bl",  // Saint Barthélemy
    "mf",  // Saint Martin
    "wf",  // Wallis et Futuna
    "pf",  // Polynésie française
    "nc",  // Nouvelle Calédonie
    "tf",  // Terres australes et antarctiques françaises
    "",
  };

  static const char kDomainsIndiaCCA[][kMaxDomainLength] = {
    "gov.in",
    "nic.in",
    "ac.in",
    "rbi.org.in",
    "bankofindia.co.in",
    "ncode.in",
    "tcs.co.in",
    "",
  };

  static const char kDomainsTest[][kMaxDomainLength] = {
    "example.com",
    "",
  };

  static const PublicKeyDomainLimitation kLimits[] = {
      // C=FR, ST=France, L=Paris, O=PM/SGDN, OU=DCSSI,
      // CN=IGC/A/emailAddress=igca@sgdn.pm.gouv.fr
      //
      // net/data/ssl/blacklist/b9bea7860a962ea3611dab97ab6da3e21c1068b97d55575ed0e11279c11c8932.pem
      {
          {0x86, 0xc1, 0x3a, 0x34, 0x08, 0xdd, 0x1a, 0xa7, 0x7e, 0xe8, 0xb6,
           0x94, 0x7c, 0x03, 0x95, 0x87, 0x72, 0xf5, 0x31, 0x24, 0x8c, 0x16,
           0x27, 0xbe, 0xfb, 0x2c, 0x4f, 0x4b, 0x04, 0xd0, 0x44, 0x96},
          kDomainsANSSI,
      },
      // C=IN, O=India PKI, CN=CCA India 2007
      // Expires: July 4th 2015.
      //
      // net/data/ssl/blacklist/f375e2f77a108bacc4234894a9af308edeca1acd8fbde0e7aaa9634e9daf7e1c.pem
      {
          {0x7e, 0x6a, 0xcd, 0x85, 0x3c, 0xac, 0xc6, 0x93, 0x2e, 0x9b, 0x51,
           0x9f, 0xda, 0xd1, 0xbe, 0xb5, 0x15, 0xed, 0x2a, 0x2d, 0x00, 0x25,
           0xcf, 0xd3, 0x98, 0xc3, 0xac, 0x1f, 0x0d, 0xbb, 0x75, 0x4b},
          kDomainsIndiaCCA,
      },
      // C=IN, O=India PKI, CN=CCA India 2011
      // Expires: March 11 2016.
      //
      // net/data/ssl/blacklist/2d66a702ae81ba03af8cff55ab318afa919039d9f31b4d64388680f81311b65a.pem
      {
          {0x42, 0xa7, 0x09, 0x84, 0xff, 0xd3, 0x99, 0xc4, 0xea, 0xf0, 0xe7,
           0x02, 0xa4, 0x4b, 0xef, 0x2a, 0xd8, 0xa7, 0x9b, 0x8b, 0xf4, 0x64,
           0x8f, 0x6b, 0xb2, 0x10, 0xe1, 0x23, 0xfd, 0x07, 0x57, 0x93},
          kDomainsIndiaCCA,
      },
      // C=IN, O=India PKI, CN=CCA India 2014
      // Expires: March 5 2024.
      //
      // net/data/ssl/blacklist/60109bc6c38328598a112c7a25e38b0f23e5a7511cb815fb64e0c4ff05db7df7.pem
      {
          {0x9c, 0xf4, 0x70, 0x4f, 0x3e, 0xe5, 0xa5, 0x98, 0x94, 0xb1, 0x6b,
           0xf0, 0x0c, 0xfe, 0x73, 0xd5, 0x88, 0xda, 0xe2, 0x69, 0xf5, 0x1d,
           0xe6, 0x6a, 0x4b, 0xa7, 0x74, 0x46, 0xee, 0x2b, 0xd1, 0xf7},
          kDomainsIndiaCCA,
      },
      // Not a real certificate - just for testing.
      // net/data/ssl/certificates/name_constraint_*.pem
      {
          {0x8e, 0x9b, 0x14, 0x9f, 0x01, 0x45, 0x4c, 0xee, 0xde, 0xfa, 0x5e,
           0x73, 0x40, 0x36, 0x21, 0xba, 0xd9, 0x1f, 0xee, 0xe0, 0x3e, 0x74,
           0x25, 0x6c, 0x59, 0xf4, 0x6f, 0xbf, 0x45, 0x03, 0x5f, 0x8d},
          kDomainsTest,
      },
  };

  for (unsigned i = 0; i < arraysize(kLimits); ++i) {
    for (HashValueVector::const_iterator j = public_key_hashes.begin();
         j != public_key_hashes.end(); ++j) {
      if (j->tag == HASH_VALUE_SHA256 &&
          memcmp(j->data(), kLimits[i].public_key, crypto::kSHA256Length) ==
              0) {
        if (dns_names.empty() && ip_addrs.empty()) {
          std::vector<std::string> dns_names;
          dns_names.push_back(common_name);
          if (!CheckNameConstraints(dns_names, kLimits[i].domains))
            return true;
        } else {
          if (!CheckNameConstraints(dns_names, kLimits[i].domains))
            return true;
        }
      }
    }
  }

  return false;
}

// static
bool CertVerifyProc::HasTooLongValidity(const X509Certificate& cert) {
  const base::Time& start = cert.valid_start();
  const base::Time& expiry = cert.valid_expiry();
  if (start.is_max() || start.is_null() || expiry.is_max() ||
      expiry.is_null() || start > expiry) {
    return true;
  }

  base::Time::Exploded exploded_start;
  base::Time::Exploded exploded_expiry;
  cert.valid_start().UTCExplode(&exploded_start);
  cert.valid_expiry().UTCExplode(&exploded_expiry);

  if (exploded_expiry.year - exploded_start.year > 10)
    return true;

  int month_diff = (exploded_expiry.year - exploded_start.year) * 12 +
                   (exploded_expiry.month - exploded_start.month);

  // Add any remainder as a full month.
  if (exploded_expiry.day_of_month > exploded_start.day_of_month)
    ++month_diff;

  const base::Time time_2012_07_01 =
      base::Time::FromInternalValue(12985574400000000);
  const base::Time time_2015_04_01 =
      base::Time::FromInternalValue(13072320000000000);
  const base::Time time_2019_07_01 =
      base::Time::FromInternalValue(13206412800000000);

  // For certificates issued before the BRs took effect.
  if (start < time_2012_07_01 && (month_diff > 120 || expiry > time_2019_07_01))
    return true;

  // For certificates issued after 1 July 2012: 60 months.
  if (start >= time_2012_07_01 && month_diff > 60)
    return true;

  // For certificates issued after 1 April 2015: 39 months.
  if (start >= time_2015_04_01 && month_diff > 39)
    return true;

  return false;
}

// static
const base::Feature CertVerifyProc::kSHA1LegacyMode{
    "SHA1LegacyMode", base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace net
