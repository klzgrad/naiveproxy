// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/certificate_view.h"

#include <cstdint>
#include <memory>

#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/ec.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/nid.h"
#include "net/third_party/quiche/src/quic/core/crypto/boring_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

// The literals below were encoded using `ascii2der | xxd -i`.  The comments
// above the literals are the contents in the der2ascii syntax.
namespace {

// X.509 version 3 (version numbering starts with zero).
// INTEGER { 2 }
constexpr uint8_t kX509Version[] = {0x02, 0x01, 0x02};

// 2.5.29.17
constexpr uint8_t kSubjectAltNameOid[] = {0x55, 0x1d, 0x11};

}  // namespace

namespace quic {

std::unique_ptr<CertificateView> CertificateView::ParseSingleCertificate(
    quiche::QuicheStringPiece certificate) {
  std::unique_ptr<CertificateView> result(new CertificateView());
  CBS top = StringPieceToCbs(certificate);

  CBS top_certificate, tbs_certificate, signature_algorithm, signature;
  if (!CBS_get_asn1(&top, &top_certificate, CBS_ASN1_SEQUENCE) ||
      CBS_len(&top) != 0) {
    return nullptr;
  }

  // Certificate  ::=  SEQUENCE  {
  if (
      //   tbsCertificate       TBSCertificate,
      !CBS_get_asn1(&top_certificate, &tbs_certificate, CBS_ASN1_SEQUENCE) ||

      //   signatureAlgorithm   AlgorithmIdentifier,
      !CBS_get_asn1(&top_certificate, &signature_algorithm,
                    CBS_ASN1_SEQUENCE) ||

      //   signature            BIT STRING  }
      !CBS_get_asn1(&top_certificate, &signature, CBS_ASN1_BITSTRING) ||
      CBS_len(&top_certificate) != 0) {
    return nullptr;
  }

  int has_version, has_extensions;
  CBS version, serial, signature_algorithm_inner, issuer, validity, subject,
      spki, issuer_id, subject_id, extensions_outer;
  // TBSCertificate  ::=  SEQUENCE  {
  if (
      //   version         [0]  Version DEFAULT v1,
      !CBS_get_optional_asn1(
          &tbs_certificate, &version, &has_version,
          CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 0) ||

      //   serialNumber         CertificateSerialNumber,
      !CBS_get_asn1(&tbs_certificate, &serial, CBS_ASN1_INTEGER) ||

      //   signature            AlgorithmIdentifier,
      !CBS_get_asn1(&tbs_certificate, &signature_algorithm_inner,
                    CBS_ASN1_SEQUENCE) ||

      //   issuer               Name,
      !CBS_get_asn1(&tbs_certificate, &issuer, CBS_ASN1_SEQUENCE) ||

      //   validity             Validity,
      !CBS_get_asn1(&tbs_certificate, &validity, CBS_ASN1_SEQUENCE) ||

      //   subject              Name,
      !CBS_get_asn1(&tbs_certificate, &subject, CBS_ASN1_SEQUENCE) ||

      //   subjectPublicKeyInfo SubjectPublicKeyInfo,
      !CBS_get_asn1_element(&tbs_certificate, &spki, CBS_ASN1_SEQUENCE) ||

      //   issuerUniqueID  [1]  IMPLICIT UniqueIdentifier OPTIONAL,
      //                        -- If present, version MUST be v2 or v3
      !CBS_get_optional_asn1(&tbs_certificate, &issuer_id, nullptr,
                             CBS_ASN1_CONTEXT_SPECIFIC | 1) ||

      //   subjectUniqueID [2]  IMPLICIT UniqueIdentifier OPTIONAL,
      //                        -- If present, version MUST be v2 or v3
      !CBS_get_optional_asn1(&tbs_certificate, &subject_id, nullptr,
                             CBS_ASN1_CONTEXT_SPECIFIC | 2) ||

      //   extensions      [3]  Extensions OPTIONAL
      //                        -- If present, version MUST be v3 --  }
      !CBS_get_optional_asn1(
          &tbs_certificate, &extensions_outer, &has_extensions,
          CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 3) ||

      CBS_len(&tbs_certificate) != 0) {
    return nullptr;
  }

  result->public_key_.reset(EVP_parse_public_key(&spki));
  if (result->public_key_ == nullptr) {
    QUIC_DLOG(WARNING) << "Failed to parse the public key";
    return nullptr;
  }
  if (!result->ValidatePublicKeyParameters()) {
    QUIC_DLOG(WARNING) << "Public key has invalid parameters";
    return nullptr;
  }

  // Only support X.509v3.
  if (!has_version ||
      !CBS_mem_equal(&version, kX509Version, sizeof(kX509Version))) {
    QUIC_DLOG(WARNING) << "Bad X.509 version";
    return nullptr;
  }

  if (!has_extensions) {
    return nullptr;
  }

  CBS extensions;
  if (!CBS_get_asn1(&extensions_outer, &extensions, CBS_ASN1_SEQUENCE) ||
      CBS_len(&extensions_outer) != 0) {
    QUIC_DLOG(WARNING) << "Failed to extract the extension sequence";
    return nullptr;
  }
  if (!result->ParseExtensions(extensions)) {
    QUIC_DLOG(WARNING) << "Failed to parse extensions";
    return nullptr;
  }

  return result;
}

bool CertificateView::ParseExtensions(CBS extensions) {
  while (CBS_len(&extensions) != 0) {
    CBS extension, oid, critical, payload;
    if (
        // Extension  ::=  SEQUENCE  {
        !CBS_get_asn1(&extensions, &extension, CBS_ASN1_SEQUENCE) ||
        //     extnID      OBJECT IDENTIFIER,
        !CBS_get_asn1(&extension, &oid, CBS_ASN1_OBJECT) ||
        //     critical    BOOLEAN DEFAULT FALSE,
        !CBS_get_optional_asn1(&extension, &critical, nullptr,
                               CBS_ASN1_BOOLEAN) ||
        //     extnValue   OCTET STRING
        //                 -- contains the DER encoding of an ASN.1 value
        //                 -- corresponding to the extension type identified
        //                 -- by extnID
        !CBS_get_asn1(&extension, &payload, CBS_ASN1_OCTETSTRING) ||
        CBS_len(&extension) != 0) {
      QUIC_DLOG(WARNING) << "Bad extension entry";
      return false;
    }

    if (CBS_mem_equal(&oid, kSubjectAltNameOid, sizeof(kSubjectAltNameOid))) {
      CBS alt_names;
      if (!CBS_get_asn1(&payload, &alt_names, CBS_ASN1_SEQUENCE) ||
          CBS_len(&payload) != 0) {
        QUIC_DLOG(WARNING) << "Failed to parse subjectAltName";
        return false;
      }
      while (CBS_len(&alt_names) != 0) {
        CBS alt_name_cbs;
        unsigned int alt_name_tag;
        if (!CBS_get_any_asn1(&alt_names, &alt_name_cbs, &alt_name_tag)) {
          QUIC_DLOG(WARNING) << "Failed to parse subjectAltName";
          return false;
        }

        quiche::QuicheStringPiece alt_name = CbsToStringPiece(alt_name_cbs);
        QuicIpAddress ip_address;
        // GeneralName ::= CHOICE {
        switch (alt_name_tag) {
          // dNSName                   [2]  IA5String,
          case CBS_ASN1_CONTEXT_SPECIFIC | 2:
            subject_alt_name_domains_.push_back(alt_name);
            break;

          // iPAddress                 [7]  OCTET STRING,
          case CBS_ASN1_CONTEXT_SPECIFIC | 7:
            if (!ip_address.FromPackedString(alt_name.data(),
                                             alt_name.size())) {
              QUIC_DLOG(WARNING) << "Failed to parse subjectAltName IP address";
              return false;
            }
            subject_alt_name_ips_.push_back(ip_address);
            break;

          default:
            QUIC_DLOG(WARNING) << "Invalid subjectAltName tag";
            return false;
        }
      }
    }
  }

  return true;
}

bool CertificateView::ValidatePublicKeyParameters() {
  // The profile here affects what certificates can be used:
  // (1) when QUIC is used as a server library without any custom certificate
  //     provider logic,
  // (2) when QuicTransport is handling self-signed certificates.
  // The goal is to allow at minimum any certificate that would be allowed on a
  // regular Web session over TLS 1.3 while ensuring we do not expose any
  // algorithms we don't want to support long-term.
  switch (EVP_PKEY_id(public_key_.get())) {
    case EVP_PKEY_RSA:
      return EVP_PKEY_bits(public_key_.get()) >= 2048;
    case EVP_PKEY_EC: {
      const EC_KEY* key = EVP_PKEY_get0_EC_KEY(public_key_.get());
      if (key == nullptr) {
        return false;
      }
      const EC_GROUP* group = EC_KEY_get0_group(key);
      if (group == nullptr) {
        return false;
      }
      const int curve_nid = EC_GROUP_get_curve_name(group);
      switch (curve_nid) {
        case NID_X9_62_prime256v1:
        case NID_secp384r1:
          return true;
        default:
          return false;
      }
    }
    case EVP_PKEY_ED25519:
      return true;
    default:
      return false;
  }
}

}  // namespace quic
