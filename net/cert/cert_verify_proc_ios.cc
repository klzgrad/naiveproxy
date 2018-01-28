// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/cert_verify_proc_ios.h"

#include <CommonCrypto/CommonDigest.h>

#include "base/logging.h"
#include "base/mac/scoped_cftyperef.h"
#include "crypto/sha2.h"
#include "net/base/net_errors.h"
#include "net/cert/asn1_util.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/test_root_certs.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util_ios.h"
#include "net/cert/x509_util_ios_and_mac.h"

using base::ScopedCFTypeRef;

namespace net {

namespace {

int NetErrorFromOSStatus(OSStatus status) {
  switch (status) {
    case noErr:
      return OK;
    case errSecNotAvailable:
      return ERR_NOT_IMPLEMENTED;
    case errSecAuthFailed:
      return ERR_ACCESS_DENIED;
    default:
      return ERR_FAILED;
  }
}

// Creates a series of SecPolicyRefs to be added to a SecTrustRef used to
// validate a certificate for an SSL server. |hostname| contains the name of
// the SSL server that the certificate should be verified against. If
// successful, returns noErr, and stores the resultant array of SecPolicyRefs
// in |policies|.
OSStatus CreateTrustPolicies(ScopedCFTypeRef<CFArrayRef>* policies) {
  ScopedCFTypeRef<CFMutableArrayRef> local_policies(
      CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
  if (!local_policies)
    return errSecAllocate;

  SecPolicyRef ssl_policy = SecPolicyCreateBasicX509();
  CFArrayAppendValue(local_policies, ssl_policy);
  CFRelease(ssl_policy);
  ssl_policy = SecPolicyCreateSSL(true, nullptr);
  CFArrayAppendValue(local_policies, ssl_policy);
  CFRelease(ssl_policy);

  policies->reset(local_policies.release());
  return noErr;
}

// Builds and evaluates a SecTrustRef for the certificate chain contained
// in |cert_array|, using the verification policies in |trust_policies|. On
// success, returns OK, and updates |trust_ref| and |trust_result|. On failure,
// no output parameters are modified.
//
// Note: An OK return does not mean that |cert_array| is trusted, merely that
// verification was performed successfully.
int BuildAndEvaluateSecTrustRef(CFArrayRef cert_array,
                                CFArrayRef trust_policies,
                                ScopedCFTypeRef<SecTrustRef>* trust_ref,
                                ScopedCFTypeRef<CFArrayRef>* verified_chain,
                                SecTrustResultType* trust_result) {
  SecTrustRef tmp_trust = nullptr;
  OSStatus status =
      SecTrustCreateWithCertificates(cert_array, trust_policies, &tmp_trust);
  if (status)
    return NetErrorFromOSStatus(status);
  ScopedCFTypeRef<SecTrustRef> scoped_tmp_trust(tmp_trust);

  if (TestRootCerts::HasInstance()) {
    status = TestRootCerts::GetInstance()->FixupSecTrustRef(tmp_trust);
    if (status)
      return NetErrorFromOSStatus(status);
  }

  SecTrustResultType tmp_trust_result;
  status = SecTrustEvaluate(tmp_trust, &tmp_trust_result);
  if (status)
    return NetErrorFromOSStatus(status);

  ScopedCFTypeRef<CFMutableArrayRef> tmp_verified_chain(
      CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
  const CFIndex chain_length = SecTrustGetCertificateCount(tmp_trust);
  for (CFIndex i = 0; i < chain_length; ++i) {
    SecCertificateRef chain_cert = SecTrustGetCertificateAtIndex(tmp_trust, i);
    CFArrayAppendValue(tmp_verified_chain, chain_cert);
  }

  trust_ref->swap(scoped_tmp_trust);
  *trust_result = tmp_trust_result;
  verified_chain->reset(tmp_verified_chain.release());
  return OK;
}

void GetCertChainInfo(CFArrayRef cert_chain, CertVerifyResult* verify_result) {
  DCHECK_LT(0, CFArrayGetCount(cert_chain));

  SecCertificateRef verified_cert = nullptr;
  std::vector<SecCertificateRef> verified_chain;
  for (CFIndex i = 0, count = CFArrayGetCount(cert_chain); i < count; ++i) {
    SecCertificateRef chain_cert = reinterpret_cast<SecCertificateRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(cert_chain, i)));
    if (i == 0) {
      verified_cert = chain_cert;
    } else {
      verified_chain.push_back(chain_cert);
    }

    base::ScopedCFTypeRef<CFDataRef> der_data(
        SecCertificateCopyData(chain_cert));
    if (!der_data) {
      verify_result->cert_status |= CERT_STATUS_INVALID;
      return;
    }

    base::StringPiece spki_bytes;
    if (!asn1::ExtractSPKIFromDERCert(
            base::StringPiece(
                reinterpret_cast<const char*>(CFDataGetBytePtr(der_data)),
                CFDataGetLength(der_data)),
            &spki_bytes)) {
      verify_result->cert_status |= CERT_STATUS_INVALID;
      return;
    }

    HashValue sha256(HASH_VALUE_SHA256);
    CC_SHA256(spki_bytes.data(), spki_bytes.size(), sha256.data());
    verify_result->public_key_hashes.push_back(sha256);

    // Ignore the signature algorithm for the trust anchor.
    if ((verify_result->cert_status & CERT_STATUS_AUTHORITY_INVALID) == 0 &&
        i == count - 1) {
      continue;
    }
  }
  if (!verified_cert) {
    NOTREACHED();
    verify_result->cert_status |= CERT_STATUS_INVALID;
    return;
  }

  scoped_refptr<X509Certificate> verified_cert_with_chain =
      x509_util::CreateX509CertificateFromSecCertificate(verified_cert,
                                                         verified_chain);
  if (verified_cert_with_chain)
    verify_result->verified_cert = std::move(verified_cert_with_chain);
  else
    verify_result->cert_status |= CERT_STATUS_INVALID;
}

}  // namespace

CertVerifyProcIOS::CertVerifyProcIOS() {}

// The iOS APIs don't expose an API-stable set of reasons for certificate
// validation failures. However, internally, the reason is tracked, and it's
// converted to user-facing localized strings.
//
// In the absence of a consistent API, convert the English strings to their
// localized counterpart, and then compare that with the error properties. If
// they're equal, it's a strong sign that this was the cause for the error.
// While this will break if/when iOS changes the contents of these strings,
// it's sufficient enough for now.
//
// TODO(rsleevi): https://crbug.com/601915 - Use a less brittle solution when
// possible.
// static
CertStatus CertVerifyProcIOS::GetCertFailureStatusFromTrust(SecTrustRef trust) {
  CertStatus reason = 0;

  base::ScopedCFTypeRef<CFArrayRef> properties(SecTrustCopyProperties(trust));
  if (!properties)
    return CERT_STATUS_INVALID;

  const CFIndex properties_length = CFArrayGetCount(properties);
  if (properties_length == 0)
    return CERT_STATUS_INVALID;

  CFBundleRef bundle =
      CFBundleGetBundleWithIdentifier(CFSTR("com.apple.Security"));
  CFStringRef date_string =
      CFSTR("One or more certificates have expired or are not valid yet.");
  ScopedCFTypeRef<CFStringRef> date_error(CFBundleCopyLocalizedString(
      bundle, date_string, date_string, CFSTR("SecCertificate")));
  CFStringRef trust_string = CFSTR("Root certificate is not trusted.");
  ScopedCFTypeRef<CFStringRef> trust_error(CFBundleCopyLocalizedString(
      bundle, trust_string, trust_string, CFSTR("SecCertificate")));
  CFStringRef weak_string =
      CFSTR("One or more certificates is using a weak key size.");
  ScopedCFTypeRef<CFStringRef> weak_error(CFBundleCopyLocalizedString(
      bundle, weak_string, weak_string, CFSTR("SecCertificate")));
  CFStringRef hostname_mismatch_string = CFSTR("Hostname mismatch.");
  ScopedCFTypeRef<CFStringRef> hostname_mismatch_error(
      CFBundleCopyLocalizedString(bundle, hostname_mismatch_string,
                                  hostname_mismatch_string,
                                  CFSTR("SecCertificate")));

  for (CFIndex i = 0; i < properties_length; ++i) {
    CFDictionaryRef dict = reinterpret_cast<CFDictionaryRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(properties, i)));
    CFStringRef error = reinterpret_cast<CFStringRef>(
        const_cast<void*>(CFDictionaryGetValue(dict, CFSTR("value"))));

    if (CFEqual(error, date_error)) {
      reason |= CERT_STATUS_DATE_INVALID;
    } else if (CFEqual(error, trust_error)) {
      reason |= CERT_STATUS_AUTHORITY_INVALID;
    } else if (CFEqual(error, weak_error)) {
      reason |= CERT_STATUS_WEAK_KEY;
    } else if (CFEqual(error, hostname_mismatch_error)) {
      reason |= CERT_STATUS_COMMON_NAME_INVALID;
    } else {
      reason |= CERT_STATUS_INVALID;
    }
  }

  return reason;
}

bool CertVerifyProcIOS::SupportsAdditionalTrustAnchors() const {
  return false;
}

bool CertVerifyProcIOS::SupportsOCSPStapling() const {
  return false;
}

CertVerifyProcIOS::~CertVerifyProcIOS() = default;

int CertVerifyProcIOS::VerifyInternal(
    X509Certificate* cert,
    const std::string& hostname,
    const std::string& ocsp_response,
    int flags,
    CRLSet* crl_set,
    const CertificateList& additional_trust_anchors,
    CertVerifyResult* verify_result) {
  ScopedCFTypeRef<CFArrayRef> trust_policies;
  OSStatus status = CreateTrustPolicies(&trust_policies);
  if (status)
    return NetErrorFromOSStatus(status);

  ScopedCFTypeRef<CFMutableArrayRef> cert_array(
      x509_util::CreateSecCertificateArrayForX509Certificate(
          cert, x509_util::InvalidIntermediateBehavior::kIgnore));
  if (!cert_array) {
    verify_result->cert_status |= CERT_STATUS_INVALID;
    return ERR_CERT_INVALID;
  }

  ScopedCFTypeRef<SecTrustRef> trust_ref;
  SecTrustResultType trust_result = kSecTrustResultDeny;
  ScopedCFTypeRef<CFArrayRef> final_chain;

  status = BuildAndEvaluateSecTrustRef(cert_array, trust_policies, &trust_ref,
                                       &final_chain, &trust_result);
  if (status)
    return NetErrorFromOSStatus(status);

  if (CFArrayGetCount(final_chain) == 0)
    return ERR_FAILED;

  // TODO(sleevi): Support CRLSet revocation.
  switch (trust_result) {
    case kSecTrustResultUnspecified:
    case kSecTrustResultProceed:
      break;
    case kSecTrustResultDeny:
      verify_result->cert_status |= CERT_STATUS_AUTHORITY_INVALID;
      break;
    default:
      verify_result->cert_status |= GetCertFailureStatusFromTrust(trust_ref);
  }

  GetCertChainInfo(final_chain, verify_result);

  // iOS lacks the ability to distinguish built-in versus non-built-in roots,
  // so opt to 'fail open' of any restrictive policies that apply to built-in
  // roots.
  verify_result->is_issued_by_known_root = false;

  if (IsCertStatusError(verify_result->cert_status))
    return MapCertStatusToNetError(verify_result->cert_status);

  return OK;
}

}  // namespace net
