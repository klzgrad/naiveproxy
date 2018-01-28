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
#include "base/pickle.h"
#include "base/stl_util.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "net/cert/pem_tokenizer.h"
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
    base::PickleIterator* pickle_iter,
    PickleType type) {
  if (type == PICKLETYPE_CERTIFICATE_CHAIN_V3) {
    int chain_length = 0;
    if (!pickle_iter->ReadLength(&chain_length))
      return NULL;

    std::vector<base::StringPiece> cert_chain;
    const char* data = NULL;
    int data_length = 0;
    for (int i = 0; i < chain_length; ++i) {
      if (!pickle_iter->ReadData(&data, &data_length))
        return NULL;
      cert_chain.push_back(base::StringPiece(data, data_length));
    }
    return CreateFromDERCertChain(cert_chain);
  }

  // Legacy / Migration code. This should eventually be removed once
  // sufficient time has passed that all pickles serialized prior to
  // PICKLETYPE_CERTIFICATE_CHAIN_V3 have been removed.
  OSCertHandle cert_handle = ReadOSCertHandleFromPickle(pickle_iter);
  if (!cert_handle)
    return NULL;

  OSCertHandles intermediates;
  uint32_t num_intermediates = 0;
  if (type != PICKLETYPE_SINGLE_CERTIFICATE) {
    if (!pickle_iter->ReadUInt32(&num_intermediates)) {
      FreeOSCertHandle(cert_handle);
      return NULL;
    }

#if defined(OS_POSIX) && !defined(OS_MACOSX) && defined(__x86_64__)
    // On 64-bit Linux (and any other 64-bit platforms), the intermediate count
    // might really be a 64-bit field since we used to use Pickle::WriteSize(),
    // which writes either 32 or 64 bits depending on the architecture. Since
    // x86-64 is little-endian, if that happens, the next 32 bits will be all
    // zeroes (the high bits) and the 32 bits we already read above are the
    // correct value (we assume there are never more than 2^32 - 1 intermediate
    // certificates in a chain; in practice, more than a dozen or so is
    // basically unheard of). Since it's invalid for a certificate to start with
    // 32 bits of zeroes, we check for that here and skip it if we find it. We
    // save a copy of the pickle iterator to restore in case we don't get 32
    // bits of zeroes. Now we always write 32 bits, so after a while, these old
    // cached pickles will all get replaced.
    // TODO(mdm): remove this compatibility code in April 2013 or so.
    base::PickleIterator saved_iter = *pickle_iter;
    uint32_t zero_check = 0;
    if (!pickle_iter->ReadUInt32(&zero_check)) {
      // This may not be an error. If there are no intermediates, and we're
      // reading an old 32-bit pickle, and there's nothing else after this in
      // the pickle, we should report success. Note that it is technically
      // possible for us to skip over zeroes that should have occurred after
      // an empty certificate list; to avoid this going forward, only do this
      // backward-compatibility stuff for PICKLETYPE_CERTIFICATE_CHAIN_V1
      // which comes from the pickle version number in http_response_info.cc.
      if (num_intermediates) {
        FreeOSCertHandle(cert_handle);
        return NULL;
      }
    }
    if (zero_check)
      *pickle_iter = saved_iter;
#endif  // defined(OS_POSIX) && !defined(OS_MACOSX) && defined(__x86_64__)

    for (uint32_t i = 0; i < num_intermediates; ++i) {
      OSCertHandle intermediate = ReadOSCertHandleFromPickle(pickle_iter);
      if (!intermediate)
        break;
      intermediates.push_back(intermediate);
    }
  }

  scoped_refptr<X509Certificate> cert = nullptr;
  if (intermediates.size() == num_intermediates)
    cert = CreateFromHandle(cert_handle, intermediates);
  FreeOSCertHandle(cert_handle);
  for (size_t i = 0; i < intermediates.size(); ++i)
    FreeOSCertHandle(intermediates[i]);

  return cert;
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
  WriteOSCertHandleToPickle(cert_handle_, pickle);
  for (size_t i = 0; i < intermediate_ca_certs_.size(); ++i)
    WriteOSCertHandleToPickle(intermediate_ca_certs_[i], pickle);
}

void X509Certificate::GetDNSNames(std::vector<std::string>* dns_names) const {
  GetSubjectAltName(dns_names, NULL);
  if (dns_names->empty())
    dns_names->push_back(subject_.common_name);
}

bool X509Certificate::HasExpired() const {
  return base::Time::Now() > valid_expiry();
}

bool X509Certificate::Equals(const X509Certificate* other) const {
  return IsSameOSCert(cert_handle_, other->cert_handle_);
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
SHA256HashValue X509Certificate::CalculateChainFingerprint256(
    OSCertHandle leaf,
    const OSCertHandles& intermediates) {
  OSCertHandles chain;
  chain.push_back(leaf);
  chain.insert(chain.end(), intermediates.begin(), intermediates.end());

  return CalculateCAFingerprint256(chain);
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

}  // namespace net
