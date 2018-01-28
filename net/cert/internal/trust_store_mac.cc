// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/trust_store_mac.h"

#include <Security/Security.h>

#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/mac/mac_logging.h"
#include "base/synchronization/lock.h"
#include "crypto/mac_security_services_lock.h"
#include "net/cert/internal/cert_errors.h"
#include "net/cert/internal/parse_name.h"
#include "net/cert/internal/parsed_certificate.h"
#include "net/cert/test_keychain_search_list_mac.h"
#include "net/cert/x509_util.h"
#include "net/cert/x509_util_mac.h"

namespace net {

namespace {

// The rules for interpreting trust settings are documented at:
// https://developer.apple.com/reference/security/1400261-sectrustsettingscopytrustsetting?language=objc

// Indicates the trust status of a certificate.
enum class TrustStatus {
  // Certificate inherits trust value from its issuer. If the certificate is the
  // root of the chain, this implies distrust.
  UNSPECIFIED,
  // Certificate is a trust anchor.
  TRUSTED,
  // Certificate is blacklisted / explicitly distrusted.
  DISTRUSTED
};

// Returns trust status of usage constraints dictionary |trust_dict| for a
// certificate that |is_self_signed|.
TrustStatus IsTrustDictionaryTrustedForPolicy(
    CFDictionaryRef trust_dict,
    bool is_self_signed,
    const CFStringRef target_policy_oid) {
  // An empty trust dict should be interpreted as
  // kSecTrustSettingsResultTrustRoot. This is handled by falling through all
  // the conditions below with the default value of |trust_settings_result|.

  // Trust settings may be scoped to a single application, by checking that the
  // code signing identity of the current application matches the serialized
  // code signing identity in the kSecTrustSettingsApplication key.
  // As this is not presently supported, skip any trust settings scoped to the
  // application.
  if (CFDictionaryContainsKey(trust_dict, kSecTrustSettingsApplication))
    return TrustStatus::UNSPECIFIED;

  // Trust settings may be scoped using policy-specific constraints. For
  // example, SSL trust settings might be scoped to a single hostname, or EAP
  // settings specific to a particular WiFi network.
  // As this is not presently supported, skip any policy-specific trust
  // settings.
  if (CFDictionaryContainsKey(trust_dict, kSecTrustSettingsPolicyString))
    return TrustStatus::UNSPECIFIED;

  // Ignoring kSecTrustSettingsKeyUsage for now; it does not seem relevant to
  // the TLS case.

  // If the trust settings are scoped to a specific policy (via
  // kSecTrustSettingsPolicy), ensure that the policy is the same policy as
  // |target_policy_oid|. If there is no kSecTrustSettingsPolicy key, it's
  // considered a match for all policies.
  SecPolicyRef policy_ref = base::mac::GetValueFromDictionary<SecPolicyRef>(
      trust_dict, kSecTrustSettingsPolicy);
  if (policy_ref) {
    base::ScopedCFTypeRef<CFDictionaryRef> policy_dict;
    {
      base::AutoLock lock(crypto::GetMacSecurityServicesLock());
      policy_dict.reset(SecPolicyCopyProperties(policy_ref));
    }

    // kSecPolicyOid is guaranteed to be present in the policy dictionary.
    //
    // TODO(mattm): remove the CFCastStrict below once Chromium builds against
    // the 10.11 SDK.
    CFStringRef policy_oid = base::mac::GetValueFromDictionary<CFStringRef>(
        policy_dict, base::mac::CFCastStrict<CFStringRef>(kSecPolicyOid));

    if (!CFEqual(policy_oid, target_policy_oid))
      return TrustStatus::UNSPECIFIED;
  }

  // If kSecTrustSettingsResult is not present in the trust dict,
  // kSecTrustSettingsResultTrustRoot is assumed.
  int trust_settings_result = kSecTrustSettingsResultTrustRoot;
  CFNumberRef trust_settings_result_ref =
      base::mac::GetValueFromDictionary<CFNumberRef>(trust_dict,
                                                     kSecTrustSettingsResult);
  if (trust_settings_result_ref &&
      !CFNumberGetValue(trust_settings_result_ref, kCFNumberIntType,
                        &trust_settings_result)) {
    return TrustStatus::UNSPECIFIED;
  }

  if (trust_settings_result == kSecTrustSettingsResultDeny)
    return TrustStatus::DISTRUSTED;

  // kSecTrustSettingsResultTrustRoot can only be applied to root(self-signed)
  // certs.
  if (is_self_signed)
    return (trust_settings_result == kSecTrustSettingsResultTrustRoot)
               ? TrustStatus::TRUSTED
               : TrustStatus::UNSPECIFIED;

  // kSecTrustSettingsResultTrustAsRoot can only be applied to non-root certs.
  return (trust_settings_result == kSecTrustSettingsResultTrustAsRoot)
             ? TrustStatus::TRUSTED
             : TrustStatus::UNSPECIFIED;
}

// Returns true if the trust settings array |trust_settings| for a certificate
// that |is_self_signed| should be treated as a trust anchor.
TrustStatus IsTrustSettingsTrustedForPolicy(CFArrayRef trust_settings,
                                            bool is_self_signed,
                                            const CFStringRef policy_oid) {
  // An empty trust settings array (that is, the trust_settings parameter
  // returns a valid but empty CFArray) means "always trust this certificate"
  // with an overall trust setting for the certificate of
  // kSecTrustSettingsResultTrustRoot.
  if (CFArrayGetCount(trust_settings) == 0 && is_self_signed)
    return TrustStatus::TRUSTED;

  for (CFIndex i = 0, settings_count = CFArrayGetCount(trust_settings);
       i < settings_count; ++i) {
    CFDictionaryRef trust_dict = reinterpret_cast<CFDictionaryRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(trust_settings, i)));
    TrustStatus trust = IsTrustDictionaryTrustedForPolicy(
        trust_dict, is_self_signed, policy_oid);
    if (trust != TrustStatus::UNSPECIFIED)
      return trust;
  }
  return TrustStatus::UNSPECIFIED;
}

// Returns true if the certificate |cert_handle| is trusted for the policy
// |policy_oid|.
TrustStatus IsSecCertificateTrustedForPolicy(SecCertificateRef cert_handle,
                                             const CFStringRef policy_oid) {
  const bool is_self_signed = x509_util::IsSelfSigned(cert_handle);
  // Evaluate trust domains in user, admin, system order. Admin settings can
  // override system ones, and user settings can override both admin and system.
  for (const auto& trust_domain :
       {kSecTrustSettingsDomainUser, kSecTrustSettingsDomainAdmin,
        kSecTrustSettingsDomainSystem}) {
    base::ScopedCFTypeRef<CFArrayRef> trust_settings;
    OSStatus err;
    {
      base::AutoLock lock(crypto::GetMacSecurityServicesLock());
      err = SecTrustSettingsCopyTrustSettings(cert_handle, trust_domain,
                                              trust_settings.InitializeInto());
    }
    if (err == errSecItemNotFound) {
      // No trust settings for that domain.. try the next.
      continue;
    }
    if (err) {
      OSSTATUS_LOG(ERROR, err) << "SecTrustSettingsCopyTrustSettings error";
      continue;
    }
    TrustStatus trust = IsTrustSettingsTrustedForPolicy(
        trust_settings, is_self_signed, policy_oid);
    if (trust != TrustStatus::UNSPECIFIED)
      return trust;
  }

  // No trust settings, or none of the settings were for the correct policy, or
  // had the correct trust result.
  return TrustStatus::UNSPECIFIED;
}

}  // namespace

TrustStoreMac::TrustStoreMac(CFTypeRef policy_oid)
    : policy_oid_(base::mac::CFCastStrict<CFStringRef>(policy_oid)) {
  DCHECK(policy_oid_);
}

TrustStoreMac::~TrustStoreMac() = default;

void TrustStoreMac::SyncGetIssuersOf(const ParsedCertificate* cert,
                                     ParsedCertificateList* issuers) {
  base::ScopedCFTypeRef<CFDataRef> name_data = GetMacNormalizedIssuer(cert);

  base::ScopedCFTypeRef<CFArrayRef> matching_items =
      FindMatchingCertificatesForMacNormalizedSubject(name_data);
  if (!matching_items)
    return;

  // Convert to ParsedCertificate.
  for (CFIndex i = 0, item_count = CFArrayGetCount(matching_items);
       i < item_count; ++i) {
    SecCertificateRef match_cert_handle = reinterpret_cast<SecCertificateRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(matching_items, i)));

    base::ScopedCFTypeRef<CFDataRef> der_data(
        SecCertificateCopyData(match_cert_handle));
    if (!der_data) {
      LOG(ERROR) << "SecCertificateCopyData error";
      continue;
    }

    CertErrors errors;
    ParseCertificateOptions options;
    options.allow_invalid_serial_numbers = true;
    scoped_refptr<ParsedCertificate> anchor_cert = ParsedCertificate::Create(
        x509_util::CreateCryptoBuffer(CFDataGetBytePtr(der_data.get()),
                                      CFDataGetLength(der_data.get())),
        options, &errors);
    if (!anchor_cert) {
      // TODO(crbug.com/634443): return errors better.
      LOG(ERROR) << "Error parsing issuer certificate:\n"
                 << errors.ToDebugString();
      continue;
    }

    issuers->push_back(std::move(anchor_cert));
  }
}

void TrustStoreMac::GetTrust(const scoped_refptr<ParsedCertificate>& cert,
                             CertificateTrust* trust) const {
  // TODO(eroman): Inefficient -- path building will convert between
  // SecCertificateRef and ParsedCertificate representations multiple times
  // (when getting the issuers, and again here).
  base::ScopedCFTypeRef<SecCertificateRef> cert_handle =
      x509_util::CreateSecCertificateFromBytes(cert->der_cert().UnsafeData(),
                                               cert->der_cert().Length());

  TrustStatus trust_status =
      IsSecCertificateTrustedForPolicy(cert_handle, policy_oid_);
  switch (trust_status) {
    case TrustStatus::TRUSTED:
      *trust = CertificateTrust::ForTrustAnchor();
      return;
    case TrustStatus::DISTRUSTED:
      *trust = CertificateTrust::ForDistrusted();
      return;
    case TrustStatus::UNSPECIFIED:
      *trust = CertificateTrust::ForUnspecified();
      return;
  }

  *trust = CertificateTrust::ForUnspecified();
  return;
}

// static
base::ScopedCFTypeRef<CFArrayRef>
TrustStoreMac::FindMatchingCertificatesForMacNormalizedSubject(
    CFDataRef name_data) {
  base::ScopedCFTypeRef<CFArrayRef> matching_items;
  base::ScopedCFTypeRef<CFMutableDictionaryRef> query(
      CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks));

  CFDictionarySetValue(query, kSecClass, kSecClassCertificate);
  CFDictionarySetValue(query, kSecReturnRef, kCFBooleanTrue);
  CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitAll);
  CFDictionarySetValue(query, kSecAttrSubject, name_data);

  base::ScopedCFTypeRef<CFArrayRef> scoped_alternate_keychain_search_list;
  if (TestKeychainSearchList::HasInstance()) {
    OSStatus status = TestKeychainSearchList::GetInstance()->CopySearchList(
        scoped_alternate_keychain_search_list.InitializeInto());
    if (status) {
      OSSTATUS_LOG(ERROR, status)
          << "TestKeychainSearchList::CopySearchList error";
      return matching_items;
    }
  }

  base::AutoLock lock(crypto::GetMacSecurityServicesLock());

  // If a TestKeychainSearchList is present, it will have already set
  // |scoped_alternate_keychain_search_list|, which will be used as the
  // basis for reordering the keychain. Otherwise, get the current keychain
  // search list and use that.
  if (!scoped_alternate_keychain_search_list) {
    OSStatus status = SecKeychainCopySearchList(
        scoped_alternate_keychain_search_list.InitializeInto());
    if (status) {
      OSSTATUS_LOG(ERROR, status) << "SecKeychainCopySearchList error";
      return matching_items;
    }
  }

  CFMutableArrayRef mutable_keychain_search_list = CFArrayCreateMutableCopy(
      kCFAllocatorDefault,
      CFArrayGetCount(scoped_alternate_keychain_search_list.get()) + 1,
      scoped_alternate_keychain_search_list.get());
  if (!mutable_keychain_search_list) {
    LOG(ERROR) << "CFArrayCreateMutableCopy";
    return matching_items;
  }
  scoped_alternate_keychain_search_list.reset(mutable_keychain_search_list);

  base::ScopedCFTypeRef<SecKeychainRef> roots_keychain;
  // The System Roots keychain is not normally searched by SecItemCopyMatching.
  // Get a reference to it and include in the keychain search list.
  OSStatus status = SecKeychainOpen(
      "/System/Library/Keychains/SystemRootCertificates.keychain",
      roots_keychain.InitializeInto());
  if (status) {
    OSSTATUS_LOG(ERROR, status) << "SecKeychainOpen error";
    return matching_items;
  }
  CFArrayAppendValue(mutable_keychain_search_list, roots_keychain);

  CFDictionarySetValue(query, kSecMatchSearchList,
                       scoped_alternate_keychain_search_list.get());

  OSStatus err = SecItemCopyMatching(
      query, reinterpret_cast<CFTypeRef*>(matching_items.InitializeInto()));
  if (err == errSecItemNotFound) {
    // No matches found.
    return matching_items;
  }
  if (err) {
    OSSTATUS_LOG(ERROR, err) << "SecItemCopyMatching error";
    return matching_items;
  }
  return matching_items;
}

// static
base::ScopedCFTypeRef<CFDataRef> TrustStoreMac::GetMacNormalizedIssuer(
    const ParsedCertificate* cert) {
  base::ScopedCFTypeRef<CFDataRef> name_data;
  // There does not appear to be any public API to get the normalized version
  // of a Name without creating a SecCertificate.
  base::ScopedCFTypeRef<SecCertificateRef> cert_handle(
      x509_util::CreateSecCertificateFromBytes(cert->der_cert().UnsafeData(),
                                               cert->der_cert().Length()));
  if (!cert_handle) {
    LOG(ERROR) << "CreateOSCertHandleFromBytes";
    return name_data;
  }
  {
    base::AutoLock lock(crypto::GetMacSecurityServicesLock());
    name_data.reset(
        SecCertificateCopyNormalizedIssuerContent(cert_handle, nullptr));
  }
  if (!name_data)
    LOG(ERROR) << "SecCertificateCopyNormalizedIssuerContent";
  return name_data;
}

}  // namespace net
