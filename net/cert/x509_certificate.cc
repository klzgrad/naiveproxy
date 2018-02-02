// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/x509_certificate.h"

#include <limits.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/numerics/safe_conversions.h"
#include "base/pickle.h"
#include "base/stl_util.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "crypto/openssl_util.h"
#include "net/base/ip_address.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "net/cert/asn1_util.h"
#include "net/cert/internal/cert_errors.h"
#include "net/cert/internal/name_constraints.h"
#include "net/cert/internal/parsed_certificate.h"
#include "net/cert/internal/signature_algorithm.h"
#include "net/cert/internal/verify_name_match.h"
#include "net/cert/internal/verify_signed_data.h"
#include "net/cert/pem_tokenizer.h"
#include "net/cert/x509_util.h"
#include "net/der/parser.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/pkcs7.h"
#include "third_party/boringssl/src/include/openssl/pool.h"
#include "third_party/boringssl/src/include/openssl/sha.h"
#include "url/url_canon.h"

namespace net {

namespace {

// Indicates the order to use when trying to decode binary data, which is
// based on (speculation) as to what will be most common -> least common
const X509Certificate::Format kFormatDecodePriority[] = {
  X509Certificate::FORMAT_SINGLE_CERTIFICATE,
  X509Certificate::FORMAT_PKCS7
};

// The PEM block header used for DER certificates
const char kCertificateHeader[] = "CERTIFICATE";
// The PEM block header used for PKCS#7 data
const char kPKCS7Header[] = "PKCS7";

// Utility to split |src| on the first occurrence of |c|, if any. |right| will
// either be empty if |c| was not found, or will contain the remainder of the
// string including the split character itself.
void SplitOnChar(const base::StringPiece& src,
                 char c,
                 base::StringPiece* left,
                 base::StringPiece* right) {
  size_t pos = src.find(c);
  if (pos == base::StringPiece::npos) {
    *left = src;
    right->clear();
  } else {
    *left = src.substr(0, pos);
    *right = src.substr(pos);
  }
}

// Converts a GeneralizedTime struct to a base::Time, returning true on success
// or false if |generalized| was invalid or cannot be represented by
// base::Time.
bool GeneralizedTimeToBaseTime(const der::GeneralizedTime& generalized,
                               base::Time* result) {
  base::Time::Exploded exploded = {0};
  exploded.year = generalized.year;
  exploded.month = generalized.month;
  exploded.day_of_month = generalized.day;
  exploded.hour = generalized.hours;
  exploded.minute = generalized.minutes;
  exploded.second = generalized.seconds;

  if (base::Time::FromUTCExploded(exploded, result))
    return true;

  // Fail on obviously bad dates.
  if (!exploded.HasValidValues())
    return false;

  // TODO(mattm): consider consolidating this with
  // SaturatedTimeFromUTCExploded from cookie_util.cc
  if (static_cast<int>(generalized.year) > base::Time::kExplodedMaxYear) {
    *result = base::Time::Max();
    return true;
  }
  if (static_cast<int>(generalized.year) < base::Time::kExplodedMinYear) {
    *result = base::Time::Min();
    return true;
  }
  return false;
}

// Sets |value| to the Value from a DER Sequence Tag-Length-Value and return
// true, or return false if the TLV was not a valid DER Sequence.
WARN_UNUSED_RESULT bool GetSequenceValue(const der::Input& tlv,
                                         der::Input* value) {
  der::Parser parser(tlv);
  return parser.ReadTag(der::kSequence, value) && !parser.HasMore();
}

// Normalize |cert|'s Issuer and store it in |out_normalized_issuer|, returning
// true on success or false if there was a parsing error.
bool GetNormalizedCertIssuer(CRYPTO_BUFFER* cert,
                             std::string* out_normalized_issuer) {
  der::Input tbs_certificate_tlv;
  der::Input signature_algorithm_tlv;
  der::BitString signature_value;
  if (!ParseCertificate(
          der::Input(CRYPTO_BUFFER_data(cert), CRYPTO_BUFFER_len(cert)),
          &tbs_certificate_tlv, &signature_algorithm_tlv, &signature_value,
          nullptr)) {
    return false;
  }
  ParsedTbsCertificate tbs;
  if (!ParseTbsCertificate(tbs_certificate_tlv,
                           x509_util::DefaultParseCertificateOptions(), &tbs,
                           nullptr))
    return false;

  der::Input issuer_value;
  if (!GetSequenceValue(tbs.issuer_tlv, &issuer_value))
    return false;

  CertErrors errors;
  return NormalizeName(issuer_value, out_normalized_issuer, &errors);
}

// Parses certificates from a PKCS#7 SignedData structure, appending them to
// |handles|.
void CreateOSCertHandlesFromPKCS7Bytes(
    const char* data,
    size_t length,
    X509Certificate::OSCertHandles* handles) {
  crypto::EnsureOpenSSLInit();
  crypto::OpenSSLErrStackTracer err_cleaner(FROM_HERE);

  CBS der_data;
  CBS_init(&der_data, reinterpret_cast<const uint8_t*>(data), length);
  STACK_OF(CRYPTO_BUFFER)* certs = sk_CRYPTO_BUFFER_new_null();

  if (PKCS7_get_raw_certificates(certs, &der_data,
                                 x509_util::GetBufferPool())) {
    for (size_t i = 0; i < sk_CRYPTO_BUFFER_num(certs); ++i) {
      handles->push_back(sk_CRYPTO_BUFFER_value(certs, i));
    }
  }
  // |handles| took ownership of the individual buffers, so only free the list
  // itself.
  sk_CRYPTO_BUFFER_free(certs);
}

}  // namespace

// static
scoped_refptr<X509Certificate> X509Certificate::CreateFromHandle(
    OSCertHandle cert_handle,
    const OSCertHandles& intermediates) {
  DCHECK(cert_handle);
  scoped_refptr<X509Certificate> cert(
      new X509Certificate(cert_handle, intermediates));
  if (!cert->os_cert_handle())
    return nullptr;  // Initialize() failed.
  return cert;
}

// static
scoped_refptr<X509Certificate> X509Certificate::CreateFromHandleUnsafeOptions(
    OSCertHandle cert_handle,
    const OSCertHandles& intermediates,
    UnsafeCreateOptions options) {
  DCHECK(cert_handle);
  scoped_refptr<X509Certificate> cert(
      new X509Certificate(cert_handle, intermediates, options));
  if (!cert->os_cert_handle())
    return nullptr;  // Initialize() failed.
  return cert;
}

// static
scoped_refptr<X509Certificate> X509Certificate::CreateFromDERCertChain(
    const std::vector<base::StringPiece>& der_certs) {
  TRACE_EVENT0("io", "X509Certificate::CreateFromDERCertChain");
  if (der_certs.empty())
    return NULL;

  X509Certificate::OSCertHandles intermediate_ca_certs;
  for (size_t i = 1; i < der_certs.size(); i++) {
    OSCertHandle handle = CreateOSCertHandleFromBytes(
        const_cast<char*>(der_certs[i].data()), der_certs[i].size());
    if (!handle)
      break;
    intermediate_ca_certs.push_back(handle);
  }

  OSCertHandle handle = NULL;
  // Return NULL if we failed to parse any of the certs.
  if (der_certs.size() - 1 == intermediate_ca_certs.size()) {
    handle = CreateOSCertHandleFromBytes(
        const_cast<char*>(der_certs[0].data()), der_certs[0].size());
  }

  scoped_refptr<X509Certificate> cert = nullptr;
  if (handle) {
    cert = CreateFromHandle(handle, intermediate_ca_certs);
    FreeOSCertHandle(handle);
  }

  for (size_t i = 0; i < intermediate_ca_certs.size(); i++)
    FreeOSCertHandle(intermediate_ca_certs[i]);

  return cert;
}

// static
scoped_refptr<X509Certificate> X509Certificate::CreateFromBytes(
    const char* data,
    size_t length) {
  return CreateFromBytesUnsafeOptions(data, length, {});
}

// static
scoped_refptr<X509Certificate> X509Certificate::CreateFromBytesUnsafeOptions(
    const char* data,
    size_t length,
    UnsafeCreateOptions options) {
  OSCertHandle cert_handle = CreateOSCertHandleFromBytes(data, length);
  if (!cert_handle)
    return NULL;

  scoped_refptr<X509Certificate> cert =
      CreateFromHandleUnsafeOptions(cert_handle, {}, options);
  FreeOSCertHandle(cert_handle);
  return cert;
}

// static
scoped_refptr<X509Certificate> X509Certificate::CreateFromPickle(
    base::PickleIterator* pickle_iter) {
  int chain_length = 0;
  if (!pickle_iter->ReadLength(&chain_length))
    return nullptr;

  std::vector<base::StringPiece> cert_chain;
  const char* data = nullptr;
  int data_length = 0;
  for (int i = 0; i < chain_length; ++i) {
    if (!pickle_iter->ReadData(&data, &data_length))
      return nullptr;
    cert_chain.push_back(base::StringPiece(data, data_length));
  }
  return CreateFromDERCertChain(cert_chain);
}

// static
CertificateList X509Certificate::CreateCertificateListFromBytes(
    const char* data,
    size_t length,
    int format) {
  OSCertHandles certificates;

  // Check to see if it is in a PEM-encoded form. This check is performed
  // first, as both OS X and NSS will both try to convert if they detect
  // PEM encoding, except they don't do it consistently between the two.
  base::StringPiece data_string(data, length);
  std::vector<std::string> pem_headers;

  // To maintain compatibility with NSS/Firefox, CERTIFICATE is a universally
  // valid PEM block header for any format.
  pem_headers.push_back(kCertificateHeader);
  if (format & FORMAT_PKCS7)
    pem_headers.push_back(kPKCS7Header);

  PEMTokenizer pem_tokenizer(data_string, pem_headers);
  while (pem_tokenizer.GetNext()) {
    std::string decoded(pem_tokenizer.data());

    OSCertHandle handle = NULL;
    if (format & FORMAT_PEM_CERT_SEQUENCE)
      handle = CreateOSCertHandleFromBytes(decoded.c_str(), decoded.size());
    if (handle != NULL) {
      // Parsed a DER encoded certificate. All PEM blocks that follow must
      // also be DER encoded certificates wrapped inside of PEM blocks.
      format = FORMAT_PEM_CERT_SEQUENCE;
      certificates.push_back(handle);
      continue;
    }

    // If the first block failed to parse as a DER certificate, and
    // formats other than PEM are acceptable, check to see if the decoded
    // data is one of the accepted formats.
    if (format & ~FORMAT_PEM_CERT_SEQUENCE) {
      for (size_t i = 0; certificates.empty() &&
           i < arraysize(kFormatDecodePriority); ++i) {
        if (format & kFormatDecodePriority[i]) {
          certificates = CreateOSCertHandlesFromBytes(decoded.c_str(),
              decoded.size(), kFormatDecodePriority[i]);
        }
      }
    }

    // Stop parsing after the first block for any format but a sequence of
    // PEM-encoded DER certificates. The case of FORMAT_PEM_CERT_SEQUENCE
    // is handled above, and continues processing until a certificate fails
    // to parse.
    break;
  }

  // Try each of the formats, in order of parse preference, to see if |data|
  // contains the binary representation of a Format, if it failed to parse
  // as a PEM certificate/chain.
  for (size_t i = 0; certificates.empty() &&
       i < arraysize(kFormatDecodePriority); ++i) {
    if (format & kFormatDecodePriority[i])
      certificates = CreateOSCertHandlesFromBytes(data, length,
                                                  kFormatDecodePriority[i]);
  }

  CertificateList results;
  // No certificates parsed.
  if (certificates.empty())
    return results;

  for (OSCertHandles::iterator it = certificates.begin();
       it != certificates.end(); ++it) {
    scoped_refptr<X509Certificate> cert =
        CreateFromHandle(*it, OSCertHandles());
    if (cert)
      results.push_back(std::move(cert));
    FreeOSCertHandle(*it);
  }

  return results;
}

void X509Certificate::Persist(base::Pickle* pickle) {
  DCHECK(cert_handle_);
  // This would be an absolutely insane number of intermediates.
  if (intermediate_ca_certs_.size() > static_cast<size_t>(INT_MAX) - 1) {
    NOTREACHED();
    return;
  }
  pickle->WriteInt(static_cast<int>(intermediate_ca_certs_.size() + 1));
  pickle->WriteString(x509_util::CryptoBufferAsStringPiece(cert_handle_));
  for (auto* intermediate : intermediate_ca_certs_)
    pickle->WriteString(x509_util::CryptoBufferAsStringPiece(intermediate));
}

void X509Certificate::GetDNSNames(std::vector<std::string>* dns_names) const {
  GetSubjectAltName(dns_names, NULL);
  if (dns_names->empty())
    dns_names->push_back(subject_.common_name);
}

bool X509Certificate::GetSubjectAltName(
    std::vector<std::string>* dns_names,
    std::vector<std::string>* ip_addrs) const {
  if (dns_names)
    dns_names->clear();
  if (ip_addrs)
    ip_addrs->clear();

  der::Input tbs_certificate_tlv;
  der::Input signature_algorithm_tlv;
  der::BitString signature_value;
  if (!ParseCertificate(der::Input(CRYPTO_BUFFER_data(cert_handle_),
                                   CRYPTO_BUFFER_len(cert_handle_)),
                        &tbs_certificate_tlv, &signature_algorithm_tlv,
                        &signature_value, nullptr)) {
    return false;
  }

  ParsedTbsCertificate tbs;
  if (!ParseTbsCertificate(tbs_certificate_tlv,
                           x509_util::DefaultParseCertificateOptions(), &tbs,
                           nullptr))
    return false;
  if (!tbs.has_extensions)
    return false;

  std::map<der::Input, ParsedExtension> extensions;
  if (!ParseExtensions(tbs.extensions_tlv, &extensions))
    return false;

  ParsedExtension subject_alt_names_extension;
  if (!ConsumeExtension(SubjectAltNameOid(), &extensions,
                        &subject_alt_names_extension)) {
    return false;
  }

  CertErrors errors;
  std::unique_ptr<GeneralNames> subject_alt_names =
      GeneralNames::Create(subject_alt_names_extension.value, &errors);
  if (!subject_alt_names)
    return false;

  if (dns_names) {
    for (const auto& dns_name : subject_alt_names->dns_names)
      dns_names->push_back(dns_name.as_string());
  }
  if (ip_addrs) {
    for (const IPAddress& addr : subject_alt_names->ip_addresses) {
      ip_addrs->push_back(
          std::string(reinterpret_cast<const char*>(addr.bytes().data()),
                      addr.bytes().size()));
    }
  }

  return !subject_alt_names->dns_names.empty() ||
         !subject_alt_names->ip_addresses.empty();
}

bool X509Certificate::HasExpired() const {
  return base::Time::Now() > valid_expiry();
}

bool X509Certificate::Equals(const X509Certificate* other) const {
  return IsSameOSCert(cert_handle_, other->cert_handle_);
}

bool X509Certificate::IsIssuedByEncoded(
    const std::vector<std::string>& valid_issuers) {
  std::vector<std::string> normalized_issuers;
  CertErrors errors;
  for (const auto& raw_issuer : valid_issuers) {
    der::Input issuer_value;
    std::string normalized_issuer;
    if (!GetSequenceValue(der::Input(&raw_issuer), &issuer_value) ||
        !NormalizeName(issuer_value, &normalized_issuer, &errors)) {
      continue;
    }
    normalized_issuers.push_back(std::move(normalized_issuer));
  }

  std::string normalized_cert_issuer;
  if (!GetNormalizedCertIssuer(cert_handle_, &normalized_cert_issuer))
    return false;
  if (base::ContainsValue(normalized_issuers, normalized_cert_issuer))
    return true;

  for (CRYPTO_BUFFER* intermediate : intermediate_ca_certs_) {
    if (!GetNormalizedCertIssuer(intermediate, &normalized_cert_issuer))
      return false;
    if (base::ContainsValue(normalized_issuers, normalized_cert_issuer))
      return true;
  }
  return false;
}

// static
bool X509Certificate::VerifyHostname(
    const std::string& hostname,
    const std::string& cert_common_name,
    const std::vector<std::string>& cert_san_dns_names,
    const std::vector<std::string>& cert_san_ip_addrs,
    bool allow_common_name_fallback) {
  DCHECK(!hostname.empty());
  // Perform name verification following http://tools.ietf.org/html/rfc6125.
  // The terminology used in this method is as per that RFC:-
  // Reference identifier == the host the local user/agent is intending to
  //                         access, i.e. the thing displayed in the URL bar.
  // Presented identifier(s) == name(s) the server knows itself as, in its cert.

  // CanonicalizeHost requires surrounding brackets to parse an IPv6 address.
  const std::string host_or_ip = hostname.find(':') != std::string::npos ?
      "[" + hostname + "]" : hostname;
  url::CanonHostInfo host_info;
  std::string reference_name = CanonicalizeHost(host_or_ip, &host_info);
  // CanonicalizeHost does not normalize absolute vs relative DNS names. If
  // the input name was absolute (included trailing .), normalize it as if it
  // was relative.
  if (!reference_name.empty() && *reference_name.rbegin() == '.')
    reference_name.resize(reference_name.size() - 1);
  if (reference_name.empty())
    return false;

  if (!allow_common_name_fallback && cert_san_dns_names.empty() &&
      cert_san_ip_addrs.empty()) {
    // Common Name matching is not allowed, so fail fast.
    return false;
  }

  // Fully handle all cases where |hostname| contains an IP address.
  if (host_info.IsIPAddress()) {
    if (allow_common_name_fallback && cert_san_dns_names.empty() &&
        cert_san_ip_addrs.empty() &&
        host_info.family == url::CanonHostInfo::IPV4) {
      // Fallback to Common name matching. As this is deprecated and only
      // supported for compatibility refuse it for IPv6 addresses.
      return reference_name == cert_common_name;
    }
    base::StringPiece ip_addr_string(
        reinterpret_cast<const char*>(host_info.address),
        host_info.AddressLength());
    return base::ContainsValue(cert_san_ip_addrs, ip_addr_string);
  }

  // |reference_domain| is the remainder of |host| after the leading host
  // component is stripped off, but includes the leading dot e.g.
  // "www.f.com" -> ".f.com".
  // If there is no meaningful domain part to |host| (e.g. it contains no dots)
  // then |reference_domain| will be empty.
  base::StringPiece reference_host, reference_domain;
  SplitOnChar(reference_name, '.', &reference_host, &reference_domain);
  bool allow_wildcards = false;
  if (!reference_domain.empty()) {
    DCHECK(reference_domain.starts_with("."));

    // Do not allow wildcards for public/ICANN registry controlled domains -
    // that is, prevent *.com or *.co.uk as valid presented names, but do not
    // prevent *.appspot.com (a private registry controlled domain).
    // In addition, unknown top-level domains (such as 'intranet' domains or
    // new TLDs/gTLDs not yet added to the registry controlled domain dataset)
    // are also implicitly prevented.
    // Because |reference_domain| must contain at least one name component that
    // is not registry controlled, this ensures that all reference domains
    // contain at least three domain components when using wildcards.
    size_t registry_length =
        registry_controlled_domains::GetCanonicalHostRegistryLength(
            reference_name,
            registry_controlled_domains::INCLUDE_UNKNOWN_REGISTRIES,
            registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);

    // Because |reference_name| was already canonicalized, the following
    // should never happen.
    CHECK_NE(std::string::npos, registry_length);

    // Account for the leading dot in |reference_domain|.
    bool is_registry_controlled =
        registry_length != 0 &&
        registry_length == (reference_domain.size() - 1);

    // Additionally, do not attempt wildcard matching for purely numeric
    // hostnames.
    allow_wildcards =
        !is_registry_controlled &&
        reference_name.find_first_not_of("0123456789.") != std::string::npos;
  }

  // Now step through the DNS names doing wild card comparison (if necessary)
  // on each against the reference name. If subjectAltName is empty, then
  // fallback to use the common name instead.
  std::vector<std::string> common_name_as_vector;
  const std::vector<std::string>* presented_names = &cert_san_dns_names;
  if (allow_common_name_fallback && cert_san_dns_names.empty() &&
      cert_san_ip_addrs.empty()) {
    // Note: there's a small possibility cert_common_name is an international
    // domain name in non-standard encoding (e.g. UTF8String or BMPString
    // instead of A-label). As common name fallback is deprecated we're not
    // doing anything specific to deal with this.
    common_name_as_vector.push_back(cert_common_name);
    presented_names = &common_name_as_vector;
  }
  for (std::vector<std::string>::const_iterator it =
           presented_names->begin();
       it != presented_names->end(); ++it) {
    // Catch badly corrupt cert names up front.
    if (it->empty() || it->find('\0') != std::string::npos) {
      DVLOG(1) << "Bad name in cert: " << *it;
      continue;
    }
    std::string presented_name(base::ToLowerASCII(*it));

    // Remove trailing dot, if any.
    if (*presented_name.rbegin() == '.')
      presented_name.resize(presented_name.length() - 1);

    // The hostname must be at least as long as the cert name it is matching,
    // as we require the wildcard (if present) to match at least one character.
    if (presented_name.length() > reference_name.length())
      continue;

    base::StringPiece presented_host, presented_domain;
    SplitOnChar(presented_name, '.', &presented_host, &presented_domain);

    if (presented_domain != reference_domain)
      continue;

    if (presented_host != "*") {
      if (presented_host == reference_host)
        return true;
      continue;
    }

    if (!allow_wildcards)
      continue;

    return true;
  }
  return false;
}

bool X509Certificate::VerifyNameMatch(const std::string& hostname,
                                      bool allow_common_name_fallback) const {
  std::vector<std::string> dns_names, ip_addrs;
  GetSubjectAltName(&dns_names, &ip_addrs);
  return VerifyHostname(hostname, subject_.common_name, dns_names, ip_addrs,
                        allow_common_name_fallback);
}

// static
bool X509Certificate::GetDEREncoded(X509Certificate::OSCertHandle cert_handle,
                                    std::string* encoded) {
  if (!cert_handle)
    return false;
  encoded->assign(
      reinterpret_cast<const char*>(CRYPTO_BUFFER_data(cert_handle)),
      CRYPTO_BUFFER_len(cert_handle));
  return true;
}

// static
bool X509Certificate::GetPEMEncodedFromDER(const std::string& der_encoded,
                                           std::string* pem_encoded) {
  if (der_encoded.empty())
    return false;
  std::string b64_encoded;
  base::Base64Encode(der_encoded, &b64_encoded);
  *pem_encoded = "-----BEGIN CERTIFICATE-----\n";

  // Divide the Base-64 encoded data into 64-character chunks, as per
  // 4.3.2.4 of RFC 1421.
  static const size_t kChunkSize = 64;
  size_t chunks = (b64_encoded.size() + (kChunkSize - 1)) / kChunkSize;
  for (size_t i = 0, chunk_offset = 0; i < chunks;
       ++i, chunk_offset += kChunkSize) {
    pem_encoded->append(b64_encoded, chunk_offset, kChunkSize);
    pem_encoded->append("\n");
  }
  pem_encoded->append("-----END CERTIFICATE-----\n");
  return true;
}

// static
bool X509Certificate::GetPEMEncoded(OSCertHandle cert_handle,
                                    std::string* pem_encoded) {
  std::string der_encoded;
  if (!GetDEREncoded(cert_handle, &der_encoded))
    return false;
  return GetPEMEncodedFromDER(der_encoded, pem_encoded);
}

bool X509Certificate::GetPEMEncodedChain(
    std::vector<std::string>* pem_encoded) const {
  std::vector<std::string> encoded_chain;
  std::string pem_data;
  if (!GetPEMEncoded(os_cert_handle(), &pem_data))
    return false;
  encoded_chain.push_back(pem_data);
  for (size_t i = 0; i < intermediate_ca_certs_.size(); ++i) {
    if (!GetPEMEncoded(intermediate_ca_certs_[i], &pem_data))
      return false;
    encoded_chain.push_back(pem_data);
  }
  pem_encoded->swap(encoded_chain);
  return true;
}

// static
void X509Certificate::GetPublicKeyInfo(OSCertHandle cert_handle,
                                       size_t* size_bits,
                                       PublicKeyType* type) {
  *type = kPublicKeyTypeUnknown;
  *size_bits = 0;

  base::StringPiece spki;
  if (!asn1::ExtractSPKIFromDERCert(
          base::StringPiece(
              reinterpret_cast<const char*>(CRYPTO_BUFFER_data(cert_handle)),
              CRYPTO_BUFFER_len(cert_handle)),
          &spki)) {
    return;
  }

  bssl::UniquePtr<EVP_PKEY> pkey;
  crypto::OpenSSLErrStackTracer err_tracer(FROM_HERE);
  CBS cbs;
  CBS_init(&cbs, reinterpret_cast<const uint8_t*>(spki.data()), spki.size());
  pkey.reset(EVP_parse_public_key(&cbs));
  if (!pkey)
    return;

  switch (pkey->type) {
    case EVP_PKEY_RSA:
      *type = kPublicKeyTypeRSA;
      break;
    case EVP_PKEY_DSA:
      *type = kPublicKeyTypeDSA;
      break;
    case EVP_PKEY_EC:
      *type = kPublicKeyTypeECDSA;
      break;
    case EVP_PKEY_DH:
      *type = kPublicKeyTypeDH;
      break;
  }
  *size_bits = base::saturated_cast<size_t>(EVP_PKEY_bits(pkey.get()));
}

// static
bool X509Certificate::IsSameOSCert(X509Certificate::OSCertHandle a,
                                   X509Certificate::OSCertHandle b) {
  DCHECK(a && b);
  if (a == b)
    return true;
  return CRYPTO_BUFFER_len(a) == CRYPTO_BUFFER_len(b) &&
         memcmp(CRYPTO_BUFFER_data(a), CRYPTO_BUFFER_data(b),
                CRYPTO_BUFFER_len(a)) == 0;
}

// static
X509Certificate::OSCertHandle X509Certificate::CreateOSCertHandleFromBytes(
    const char* data,
    size_t length) {
  der::Input tbs_certificate_tlv;
  der::Input signature_algorithm_tlv;
  der::BitString signature_value;
  // Do a bare minimum of DER parsing here to make sure the input is not
  // completely crazy. (This is required for at least
  // CreateCertificateListFromBytes with FORMAT_AUTO, if not more.)
  if (!ParseCertificate(
          der::Input(reinterpret_cast<const uint8_t*>(data), length),
          &tbs_certificate_tlv, &signature_algorithm_tlv, &signature_value,
          nullptr)) {
    return nullptr;
  }

  return CRYPTO_BUFFER_new(reinterpret_cast<const uint8_t*>(data), length,
                           x509_util::GetBufferPool());
}

// static
X509Certificate::OSCertHandles X509Certificate::CreateOSCertHandlesFromBytes(
    const char* data,
    size_t length,
    Format format) {
  OSCertHandles results;

  switch (format) {
    case FORMAT_SINGLE_CERTIFICATE: {
      OSCertHandle handle = CreateOSCertHandleFromBytes(data, length);
      if (handle)
        results.push_back(handle);
      break;
    }
    case FORMAT_PKCS7: {
      CreateOSCertHandlesFromPKCS7Bytes(data, length, &results);
      break;
    }
    default: {
      NOTREACHED() << "Certificate format " << format << " unimplemented";
      break;
    }
  }

  return results;
}

// static
X509Certificate::OSCertHandle X509Certificate::DupOSCertHandle(
    OSCertHandle cert_handle) {
  CRYPTO_BUFFER_up_ref(cert_handle);
  return cert_handle;
}

// static
void X509Certificate::FreeOSCertHandle(OSCertHandle cert_handle) {
  CRYPTO_BUFFER_free(cert_handle);
}

// static
SHA256HashValue X509Certificate::CalculateFingerprint256(OSCertHandle cert) {
  SHA256HashValue sha256;

  SHA256(CRYPTO_BUFFER_data(cert), CRYPTO_BUFFER_len(cert), sha256.data);
  return sha256;
}

SHA256HashValue X509Certificate::CalculateChainFingerprint256() const {
  SHA256HashValue sha256;
  memset(sha256.data, 0, sizeof(sha256.data));

  SHA256_CTX sha256_ctx;
  SHA256_Init(&sha256_ctx);
  SHA256_Update(&sha256_ctx, CRYPTO_BUFFER_data(cert_handle_),
                CRYPTO_BUFFER_len(cert_handle_));
  for (CRYPTO_BUFFER* cert : intermediate_ca_certs_) {
    SHA256_Update(&sha256_ctx, CRYPTO_BUFFER_data(cert),
                  CRYPTO_BUFFER_len(cert));
  }
  SHA256_Final(sha256.data, &sha256_ctx);

  return sha256;
}

// static
bool X509Certificate::IsSelfSigned(OSCertHandle cert_handle) {
  der::Input tbs_certificate_tlv;
  der::Input signature_algorithm_tlv;
  der::BitString signature_value;
  if (!ParseCertificate(der::Input(CRYPTO_BUFFER_data(cert_handle),
                                   CRYPTO_BUFFER_len(cert_handle)),
                        &tbs_certificate_tlv, &signature_algorithm_tlv,
                        &signature_value, nullptr)) {
    return false;
  }
  ParsedTbsCertificate tbs;
  if (!ParseTbsCertificate(tbs_certificate_tlv,
                           x509_util::DefaultParseCertificateOptions(), &tbs,
                           nullptr)) {
    return false;
  }

  der::Input subject_value;
  CertErrors errors;
  std::string normalized_subject;
  if (!GetSequenceValue(tbs.subject_tlv, &subject_value) ||
      !NormalizeName(subject_value, &normalized_subject, &errors)) {
    return false;
  }
  der::Input issuer_value;
  std::string normalized_issuer;
  if (!GetSequenceValue(tbs.issuer_tlv, &issuer_value) ||
      !NormalizeName(issuer_value, &normalized_issuer, &errors)) {
    return false;
  }

  if (normalized_subject != normalized_issuer)
    return false;

  std::unique_ptr<SignatureAlgorithm> signature_algorithm =
      SignatureAlgorithm::Create(signature_algorithm_tlv, nullptr /* errors */);
  if (!signature_algorithm)
    return false;

  // Don't enforce any minimum key size or restrict the algorithm, since when
  // self signed not very relevant.
  return VerifySignedData(*signature_algorithm, tbs_certificate_tlv,
                          signature_value, tbs.spki_tlv);
}

X509Certificate::X509Certificate(OSCertHandle cert_handle,
                                 const OSCertHandles& intermediates)
    : X509Certificate(cert_handle, intermediates, {}) {}

X509Certificate::X509Certificate(OSCertHandle cert_handle,
                                 const OSCertHandles& intermediates,
                                 UnsafeCreateOptions options)
    : cert_handle_(DupOSCertHandle(cert_handle)) {
  for (size_t i = 0; i < intermediates.size(); ++i) {
    // Duplicate the incoming certificate, as the caller retains ownership
    // of |intermediates|.
    intermediate_ca_certs_.push_back(DupOSCertHandle(intermediates[i]));
  }
  // Platform-specific initialization.
  if (!Initialize(options) && cert_handle_) {
    // Signal initialization failure by clearing cert_handle_.
    FreeOSCertHandle(cert_handle_);
    cert_handle_ = nullptr;
  }
}

X509Certificate::~X509Certificate() {
  if (cert_handle_)
    FreeOSCertHandle(cert_handle_);
  for (size_t i = 0; i < intermediate_ca_certs_.size(); ++i)
    FreeOSCertHandle(intermediate_ca_certs_[i]);
}

bool X509Certificate::Initialize(UnsafeCreateOptions options) {
  der::Input tbs_certificate_tlv;
  der::Input signature_algorithm_tlv;
  der::BitString signature_value;

  if (!ParseCertificate(der::Input(CRYPTO_BUFFER_data(cert_handle_),
                                   CRYPTO_BUFFER_len(cert_handle_)),
                        &tbs_certificate_tlv, &signature_algorithm_tlv,
                        &signature_value, nullptr)) {
    return false;
  }

  ParsedTbsCertificate tbs;
  if (!ParseTbsCertificate(tbs_certificate_tlv,
                           x509_util::DefaultParseCertificateOptions(), &tbs,
                           nullptr))
    return false;

  CertPrincipal::PrintableStringHandling printable_string_handling =
      options.printable_string_is_utf8
          ? CertPrincipal::PrintableStringHandling::kAsUTF8Hack
          : CertPrincipal::PrintableStringHandling::kDefault;
  if (!subject_.ParseDistinguishedName(tbs.subject_tlv.UnsafeData(),
                                       tbs.subject_tlv.Length(),
                                       printable_string_handling) ||
      !issuer_.ParseDistinguishedName(tbs.issuer_tlv.UnsafeData(),
                                      tbs.issuer_tlv.Length(),
                                      printable_string_handling)) {
    return false;
  }

  if (!GeneralizedTimeToBaseTime(tbs.validity_not_before, &valid_start_) ||
      !GeneralizedTimeToBaseTime(tbs.validity_not_after, &valid_expiry_)) {
    return false;
  }
  serial_number_ = tbs.serial_number.AsString();
  return true;
}

}  // namespace net
