// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cert.h>
#include <cryptohi.h>
#include <keyhi.h>
#include <nss.h>
#include <pk11pub.h>
#include <prtime.h>
#include <seccomon.h>
#include <secder.h>
#include <sechash.h>

#include <memory>

#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/pickle.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "crypto/nss_util.h"
#include "crypto/scoped_nss_types.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util_nss.h"

namespace net {

namespace {

// Callback for CERT_DecodeCertPackage(), used in
// CreateOSCertHandlesFromBytes().
SECStatus PR_CALLBACK CollectCertsCallback(void* arg,
                                           SECItem** certs,
                                           int num_certs) {
  X509Certificate::OSCertHandles* results =
      reinterpret_cast<X509Certificate::OSCertHandles*>(arg);

  for (int i = 0; i < num_certs; ++i) {
    X509Certificate::OSCertHandle handle =
        X509Certificate::CreateOSCertHandleFromBytes(
            reinterpret_cast<char*>(certs[i]->data), certs[i]->len);
    if (handle)
      results->push_back(handle);
  }

  return SECSuccess;
}

// Parses the Principal attribute from |name| and outputs the result in
// |principal|. Returns true on success.
bool ParsePrincipal(CERTName* name, CertPrincipal* principal) {
  typedef char* (*CERTGetNameFunc)(const CERTName* name);

  // TODO(jcampan): add business_category and serial_number.
  // TODO(wtc): NSS has the CERT_GetOrgName, CERT_GetOrgUnitName, and
  // CERT_GetDomainComponentName functions, but they return only the most
  // general (the first) RDN.  NSS doesn't have a function for the street
  // address.
  static const SECOidTag kOIDs[] = {
      SEC_OID_AVA_STREET_ADDRESS, SEC_OID_AVA_ORGANIZATION_NAME,
      SEC_OID_AVA_ORGANIZATIONAL_UNIT_NAME, SEC_OID_AVA_DC};

  std::vector<std::string>* values[] = {
      &principal->street_addresses, &principal->organization_names,
      &principal->organization_unit_names, &principal->domain_components};
  DCHECK_EQ(arraysize(kOIDs), arraysize(values));

  CERTRDN** rdns = name->rdns;
  for (size_t rdn = 0; rdns[rdn]; ++rdn) {
    CERTAVA** avas = rdns[rdn]->avas;
    for (size_t pair = 0; avas[pair] != 0; ++pair) {
      SECOidTag tag = CERT_GetAVATag(avas[pair]);
      for (size_t oid = 0; oid < arraysize(kOIDs); ++oid) {
        if (kOIDs[oid] == tag) {
          SECItem* decode_item = CERT_DecodeAVAValue(&avas[pair]->value);
          if (!decode_item)
            return false;
          // TODO(wtc): Pass decode_item to CERT_RFC1485_EscapeAndQuote.
          std::string value(reinterpret_cast<char*>(decode_item->data),
                            decode_item->len);
          values[oid]->push_back(value);
          SECITEM_FreeItem(decode_item, PR_TRUE);
          break;
        }
      }
    }
  }

  // Get CN, L, S, and C.
  CERTGetNameFunc get_name_funcs[4] = {CERT_GetCommonName, CERT_GetLocalityName,
                                       CERT_GetStateName, CERT_GetCountryName};
  std::string* single_values[4] = {
      &principal->common_name, &principal->locality_name,
      &principal->state_or_province_name, &principal->country_name};
  for (size_t i = 0; i < arraysize(get_name_funcs); ++i) {
    char* value = get_name_funcs[i](name);
    if (value) {
      single_values[i]->assign(value);
      PORT_Free(value);
    }
  }

  return true;
}

// Parses the date from |der_date| and outputs the result in |result|.
// Returns true on success.
bool ParseDate(const SECItem* der_date, base::Time* result) {
  PRTime prtime;
  SECStatus rv = DER_DecodeTimeChoice(&prtime, der_date);
  if (rv != SECSuccess)
    return false;
  *result = crypto::PRTimeToBaseTime(prtime);
  return true;
}

// Parses the serial number from |certificate|.
std::string ParseSerialNumber(const CERTCertificate* certificate) {
  return std::string(reinterpret_cast<char*>(certificate->serialNumber.data),
                     certificate->serialNumber.len);
}

typedef std::unique_ptr<CERTName,
                        crypto::NSSDestroyer<CERTName, CERT_DestroyName>>
    ScopedCERTName;

// Create a new CERTName object from its encoded representation.
// |arena| is the allocation pool to use.
// |data| points to a DER-encoded X.509 DistinguishedName.
// Return a new CERTName pointer on success, or NULL.
CERTName* CreateCertNameFromEncoded(PLArenaPool* arena,
                                    const base::StringPiece& data) {
  if (!arena)
    return NULL;

  ScopedCERTName name(PORT_ArenaZNew(arena, CERTName));
  if (!name.get())
    return NULL;

  SECItem item;
  item.len = static_cast<unsigned int>(data.length());
  item.data = reinterpret_cast<unsigned char*>(const_cast<char*>(data.data()));

  SECStatus rv = SEC_ASN1DecodeItem(arena, name.get(),
                                    SEC_ASN1_GET(CERT_NameTemplate), &item);
  if (rv != SECSuccess)
    return NULL;

  return name.release();
}

// Create a list of CERTName objects from a list of DER-encoded X.509
// DistinguishedName items. All objects are created in a given arena.
// |encoded_issuers| is the list of encoded DNs.
// |arena| is the arena used for all allocations.
// |out| will receive the result list on success.
// Return true on success. On failure, the caller must free the
// intermediate CERTName objects pushed to |out|.
bool GetIssuersFromEncodedList(const std::vector<std::string>& encoded_issuers,
                               PLArenaPool* arena,
                               std::vector<CERTName*>* out) {
  std::vector<CERTName*> result;
  for (size_t n = 0; n < encoded_issuers.size(); ++n) {
    CERTName* name = CreateCertNameFromEncoded(arena, encoded_issuers[n]);
    if (name != NULL)
      result.push_back(name);
  }

  if (result.size() == encoded_issuers.size()) {
    out->swap(result);
    return true;
  }

  for (size_t n = 0; n < result.size(); ++n)
    CERT_DestroyName(result[n]);
  return false;
}

// Returns true iff a certificate is issued by any of the issuers listed
// by name in |valid_issuers|.
// |cert_chain| is the certificate's chain.
// |valid_issuers| is a list of strings, where each string contains
// a DER-encoded X.509 Distinguished Name.
bool IsCertificateIssuedBy(const std::vector<CERTCertificate*>& cert_chain,
                           const std::vector<CERTName*>& valid_issuers) {
  for (size_t n = 0; n < cert_chain.size(); ++n) {
    CERTName* cert_issuer = &cert_chain[n]->issuer;
    for (size_t i = 0; i < valid_issuers.size(); ++i) {
      if (CERT_CompareName(valid_issuers[i], cert_issuer) == SECEqual)
        return true;
    }
  }
  return false;
}

}  // namespace

bool X509Certificate::Initialize(UnsafeCreateOptions) {
  serial_number_ = ParseSerialNumber(cert_handle_);

  return (!serial_number_.empty() &&
          ParsePrincipal(&cert_handle_->subject, &subject_) &&
          ParsePrincipal(&cert_handle_->issuer, &issuer_) &&
          ParseDate(&cert_handle_->validity.notBefore, &valid_start_) &&
          ParseDate(&cert_handle_->validity.notAfter, &valid_expiry_));
}

bool X509Certificate::GetSubjectAltName(
    std::vector<std::string>* dns_names,
    std::vector<std::string>* ip_addrs) const {
  if (dns_names)
    dns_names->clear();
  if (ip_addrs)
    ip_addrs->clear();

  SECItem alt_name;
  SECStatus rv = CERT_FindCertExtension(
      cert_handle_, SEC_OID_X509_SUBJECT_ALT_NAME, &alt_name);
  if (rv != SECSuccess)
    return false;

  crypto::ScopedPLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));

  CERTGeneralName* alt_name_list;
  alt_name_list = CERT_DecodeAltNameExtension(arena.get(), &alt_name);
  SECITEM_FreeItem(&alt_name, PR_FALSE);

  bool has_san = false;
  CERTGeneralName* name = alt_name_list;
  while (name) {
    // DNSName and IPAddress are encoded as IA5String and OCTET STRINGs
    // respectively, both of which can be byte copied from
    // SECItemType::data into the appropriate output vector.
    if (name->type == certDNSName) {
      has_san = true;
      if (dns_names) {
        dns_names->push_back(
            std::string(reinterpret_cast<char*>(name->name.other.data),
                        name->name.other.len));
      }
    } else if (name->type == certIPAddress) {
      has_san = true;
      if (ip_addrs) {
        ip_addrs->push_back(
            std::string(reinterpret_cast<char*>(name->name.other.data),
                        name->name.other.len));
      }
    }
    // Fast path: Found at least one subjectAltName and the caller doesn't
    // need the actual values.
    if (has_san && !ip_addrs && !dns_names)
      return true;

    name = CERT_GetNextGeneralName(name);
    if (name == alt_name_list)
      break;
  }
  return has_san;
}

bool X509Certificate::IsIssuedByEncoded(
    const std::vector<std::string>& valid_issuers) {
  // Get certificate chain as scoped list of CERTCertificate objects.
  std::vector<CERTCertificate*> cert_chain;
  cert_chain.push_back(cert_handle_);
  for (size_t n = 0; n < intermediate_ca_certs_.size(); ++n) {
    cert_chain.push_back(intermediate_ca_certs_[n]);
  }
  // Convert encoded issuers to scoped CERTName* list.
  std::vector<CERTName*> issuers;
  crypto::ScopedPLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!GetIssuersFromEncodedList(valid_issuers, arena.get(), &issuers)) {
    return false;
  }
  return IsCertificateIssuedBy(cert_chain, issuers);
}

// static
bool X509Certificate::GetDEREncoded(X509Certificate::OSCertHandle cert_handle,
                                    std::string* encoded) {
  return x509_util::GetDEREncoded(cert_handle, encoded);
}

// static
bool X509Certificate::IsSameOSCert(X509Certificate::OSCertHandle a,
                                   X509Certificate::OSCertHandle b) {
  return x509_util::IsSameCertificate(a, b);
}

// static
X509Certificate::OSCertHandle X509Certificate::CreateOSCertHandleFromBytes(
    const char* data,
    size_t length) {
  return x509_util::CreateCERTCertificateFromBytes(
             reinterpret_cast<const uint8_t*>(data), length)
      .release();
}

// static
X509Certificate::OSCertHandles X509Certificate::CreateOSCertHandlesFromBytes(
    const char* data,
    size_t length,
    Format format) {
  X509Certificate::OSCertHandles results;

  crypto::EnsureNSSInit();

  if (!NSS_IsInitialized())
    return results;

  switch (format) {
    case X509Certificate::FORMAT_SINGLE_CERTIFICATE: {
      X509Certificate::OSCertHandle handle =
          X509Certificate::CreateOSCertHandleFromBytes(data, length);
      if (handle)
        results.push_back(handle);
      break;
    }
    case X509Certificate::FORMAT_PKCS7: {
      // Make a copy since CERT_DecodeCertPackage may modify it
      std::vector<char> data_copy(data, data + length);

      SECStatus result = CERT_DecodeCertPackage(
          data_copy.data(), base::checked_cast<int>(data_copy.size()),
          CollectCertsCallback, &results);
      if (result != SECSuccess)
        results.clear();
      break;
    }
    default:
      NOTREACHED() << "Certificate format " << format << " unimplemented";
      break;
  }

  return results;
}

// static
X509Certificate::OSCertHandle X509Certificate::DupOSCertHandle(
    OSCertHandle cert_handle) {
  return CERT_DupCertificate(cert_handle);
}

// static
void X509Certificate::FreeOSCertHandle(OSCertHandle cert_handle) {
  CERT_DestroyCertificate(cert_handle);
}

// static
SHA256HashValue X509Certificate::CalculateFingerprint256(OSCertHandle cert) {
  return x509_util::CalculateFingerprint256(cert);
}

// static
SHA256HashValue X509Certificate::CalculateCAFingerprint256(
    const OSCertHandles& intermediates) {
  SHA256HashValue sha256;
  memset(sha256.data, 0, sizeof(sha256.data));

  HASHContext* sha256_ctx = HASH_Create(HASH_AlgSHA256);
  if (!sha256_ctx)
    return sha256;
  HASH_Begin(sha256_ctx);
  for (size_t i = 0; i < intermediates.size(); ++i) {
    CERTCertificate* ca_cert = intermediates[i];
    HASH_Update(sha256_ctx, ca_cert->derCert.data, ca_cert->derCert.len);
  }
  unsigned int result_len;
  HASH_End(sha256_ctx, sha256.data, &result_len,
           HASH_ResultLenContext(sha256_ctx));
  HASH_Destroy(sha256_ctx);

  return sha256;
}

// static
X509Certificate::OSCertHandle X509Certificate::ReadOSCertHandleFromPickle(
    base::PickleIterator* pickle_iter) {
  const char* data;
  int length;
  if (!pickle_iter->ReadData(&data, &length))
    return NULL;

  return CreateOSCertHandleFromBytes(data, length);
}

// static
void X509Certificate::WriteOSCertHandleToPickle(OSCertHandle cert_handle,
                                                base::Pickle* pickle) {
  pickle->WriteData(reinterpret_cast<const char*>(cert_handle->derCert.data),
                    cert_handle->derCert.len);
}

// static
void X509Certificate::GetPublicKeyInfo(OSCertHandle cert_handle,
                                       size_t* size_bits,
                                       PublicKeyType* type) {
  // Since we might fail, set the output parameters to default values first.
  *type = X509Certificate::kPublicKeyTypeUnknown;
  *size_bits = 0;

  crypto::ScopedSECKEYPublicKey key(CERT_ExtractPublicKey(cert_handle));
  if (!key.get())
    return;

  *size_bits = SECKEY_PublicKeyStrengthInBits(key.get());

  switch (key->keyType) {
    case rsaKey:
      *type = X509Certificate::kPublicKeyTypeRSA;
      break;
    case dsaKey:
      *type = X509Certificate::kPublicKeyTypeDSA;
      break;
    case dhKey:
      *type = X509Certificate::kPublicKeyTypeDH;
      break;
    case ecKey:
      *type = X509Certificate::kPublicKeyTypeECDSA;
      break;
    default:
      *type = X509Certificate::kPublicKeyTypeUnknown;
      *size_bits = 0;
      break;
  }
}

// static
bool X509Certificate::IsSelfSigned(OSCertHandle cert_handle) {
  crypto::ScopedSECKEYPublicKey public_key(CERT_ExtractPublicKey(cert_handle));
  if (!public_key.get())
    return false;
  if (SECSuccess != CERT_VerifySignedDataWithPublicKey(
                        &cert_handle->signatureWrap, public_key.get(), NULL)) {
    return false;
  }
  return CERT_CompareName(&cert_handle->subject, &cert_handle->issuer) ==
         SECEqual;
}

}  // namespace net
