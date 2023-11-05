// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/certificate_view.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "openssl/base.h"
#include "openssl/bytestring.h"
#include "openssl/digest.h"
#include "openssl/ec.h"
#include "openssl/ec_key.h"
#include "openssl/evp.h"
#include "openssl/nid.h"
#include "openssl/rsa.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/boring_utils.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_time_utils.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {
namespace {

using ::quiche::QuicheTextUtils;

// The literals below were encoded using `ascii2der | xxd -i`.  The comments
// above the literals are the contents in the der2ascii syntax.

// X.509 version 3 (version numbering starts with zero).
// INTEGER { 2 }
constexpr uint8_t kX509Version[] = {0x02, 0x01, 0x02};

// 2.5.29.17
constexpr uint8_t kSubjectAltNameOid[] = {0x55, 0x1d, 0x11};

PublicKeyType PublicKeyTypeFromKey(EVP_PKEY* public_key) {
  switch (EVP_PKEY_id(public_key)) {
    case EVP_PKEY_RSA:
      return PublicKeyType::kRsa;
    case EVP_PKEY_EC: {
      const EC_KEY* key = EVP_PKEY_get0_EC_KEY(public_key);
      if (key == nullptr) {
        return PublicKeyType::kUnknown;
      }
      const EC_GROUP* group = EC_KEY_get0_group(key);
      if (group == nullptr) {
        return PublicKeyType::kUnknown;
      }
      const int curve_nid = EC_GROUP_get_curve_name(group);
      switch (curve_nid) {
        case NID_X9_62_prime256v1:
          return PublicKeyType::kP256;
        case NID_secp384r1:
          return PublicKeyType::kP384;
        default:
          return PublicKeyType::kUnknown;
      }
    }
    case EVP_PKEY_ED25519:
      return PublicKeyType::kEd25519;
    default:
      return PublicKeyType::kUnknown;
  }
}

}  // namespace

PublicKeyType PublicKeyTypeFromSignatureAlgorithm(
    uint16_t signature_algorithm) {
  // This should be kept in sync with the list in
  // SupportedSignatureAlgorithmsForQuic().
  switch (signature_algorithm) {
    case SSL_SIGN_RSA_PSS_RSAE_SHA256:
      return PublicKeyType::kRsa;
    case SSL_SIGN_ECDSA_SECP256R1_SHA256:
      return PublicKeyType::kP256;
    case SSL_SIGN_ECDSA_SECP384R1_SHA384:
      return PublicKeyType::kP384;
    case SSL_SIGN_ED25519:
      return PublicKeyType::kEd25519;
    default:
      return PublicKeyType::kUnknown;
  }
}

QUICHE_EXPORT QuicSignatureAlgorithmVector
SupportedSignatureAlgorithmsForQuic() {
  // This should be kept in sync with the list in
  // PublicKeyTypeFromSignatureAlgorithm().
  return QuicSignatureAlgorithmVector{
      SSL_SIGN_ED25519, SSL_SIGN_ECDSA_SECP256R1_SHA256,
      SSL_SIGN_ECDSA_SECP384R1_SHA384, SSL_SIGN_RSA_PSS_RSAE_SHA256};
}

namespace {

std::string AttributeNameToString(const CBS& oid_cbs) {
  absl::string_view oid = CbsToStringPiece(oid_cbs);

  // We only handle OIDs of form 2.5.4.N, which have binary encoding of
  // "55 04 0N".
  if (oid.length() == 3 && absl::StartsWith(oid, "\x55\x04")) {
    // clang-format off
    switch (oid[2]) {
      case '\x3': return "CN";
      case '\x7': return "L";
      case '\x8': return "ST";
      case '\xa': return "O";
      case '\xb': return "OU";
      case '\x6': return "C";
    }
    // clang-format on
  }

  bssl::UniquePtr<char> oid_representation(CBS_asn1_oid_to_text(&oid_cbs));
  if (oid_representation == nullptr) {
    return absl::StrCat("(", absl::BytesToHexString(oid), ")");
  }
  return std::string(oid_representation.get());
}

}  // namespace

absl::optional<std::string> X509NameAttributeToString(CBS input) {
  CBS name, value;
  unsigned value_tag;
  if (!CBS_get_asn1(&input, &name, CBS_ASN1_OBJECT) ||
      !CBS_get_any_asn1(&input, &value, &value_tag) || CBS_len(&input) != 0) {
    return absl::nullopt;
  }
  // Note that this does not process encoding of |input| in any way.  This works
  // fine for the most cases.
  return absl::StrCat(AttributeNameToString(name), "=",
                      absl::CHexEscape(CbsToStringPiece(value)));
}

namespace {

template <unsigned inner_tag, char separator,
          absl::optional<std::string> (*parser)(CBS)>
absl::optional<std::string> ParseAndJoin(CBS input) {
  std::vector<std::string> pieces;
  while (CBS_len(&input) != 0) {
    CBS attribute;
    if (!CBS_get_asn1(&input, &attribute, inner_tag)) {
      return absl::nullopt;
    }
    absl::optional<std::string> formatted = parser(attribute);
    if (!formatted.has_value()) {
      return absl::nullopt;
    }
    pieces.push_back(*formatted);
  }

  return absl::StrJoin(pieces, std::string({separator}));
}

absl::optional<std::string> RelativeDistinguishedNameToString(CBS input) {
  return ParseAndJoin<CBS_ASN1_SEQUENCE, '+', X509NameAttributeToString>(input);
}

absl::optional<std::string> DistinguishedNameToString(CBS input) {
  return ParseAndJoin<CBS_ASN1_SET, ',', RelativeDistinguishedNameToString>(
      input);
}

}  // namespace

std::string PublicKeyTypeToString(PublicKeyType type) {
  switch (type) {
    case PublicKeyType::kRsa:
      return "RSA";
    case PublicKeyType::kP256:
      return "ECDSA P-256";
    case PublicKeyType::kP384:
      return "ECDSA P-384";
    case PublicKeyType::kEd25519:
      return "Ed25519";
    case PublicKeyType::kUnknown:
      return "unknown";
  }
  return "";
}

absl::optional<quic::QuicWallTime> ParseDerTime(unsigned tag,
                                                absl::string_view payload) {
  if (tag != CBS_ASN1_GENERALIZEDTIME && tag != CBS_ASN1_UTCTIME) {
    QUIC_DLOG(WARNING) << "Invalid tag supplied for a DER timestamp";
    return absl::nullopt;
  }

  const size_t year_length = tag == CBS_ASN1_GENERALIZEDTIME ? 4 : 2;
  uint64_t year, month, day, hour, minute, second;
  quiche::QuicheDataReader reader(payload);
  if (!reader.ReadDecimal64(year_length, &year) ||
      !reader.ReadDecimal64(2, &month) || !reader.ReadDecimal64(2, &day) ||
      !reader.ReadDecimal64(2, &hour) || !reader.ReadDecimal64(2, &minute) ||
      !reader.ReadDecimal64(2, &second) ||
      reader.ReadRemainingPayload() != "Z") {
    QUIC_DLOG(WARNING) << "Failed to parse the DER timestamp";
    return absl::nullopt;
  }

  if (tag == CBS_ASN1_UTCTIME) {
    QUICHE_DCHECK_LE(year, 100u);
    year += (year >= 50) ? 1900 : 2000;
  }

  const absl::optional<int64_t> unix_time =
      quiche::QuicheUtcDateTimeToUnixSeconds(year, month, day, hour, minute,
                                             second);
  if (!unix_time.has_value() || *unix_time < 0) {
    return absl::nullopt;
  }
  return QuicWallTime::FromUNIXSeconds(*unix_time);
}

PemReadResult ReadNextPemMessage(std::istream* input) {
  constexpr absl::string_view kPemBegin = "-----BEGIN ";
  constexpr absl::string_view kPemEnd = "-----END ";
  constexpr absl::string_view kPemDashes = "-----";

  std::string line_buffer, encoded_message_contents, expected_end;
  bool pending_message = false;
  PemReadResult result;
  while (std::getline(*input, line_buffer)) {
    absl::string_view line(line_buffer);
    QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&line);

    // Handle BEGIN lines.
    if (!pending_message && absl::StartsWith(line, kPemBegin) &&
        absl::EndsWith(line, kPemDashes)) {
      result.type = std::string(
          line.substr(kPemBegin.size(),
                      line.size() - kPemDashes.size() - kPemBegin.size()));
      expected_end = absl::StrCat(kPemEnd, result.type, kPemDashes);
      pending_message = true;
      continue;
    }

    // Handle END lines.
    if (pending_message && line == expected_end) {
      absl::optional<std::string> data =
          QuicheTextUtils::Base64Decode(encoded_message_contents);
      if (data.has_value()) {
        result.status = PemReadResult::kOk;
        result.contents = data.value();
      } else {
        result.status = PemReadResult::kError;
      }
      return result;
    }

    if (pending_message) {
      encoded_message_contents.append(std::string(line));
    }
  }
  bool eof_reached = input->eof() && !pending_message;
  return PemReadResult{
      (eof_reached ? PemReadResult::kEof : PemReadResult::kError), "", ""};
}

std::unique_ptr<CertificateView> CertificateView::ParseSingleCertificate(
    absl::string_view certificate) {
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

  result->subject_der_ = CbsToStringPiece(subject);

  unsigned not_before_tag, not_after_tag;
  CBS not_before, not_after;
  if (!CBS_get_any_asn1(&validity, &not_before, &not_before_tag) ||
      !CBS_get_any_asn1(&validity, &not_after, &not_after_tag) ||
      CBS_len(&validity) != 0) {
    QUIC_DLOG(WARNING) << "Failed to extract the validity dates";
    return nullptr;
  }
  absl::optional<QuicWallTime> not_before_parsed =
      ParseDerTime(not_before_tag, CbsToStringPiece(not_before));
  absl::optional<QuicWallTime> not_after_parsed =
      ParseDerTime(not_after_tag, CbsToStringPiece(not_after));
  if (!not_before_parsed.has_value() || !not_after_parsed.has_value()) {
    QUIC_DLOG(WARNING) << "Failed to parse validity dates";
    return nullptr;
  }
  result->validity_start_ = *not_before_parsed;
  result->validity_end_ = *not_after_parsed;

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

        absl::string_view alt_name = CbsToStringPiece(alt_name_cbs);
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
            QUIC_DLOG(INFO) << "Unknown subjectAltName tag " << alt_name_tag;
            continue;
        }
      }
    }
  }

  return true;
}

std::vector<std::string> CertificateView::LoadPemFromStream(
    std::istream* input) {
  std::vector<std::string> result;
  for (;;) {
    PemReadResult read_result = ReadNextPemMessage(input);
    if (read_result.status == PemReadResult::kEof) {
      return result;
    }
    if (read_result.status != PemReadResult::kOk) {
      return std::vector<std::string>();
    }
    if (read_result.type != "CERTIFICATE") {
      continue;
    }
    result.emplace_back(std::move(read_result.contents));
  }
}

PublicKeyType CertificateView::public_key_type() const {
  return PublicKeyTypeFromKey(public_key_.get());
}

bool CertificateView::ValidatePublicKeyParameters() {
  // The profile here affects what certificates can be used when QUIC is used as
  // a server library without any custom certificate provider logic.
  // The goal is to allow at minimum any certificate that would be allowed on a
  // regular Web session over TLS 1.3 while ensuring we do not expose any
  // algorithms we don't want to support long-term.
  PublicKeyType key_type = PublicKeyTypeFromKey(public_key_.get());
  switch (key_type) {
    case PublicKeyType::kRsa:
      return EVP_PKEY_bits(public_key_.get()) >= 2048;
    case PublicKeyType::kP256:
    case PublicKeyType::kP384:
    case PublicKeyType::kEd25519:
      return true;
    default:
      return false;
  }
}

bool CertificateView::VerifySignature(absl::string_view data,
                                      absl::string_view signature,
                                      uint16_t signature_algorithm) const {
  if (PublicKeyTypeFromSignatureAlgorithm(signature_algorithm) !=
      PublicKeyTypeFromKey(public_key_.get())) {
    QUIC_BUG(quic_bug_10640_1)
        << "Mismatch between the requested signature algorithm and the "
           "type of the public key.";
    return false;
  }

  bssl::ScopedEVP_MD_CTX md_ctx;
  EVP_PKEY_CTX* pctx;
  if (!EVP_DigestVerifyInit(
          md_ctx.get(), &pctx,
          SSL_get_signature_algorithm_digest(signature_algorithm), nullptr,
          public_key_.get())) {
    return false;
  }
  if (SSL_is_signature_algorithm_rsa_pss(signature_algorithm)) {
    if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) ||
        !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1)) {
      return false;
    }
  }
  return EVP_DigestVerify(
      md_ctx.get(), reinterpret_cast<const uint8_t*>(signature.data()),
      signature.size(), reinterpret_cast<const uint8_t*>(data.data()),
      data.size());
}

absl::optional<std::string> CertificateView::GetHumanReadableSubject() const {
  CBS input = StringPieceToCbs(subject_der_);
  return DistinguishedNameToString(input);
}

std::unique_ptr<CertificatePrivateKey> CertificatePrivateKey::LoadFromDer(
    absl::string_view private_key) {
  std::unique_ptr<CertificatePrivateKey> result(new CertificatePrivateKey());
  CBS private_key_cbs = StringPieceToCbs(private_key);
  result->private_key_.reset(EVP_parse_private_key(&private_key_cbs));
  if (result->private_key_ == nullptr || CBS_len(&private_key_cbs) != 0) {
    return nullptr;
  }
  return result;
}

std::unique_ptr<CertificatePrivateKey> CertificatePrivateKey::LoadPemFromStream(
    std::istream* input) {
skip:
  PemReadResult result = ReadNextPemMessage(input);
  if (result.status != PemReadResult::kOk) {
    return nullptr;
  }
  // RFC 5958 OneAsymmetricKey message.
  if (result.type == "PRIVATE KEY") {
    return LoadFromDer(result.contents);
  }
  // Legacy OpenSSL format: PKCS#1 (RFC 8017) RSAPrivateKey message.
  if (result.type == "RSA PRIVATE KEY") {
    CBS private_key_cbs = StringPieceToCbs(result.contents);
    bssl::UniquePtr<RSA> rsa(RSA_parse_private_key(&private_key_cbs));
    if (rsa == nullptr || CBS_len(&private_key_cbs) != 0) {
      return nullptr;
    }

    std::unique_ptr<CertificatePrivateKey> key(new CertificatePrivateKey());
    key->private_key_.reset(EVP_PKEY_new());
    EVP_PKEY_assign_RSA(key->private_key_.get(), rsa.release());
    return key;
  }
  // EC keys are sometimes generated with "openssl ecparam -genkey". If the user
  // forgets -noout, OpenSSL will output a redundant copy of the EC parameters.
  // Skip those.
  if (result.type == "EC PARAMETERS") {
    goto skip;
  }
  // Legacy OpenSSL format: RFC 5915 ECPrivateKey message.
  if (result.type == "EC PRIVATE KEY") {
    CBS private_key_cbs = StringPieceToCbs(result.contents);
    bssl::UniquePtr<EC_KEY> ec_key(
        EC_KEY_parse_private_key(&private_key_cbs, /*group=*/nullptr));
    if (ec_key == nullptr || CBS_len(&private_key_cbs) != 0) {
      return nullptr;
    }

    std::unique_ptr<CertificatePrivateKey> key(new CertificatePrivateKey());
    key->private_key_.reset(EVP_PKEY_new());
    EVP_PKEY_assign_EC_KEY(key->private_key_.get(), ec_key.release());
    return key;
  }
  // Unknown format.
  return nullptr;
}

std::string CertificatePrivateKey::Sign(absl::string_view input,
                                        uint16_t signature_algorithm) const {
  if (!ValidForSignatureAlgorithm(signature_algorithm)) {
    QUIC_BUG(quic_bug_10640_2)
        << "Mismatch between the requested signature algorithm and the "
           "type of the private key.";
    return "";
  }

  bssl::ScopedEVP_MD_CTX md_ctx;
  EVP_PKEY_CTX* pctx;
  if (!EVP_DigestSignInit(
          md_ctx.get(), &pctx,
          SSL_get_signature_algorithm_digest(signature_algorithm),
          /*e=*/nullptr, private_key_.get())) {
    return "";
  }
  if (SSL_is_signature_algorithm_rsa_pss(signature_algorithm)) {
    if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) ||
        !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1)) {
      return "";
    }
  }

  std::string output;
  size_t output_size;
  if (!EVP_DigestSign(md_ctx.get(), /*out_sig=*/nullptr, &output_size,
                      reinterpret_cast<const uint8_t*>(input.data()),
                      input.size())) {
    return "";
  }
  output.resize(output_size);
  if (!EVP_DigestSign(
          md_ctx.get(), reinterpret_cast<uint8_t*>(&output[0]), &output_size,
          reinterpret_cast<const uint8_t*>(input.data()), input.size())) {
    return "";
  }
  output.resize(output_size);
  return output;
}

bool CertificatePrivateKey::MatchesPublicKey(
    const CertificateView& view) const {
  return EVP_PKEY_cmp(view.public_key(), private_key_.get()) == 1;
}

bool CertificatePrivateKey::ValidForSignatureAlgorithm(
    uint16_t signature_algorithm) const {
  return PublicKeyTypeFromSignatureAlgorithm(signature_algorithm) ==
         PublicKeyTypeFromKey(private_key_.get());
}

}  // namespace quic
