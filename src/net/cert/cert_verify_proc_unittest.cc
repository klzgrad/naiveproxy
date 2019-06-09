// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/cert_verify_proc.h"

#include <vector>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#include "crypto/openssl_util.h"
#include "crypto/rsa_private_key.h"
#include "crypto/sha2.h"
#include "net/base/net_errors.h"
#include "net/cert/asn1_util.h"
#include "net/cert/cert_net_fetcher.h"
#include "net/cert/cert_status_flags.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/cert_verify_proc_builtin.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/crl_set.h"
#include "net/cert/ev_root_ca_metadata.h"
#include "net/cert/internal/parse_certificate.h"
#include "net/cert/internal/signature_algorithm.h"
#include "net/cert/pem_tokenizer.h"
#include "net/cert/test_root_certs.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"
#include "net/cert_net/cert_net_fetcher_impl.h"
#include "net/der/input.h"
#include "net/der/parser.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/test/cert_test_util.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/gtest_util.h"
#include "net/test/test_certificate_data.h"
#include "net/test/test_data_directory.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_context_getter.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/mem.h"

#if defined(USE_NSS_CERTS)
#include "net/cert_net/nss_ocsp.h"
#endif

#if defined(OS_ANDROID)
#include "base/android/build_info.h"
#endif

#if defined(OS_MACOSX) && !defined(OS_IOS)
#include "base/mac/mac_util.h"
#endif

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

// TODO(crbug.com/649017): Add tests that only certificates with
// serverAuth are accepted.

using net::test::IsError;
using net::test::IsOk;

using base::HexEncode;

namespace net {

namespace {

const char kTLSFeatureExtensionHistogram[] =
    "Net.Certificate.TLSFeatureExtensionWithPrivateRoot";
const char kTLSFeatureExtensionOCSPHistogram[] =
    "Net.Certificate.TLSFeatureExtensionWithPrivateRootHasOCSP";
const char kTrustAnchorVerifyHistogram[] = "Net.Certificate.TrustAnchor.Verify";
const char kTrustAnchorVerifyOutOfDateHistogram[] =
    "Net.Certificate.TrustAnchor.VerifyOutOfDate";

// Mock CertVerifyProc that sets the CertVerifyResult to a given value for
// all certificates that are Verify()'d
class MockCertVerifyProc : public CertVerifyProc {
 public:
  explicit MockCertVerifyProc(const CertVerifyResult& result)
      : result_(result) {}
  // CertVerifyProc implementation:
  bool SupportsAdditionalTrustAnchors() const override { return false; }

 protected:
  ~MockCertVerifyProc() override = default;

 private:
  int VerifyInternal(X509Certificate* cert,
                     const std::string& hostname,
                     const std::string& ocsp_response,
                     const std::string& sct_list,
                     int flags,
                     CRLSet* crl_set,
                     const CertificateList& additional_trust_anchors,
                     CertVerifyResult* verify_result) override;

  const CertVerifyResult result_;

  DISALLOW_COPY_AND_ASSIGN(MockCertVerifyProc);
};

int MockCertVerifyProc::VerifyInternal(
    X509Certificate* cert,
    const std::string& hostname,
    const std::string& ocsp_response,
    const std::string& sct_list,
    int flags,
    CRLSet* crl_set,
    const CertificateList& additional_trust_anchors,
    CertVerifyResult* verify_result) {
  *verify_result = result_;
  verify_result->verified_cert = cert;
  return OK;
}

// This enum identifies a concrete implemenation of CertVerifyProc.
//
// The type is erased by CertVerifyProc::CreateDefault(), however
// needs to be known for some of the test expectations.
enum CertVerifyProcType {
  CERT_VERIFY_PROC_NSS,
  CERT_VERIFY_PROC_ANDROID,
  CERT_VERIFY_PROC_IOS,
  CERT_VERIFY_PROC_MAC,
  CERT_VERIFY_PROC_WIN,
  CERT_VERIFY_PROC_BUILTIN,
};

// Returns the CertVerifyProcType corresponding to what
// CertVerifyProc::CreateDefault() returns. This needs to be kept in sync with
// CreateDefault().
CertVerifyProcType GetDefaultCertVerifyProcType() {
#if defined(USE_NSS_CERTS)
  return CERT_VERIFY_PROC_NSS;
#elif defined(OS_ANDROID)
  return CERT_VERIFY_PROC_ANDROID;
#elif defined(OS_IOS)
  return CERT_VERIFY_PROC_IOS;
#elif defined(OS_MACOSX)
  return CERT_VERIFY_PROC_MAC;
#elif defined(OS_WIN)
  return CERT_VERIFY_PROC_WIN;
#elif defined(OS_FUCHSIA)
  return CERT_VERIFY_PROC_BUILTIN;
#else
// Will fail to compile.
#endif
}

// Whether the test is running within the iphone simulator.
const bool kTargetIsIphoneSimulator =
#if TARGET_IPHONE_SIMULATOR
    true;
#else
    false;
#endif

// Returns a textual description of the CertVerifyProc implementation
// that is being tested, used to give better names to parameterized
// tests.
std::string VerifyProcTypeToName(
    const testing::TestParamInfo<CertVerifyProcType>& params) {
  switch (params.param) {
    case CERT_VERIFY_PROC_NSS:
      return "CertVerifyProcNSS";
    case CERT_VERIFY_PROC_ANDROID:
      return "CertVerifyProcAndroid";
    case CERT_VERIFY_PROC_IOS:
      return "CertVerifyProcIOS";
    case CERT_VERIFY_PROC_MAC:
      return "CertVerifyProcMac";
    case CERT_VERIFY_PROC_WIN:
      return "CertVerifyProcWin";
    case CERT_VERIFY_PROC_BUILTIN:
      return "CertVerifyProcBuiltin";
  }

  return nullptr;
}

// The set of all CertVerifyProcTypes that tests should be
// parameterized on.
const std::vector<CertVerifyProcType> kAllCertVerifiers = {
    GetDefaultCertVerifyProcType()

// TODO(crbug.com/649017): Enable this everywhere. Right now this is
// gated on having CertVerifyProcBuiltin understand the roots added
// via TestRootCerts.
#if defined(USE_NSS_CERTS) || (defined(OS_MACOSX) && !defined(OS_IOS))
        ,
    CERT_VERIFY_PROC_BUILTIN
#endif
};

// Returns true if a test root added through ScopedTestRoot can verify
// successfully as a target certificate with chain of length 1 on the given
// CertVerifyProcType.
bool ScopedTestRootCanTrustTargetCert(CertVerifyProcType verify_proc_type) {
  return verify_proc_type == CERT_VERIFY_PROC_MAC ||
         verify_proc_type == CERT_VERIFY_PROC_IOS ||
         verify_proc_type == CERT_VERIFY_PROC_NSS ||
         verify_proc_type == CERT_VERIFY_PROC_ANDROID;
}

// TODO(crbug.com/649017): This is not parameterized by the CertVerifyProc
// because the CertVerifyProc::Verify() does this unconditionally based on the
// platform.
bool AreSHA1IntermediatesAllowed() {
#if defined(OS_WIN)
  // TODO(rsleevi): Remove this once https://crbug.com/588789 is resolved
  // for Windows 7/2008 users.
  // Note: This must be kept in sync with cert_verify_proc.cc
  return base::win::GetVersion() < base::win::Version::WIN8;
#else
  return false;
#endif
}

std::string MakeRandomHexString(size_t num_bytes) {
  std::vector<char> rand_bytes;
  rand_bytes.resize(num_bytes);

  base::RandBytes(&rand_bytes[0], rand_bytes.size());
  return base::HexEncode(&rand_bytes[0], rand_bytes.size());
}

// CertBuilder is a helper class to dynamically create a test certificate.
//
// CertBuilder is initialized using an existing certificate, from which it
// copies most properties (see InitFromCert for details).
//
// The subject, serial number, and key for the final certificate are chosen
// randomly. Using a randomized subject and serial number is important to defeat
// certificate caching done by NSS, which otherwise can make test outcomes
// dependent on ordering.
class CertBuilder {
 public:
  // Initializes the CertBuilder using |orig_cert|. If |issuer| is null
  // then the generated certificate will be self-signed. Otherwise, it
  // will be signed using |issuer|.
  CertBuilder(CRYPTO_BUFFER* orig_cert, CertBuilder* issuer) : issuer_(issuer) {
    if (!issuer_)
      issuer_ = this;

    crypto::EnsureOpenSSLInit();
    InitFromCert(der::Input(x509_util::CryptoBufferAsStringPiece(orig_cert)));
  }

  // Sets a value for the indicated X.509 (v3) extension.
  void SetExtension(const der::Input& oid,
                    std::string value,
                    bool critical = false) {
    auto& extension_value = extensions_[oid.AsString()];
    extension_value.critical = critical;
    extension_value.value = std::move(value);

    Invalidate();
  }

  // Sets an AIA extension with a single caIssuers access method.
  void SetCaIssuersUrl(const GURL& url) {
    std::string url_spec = url.spec();

    // From RFC 5280:
    //
    //   AuthorityInfoAccessSyntax  ::=
    //           SEQUENCE SIZE (1..MAX) OF AccessDescription
    //
    //   AccessDescription  ::=  SEQUENCE {
    //           accessMethod          OBJECT IDENTIFIER,
    //           accessLocation        GeneralName  }
    bssl::ScopedCBB cbb;
    CBB aia, ca_issuer, access_method, access_location;
    ASSERT_TRUE(CBB_init(cbb.get(), url_spec.size()));
    ASSERT_TRUE(CBB_add_asn1(cbb.get(), &aia, CBS_ASN1_SEQUENCE));
    ASSERT_TRUE(CBB_add_asn1(&aia, &ca_issuer, CBS_ASN1_SEQUENCE));
    ASSERT_TRUE(CBB_add_asn1(&ca_issuer, &access_method, CBS_ASN1_OBJECT));
    ASSERT_TRUE(
        AddBytesToCBB(&access_method, AdCaIssuersOid().AsStringPiece()));
    ASSERT_TRUE(CBB_add_asn1(&ca_issuer, &access_location,
                             CBS_ASN1_CONTEXT_SPECIFIC | 6));
    ASSERT_TRUE(AddBytesToCBB(&access_location, url_spec));

    SetExtension(AuthorityInfoAccessOid(), FinishCBB(cbb.get()));
  }

  // Sets the SAN for the certificate to a single dNSName.
  void SetSubjectAltName(const std::string& dns_name) {
    // From RFC 5280:
    //
    //   SubjectAltName ::= GeneralNames
    //
    //   GeneralNames ::= SEQUENCE SIZE (1..MAX) OF GeneralName
    //
    //   GeneralName ::= CHOICE {
    //        otherName                       [0]     OtherName,
    //        rfc822Name                      [1]     IA5String,
    //        dNSName                         [2]     IA5String,
    //        ... }
    bssl::ScopedCBB cbb;
    CBB general_names, general_name;
    ASSERT_TRUE(CBB_init(cbb.get(), dns_name.size()));
    ASSERT_TRUE(CBB_add_asn1(cbb.get(), &general_names, CBS_ASN1_SEQUENCE));
    ASSERT_TRUE(CBB_add_asn1(&general_names, &general_name,
                             CBS_ASN1_CONTEXT_SPECIFIC | 2));
    ASSERT_TRUE(AddBytesToCBB(&general_name, dns_name));

    SetExtension(SubjectAltNameOid(), FinishCBB(cbb.get()));
  }

  // Sets the signature algorithm for the certificate to either
  // sha256WithRSAEncryption or sha1WithRSAEncryption.
  void SetSignatureAlgorithmRsaPkca1(DigestAlgorithm digest) {
    switch (digest) {
      case DigestAlgorithm::Sha256: {
        const uint8_t kSha256WithRSAEncryption[] = {
            0x30, 0x0D, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
            0xf7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00};
        SetSignatureAlgorithm(std::string(std::begin(kSha256WithRSAEncryption),
                                          std::end(kSha256WithRSAEncryption)));
        break;
      }

      case DigestAlgorithm::Sha1: {
        const uint8_t kSha1WithRSAEncryption[] = {0x30, 0x0D, 0x06, 0x09, 0x2a,
                                                  0x86, 0x48, 0x86, 0xf7, 0x0d,
                                                  0x01, 0x01, 0x05, 0x05, 0x00};
        SetSignatureAlgorithm(std::string(std::begin(kSha1WithRSAEncryption),
                                          std::end(kSha1WithRSAEncryption)));
        break;
      }

      default:
        ASSERT_TRUE(false);
    }
  }

  void SetSignatureAlgorithm(std::string algorithm_tlv) {
    signature_algorithm_tlv_ = std::move(algorithm_tlv);
    Invalidate();
  }

  void SetRandomSerialNumber() {
    serial_number_ = base::RandUint64();
    Invalidate();
  }

  // Returns a CRYPTO_BUFFER to the generated certificate.
  CRYPTO_BUFFER* GetCertBuffer() {
    if (!cert_)
      GenerateCertificate();
    return cert_.get();
  }

  bssl::UniquePtr<CRYPTO_BUFFER> DupCertBuffer() {
    return bssl::UpRef(GetCertBuffer());
  }

  // Returns the subject of the generated certificate.
  const std::string& GetSubject() {
    if (subject_tlv_.empty())
      GenerateSubject();
    return subject_tlv_;
  }

  // Returns the (RSA) key for the generated certificate.
  EVP_PKEY* GetKey() {
    if (!key_)
      GenerateKey();
    return key_.get();
  }

  // Returns an X509Certificate for the generated certificate.
  scoped_refptr<X509Certificate> GetX509Certificate() {
    return X509Certificate::CreateFromBuffer(DupCertBuffer(), {});
  }

  // Returns a copy of the certificate's DER.
  std::string GetDER() {
    return x509_util::CryptoBufferAsStringPiece(GetCertBuffer()).as_string();
  }

 private:
  // Marks the generated certificate DER as invalid, so it will need to
  // be re-generated next time the DER is accessed.
  void Invalidate() { cert_.reset(); }

  // Sets the |key_| to a 2048-bit RSA key.
  void GenerateKey() {
    ASSERT_FALSE(key_);

    auto private_key = crypto::RSAPrivateKey::Create(2048);
    key_ = bssl::UpRef(private_key->key());
  }

  // Adds bytes (specified as a StringPiece) to the given CBB.
  static bool AddBytesToCBB(CBB* cbb, base::StringPiece bytes) {
    return CBB_add_bytes(cbb, reinterpret_cast<const uint8_t*>(bytes.data()),
                         bytes.size());
  }

  // Finalizes the CBB to a std::string.
  static std::string FinishCBB(CBB* cbb) {
    size_t cbb_len;
    uint8_t* cbb_bytes;

    if (!CBB_finish(cbb, &cbb_bytes, &cbb_len)) {
      ADD_FAILURE() << "CBB_finish() failed";
      return std::string();
    }

    bssl::UniquePtr<uint8_t> delete_bytes(cbb_bytes);
    return std::string(reinterpret_cast<char*>(cbb_bytes), cbb_len);
  }

  // Generates a random subject for the certificate, comprised of just a CN.
  void GenerateSubject() {
    ASSERT_TRUE(subject_tlv_.empty());

    // Use a random common name comprised of 12 bytes in hex.
    std::string common_name = MakeRandomHexString(12);

    // See RFC 4519.
    static const uint8_t kCommonName[] = {0x55, 0x04, 0x03};

    // See RFC 5280, section 4.1.2.4.
    bssl::ScopedCBB cbb;
    CBB rdns, rdn, attr, type, value;
    ASSERT_TRUE(CBB_init(cbb.get(), 64));
    ASSERT_TRUE(CBB_add_asn1(cbb.get(), &rdns, CBS_ASN1_SEQUENCE));
    ASSERT_TRUE(CBB_add_asn1(&rdns, &rdn, CBS_ASN1_SET));
    ASSERT_TRUE(CBB_add_asn1(&rdn, &attr, CBS_ASN1_SEQUENCE));
    ASSERT_TRUE(CBB_add_asn1(&attr, &type, CBS_ASN1_OBJECT));
    ASSERT_TRUE(CBB_add_bytes(&type, kCommonName, sizeof(kCommonName)));
    ASSERT_TRUE(CBB_add_asn1(&attr, &value, CBS_ASN1_UTF8STRING));
    ASSERT_TRUE(AddBytesToCBB(&value, common_name));

    subject_tlv_ = FinishCBB(cbb.get());
  }

  // Returns the serial number for the generated certificate.
  uint64_t GetSerialNumber() {
    if (!serial_number_)
      serial_number_ = base::RandUint64();
    return serial_number_;
  }

  // Parses |cert| and copies the following properties:
  //   * All extensions (dropping any duplicates)
  //   * Signature algorithm (from Certificate)
  //   * Validity (expiration)
  void InitFromCert(const der::Input& cert) {
    extensions_.clear();
    Invalidate();

    // From RFC 5280, section 4.1
    //    Certificate  ::=  SEQUENCE  {
    //      tbsCertificate       TBSCertificate,
    //      signatureAlgorithm   AlgorithmIdentifier,
    //      signatureValue       BIT STRING  }

    // TBSCertificate  ::=  SEQUENCE  {
    //      version         [0]  EXPLICIT Version DEFAULT v1,
    //      serialNumber         CertificateSerialNumber,
    //      signature            AlgorithmIdentifier,
    //      issuer               Name,
    //      validity             Validity,
    //      subject              Name,
    //      subjectPublicKeyInfo SubjectPublicKeyInfo,
    //      issuerUniqueID  [1]  IMPLICIT UniqueIdentifier OPTIONAL,
    //                           -- If present, version MUST be v2 or v3
    //      subjectUniqueID [2]  IMPLICIT UniqueIdentifier OPTIONAL,
    //                           -- If present, version MUST be v2 or v3
    //      extensions      [3]  EXPLICIT Extensions OPTIONAL
    //                           -- If present, version MUST be v3
    //      }
    der::Parser parser(cert);
    der::Parser certificate;
    der::Parser tbs_certificate;
    ASSERT_TRUE(parser.ReadSequence(&certificate));
    ASSERT_TRUE(certificate.ReadSequence(&tbs_certificate));

    // version
    bool unused;
    ASSERT_TRUE(tbs_certificate.SkipOptionalTag(
        der::kTagConstructed | der::kTagContextSpecific | 0, &unused));
    // serialNumber
    ASSERT_TRUE(tbs_certificate.SkipTag(der::kInteger));

    // signature
    der::Input signature_algorithm_tlv;
    ASSERT_TRUE(tbs_certificate.ReadRawTLV(&signature_algorithm_tlv));
    signature_algorithm_tlv_ = signature_algorithm_tlv.AsString();

    // issuer
    ASSERT_TRUE(tbs_certificate.SkipTag(der::kSequence));

    // validity
    der::Input validity_tlv;
    ASSERT_TRUE(tbs_certificate.ReadRawTLV(&validity_tlv));
    validity_tlv_ = validity_tlv.AsString();

    // subject
    ASSERT_TRUE(tbs_certificate.SkipTag(der::kSequence));
    // subjectPublicKeyInfo
    ASSERT_TRUE(tbs_certificate.SkipTag(der::kSequence));
    // issuerUniqueID
    ASSERT_TRUE(tbs_certificate.SkipOptionalTag(
        der::ContextSpecificPrimitive(1), &unused));
    // subjectUniqueID
    ASSERT_TRUE(tbs_certificate.SkipOptionalTag(
        der::ContextSpecificPrimitive(2), &unused));

    // extensions
    bool has_extensions = false;
    der::Input extensions_tlv;
    ASSERT_TRUE(tbs_certificate.ReadOptionalTag(
        der::ContextSpecificConstructed(3), &extensions_tlv, &has_extensions));
    if (has_extensions) {
      std::map<der::Input, ParsedExtension> parsed_extensions;
      ASSERT_TRUE(ParseExtensions(extensions_tlv, &parsed_extensions));

      for (const auto& parsed_extension : parsed_extensions) {
        SetExtension(parsed_extension.second.oid,
                     parsed_extension.second.value.AsString(),
                     parsed_extension.second.critical);
      }
    }
  }

  // Assembles the CertBuilder into a TBSCertificate.
  void BuildTBSCertificate(std::string* out) {
    bssl::ScopedCBB cbb;
    CBB tbs_cert, version, extensions_context, extensions;

    ASSERT_TRUE(CBB_init(cbb.get(), 64));
    ASSERT_TRUE(CBB_add_asn1(cbb.get(), &tbs_cert, CBS_ASN1_SEQUENCE));
    ASSERT_TRUE(
        CBB_add_asn1(&tbs_cert, &version,
                     CBS_ASN1_CONTEXT_SPECIFIC | CBS_ASN1_CONSTRUCTED | 0));
    // Always use v3 certificates.
    ASSERT_TRUE(CBB_add_asn1_uint64(&version, 2));
    ASSERT_TRUE(CBB_add_asn1_uint64(&tbs_cert, GetSerialNumber()));
    ASSERT_TRUE(AddSignatureAlgorithm(&tbs_cert));
    ASSERT_TRUE(AddBytesToCBB(&tbs_cert, issuer_->GetSubject()));
    ASSERT_TRUE(AddBytesToCBB(&tbs_cert, validity_tlv_));
    ASSERT_TRUE(AddBytesToCBB(&tbs_cert, GetSubject()));
    ASSERT_TRUE(EVP_marshal_public_key(&tbs_cert, GetKey()));

    // Serialize all the extensions.
    if (!extensions_.empty()) {
      ASSERT_TRUE(
          CBB_add_asn1(&tbs_cert, &extensions_context,
                       CBS_ASN1_CONTEXT_SPECIFIC | CBS_ASN1_CONSTRUCTED | 3));
      ASSERT_TRUE(
          CBB_add_asn1(&extensions_context, &extensions, CBS_ASN1_SEQUENCE));

      //   Extension  ::=  SEQUENCE  {
      //        extnID      OBJECT IDENTIFIER,
      //        critical    BOOLEAN DEFAULT FALSE,
      //        extnValue   OCTET STRING
      //                    -- contains the DER encoding of an ASN.1 value
      //                    -- corresponding to the extension type identified
      //                    -- by extnID
      //        }
      for (const auto& extension_it : extensions_) {
        CBB extension_seq, oid, extn_value;
        ASSERT_TRUE(
            CBB_add_asn1(&extensions, &extension_seq, CBS_ASN1_SEQUENCE));
        ASSERT_TRUE(CBB_add_asn1(&extension_seq, &oid, CBS_ASN1_OBJECT));
        ASSERT_TRUE(AddBytesToCBB(&oid, extension_it.first));
        if (extension_it.second.critical) {
          ASSERT_TRUE(CBB_add_asn1_bool(&extension_seq, true));
        }

        ASSERT_TRUE(
            CBB_add_asn1(&extension_seq, &extn_value, CBS_ASN1_OCTETSTRING));
        ASSERT_TRUE(AddBytesToCBB(&extn_value, extension_it.second.value));
        ASSERT_TRUE(CBB_flush(&extensions));
      }
    }

    *out = FinishCBB(cbb.get());
  }

  bool AddSignatureAlgorithm(CBB* cbb) {
    return AddBytesToCBB(cbb, signature_algorithm_tlv_);
  }

  void GenerateCertificate() {
    ASSERT_FALSE(cert_);

    std::string tbs_cert;
    BuildTBSCertificate(&tbs_cert);
    const uint8_t* tbs_cert_bytes =
        reinterpret_cast<const uint8_t*>(tbs_cert.data());

    // Determine the correct digest algorithm to use (assumes RSA PKCS#1
    // signatures).
    auto signature_algorithm = SignatureAlgorithm::Create(
        der::Input(&signature_algorithm_tlv_), nullptr);
    ASSERT_TRUE(signature_algorithm);
    ASSERT_EQ(SignatureAlgorithmId::RsaPkcs1, signature_algorithm->algorithm());
    const EVP_MD* md = nullptr;

    switch (signature_algorithm->digest()) {
      case DigestAlgorithm::Sha256:
        md = EVP_sha256();
        break;

      case DigestAlgorithm::Sha1:
        md = EVP_sha1();
        break;

      default:
        ASSERT_TRUE(false) << "Only rsaEncryptionWithSha256 or "
                              "rsaEnryptionWithSha1 are supported";
        break;
    }

    // Sign the TBSCertificate and write the entire certificate.
    bssl::ScopedCBB cbb;
    CBB cert, signature;
    bssl::ScopedEVP_MD_CTX ctx;
    uint8_t* sig_out;
    size_t sig_len;

    ASSERT_TRUE(CBB_init(cbb.get(), tbs_cert.size()));
    ASSERT_TRUE(CBB_add_asn1(cbb.get(), &cert, CBS_ASN1_SEQUENCE));
    ASSERT_TRUE(AddBytesToCBB(&cert, tbs_cert));
    ASSERT_TRUE(AddSignatureAlgorithm(&cert));
    ASSERT_TRUE(CBB_add_asn1(&cert, &signature, CBS_ASN1_BITSTRING));
    ASSERT_TRUE(CBB_add_u8(&signature, 0 /* no unused bits */));
    ASSERT_TRUE(
        EVP_DigestSignInit(ctx.get(), nullptr, md, nullptr, issuer_->GetKey()));
    ASSERT_TRUE(EVP_DigestSign(ctx.get(), nullptr, &sig_len, tbs_cert_bytes,
                               tbs_cert.size()));
    ASSERT_TRUE(CBB_reserve(&signature, &sig_out, sig_len));
    ASSERT_TRUE(EVP_DigestSign(ctx.get(), sig_out, &sig_len, tbs_cert_bytes,
                               tbs_cert.size()));
    ASSERT_TRUE(CBB_did_write(&signature, sig_len));

    auto cert_der = FinishCBB(cbb.get());
    cert_ = x509_util::CreateCryptoBuffer(
        reinterpret_cast<const uint8_t*>(cert_der.data()), cert_der.size());
  }

  struct ExtensionValue {
    bool critical = false;
    std::string value;
  };

  std::string validity_tlv_;
  std::string subject_tlv_;
  std::string signature_algorithm_tlv_;
  uint64_t serial_number_ = 0;

  std::map<std::string, ExtensionValue> extensions_;

  bssl::UniquePtr<CRYPTO_BUFFER> cert_;
  bssl::UniquePtr<EVP_PKEY> key_;

  CertBuilder* issuer_ = nullptr;
};

}  // namespace

// This fixture is for tests that apply to concrete implementations of
// CertVerifyProc. It will be run for all of the concrete CertVerifyProc types.
//
// It is called "Internal" as it tests the internal methods like
// "VerifyInternal()".
class CertVerifyProcInternalTest
    : public testing::TestWithParam<CertVerifyProcType> {
 protected:
  void SetUp() override { SetUpWithCertNetFetcher(nullptr); }

  // CertNetFetcher may be initialized by subclasses that want to use net
  // fetching by calling SetUpWithCertNetFetcher instead of SetUp.
  void SetUpWithCertNetFetcher(scoped_refptr<CertNetFetcher> cert_net_fetcher) {
    CertVerifyProcType type = verify_proc_type();
    if (type == CERT_VERIFY_PROC_BUILTIN) {
      verify_proc_ = CreateCertVerifyProcBuiltin(std::move(cert_net_fetcher));
    } else if (type == GetDefaultCertVerifyProcType()) {
      verify_proc_ = CertVerifyProc::CreateDefault(std::move(cert_net_fetcher));
    } else {
      ADD_FAILURE() << "Unhandled CertVerifyProcType";
    }
  }

  int Verify(X509Certificate* cert,
             const std::string& hostname,
             int flags,
             CRLSet* crl_set,
             const CertificateList& additional_trust_anchors,
             CertVerifyResult* verify_result) {
    return verify_proc_->Verify(cert, hostname, /*ocsp_response=*/std::string(),
                                /*sct_list=*/std::string(), flags, crl_set,
                                additional_trust_anchors, verify_result);
  }

  CertVerifyProcType verify_proc_type() const { return GetParam(); }

  bool SupportsAdditionalTrustAnchors() const {
    return verify_proc_->SupportsAdditionalTrustAnchors();
  }

  bool SupportsReturningVerifiedChain() const {
#if defined(OS_ANDROID)
    // Before API level 17 (SDK_VERSION_JELLY_BEAN_MR1), Android does
    // not expose the APIs necessary to get at the verified
    // certificate chain.
    if (verify_proc_type() == CERT_VERIFY_PROC_ANDROID &&
        base::android::BuildInfo::GetInstance()->sdk_int() <
            base::android::SDK_VERSION_JELLY_BEAN_MR1)
      return false;
#endif
    return true;
  }

  bool WeakKeysAreInvalid() const {
#if defined(OS_MACOSX) && !defined(OS_IOS)
    // Starting with Mac OS 10.12, certs with weak keys are treated as
    // (recoverable) invalid certificate errors.
    if (verify_proc_type() == CERT_VERIFY_PROC_MAC &&
        base::mac::IsAtLeastOS10_12()) {
      return true;
    }
#endif
    return false;
  }

  bool SupportsCRLSet() const {
    return verify_proc_type() == CERT_VERIFY_PROC_NSS ||
           verify_proc_type() == CERT_VERIFY_PROC_WIN ||
           verify_proc_type() == CERT_VERIFY_PROC_MAC ||
           verify_proc_type() == CERT_VERIFY_PROC_BUILTIN;
  }

  bool SupportsCRLSetsInPathBuilding() const {
    return verify_proc_type() == CERT_VERIFY_PROC_WIN ||
           verify_proc_type() == CERT_VERIFY_PROC_NSS ||
           verify_proc_type() == CERT_VERIFY_PROC_BUILTIN;
  }

  bool SupportsEV() const {
    // TODO(crbug.com/117478): Android and iOS do not support EV.
    return verify_proc_type() == CERT_VERIFY_PROC_NSS ||
           verify_proc_type() == CERT_VERIFY_PROC_WIN ||
           verify_proc_type() == CERT_VERIFY_PROC_MAC ||
           verify_proc_type() == CERT_VERIFY_PROC_BUILTIN;
  }

  CertVerifyProc* verify_proc() const { return verify_proc_.get(); }

 private:
  scoped_refptr<CertVerifyProc> verify_proc_;
};

INSTANTIATE_TEST_SUITE_P(,
                         CertVerifyProcInternalTest,
                         testing::ValuesIn(kAllCertVerifiers),
                         VerifyProcTypeToName);

// Tests that a certificate is recognized as EV, when the valid EV policy OID
// for the trust anchor is the second candidate EV oid in the target
// certificate. This is a regression test for crbug.com/705285.
TEST_P(CertVerifyProcInternalTest, EVVerificationMultipleOID) {
  if (!SupportsEV()) {
    LOG(INFO) << "Skipping test as EV verification is not yet supported";
    return;
  }

  // TODO(eroman): Update this test to use a synthetic certificate, so the test
  // does not break in the future. The certificate chain in question expires on
  // Jun 12 14:33:43 2020 GMT, at which point this test will start failing.
  if (base::Time::Now() >
      base::Time::UnixEpoch() + base::TimeDelta::FromSeconds(1591972423)) {
    FAIL() << "This test uses a certificate chain which is now expired. Please "
              "disable and file a bug.";
    return;
  }

  scoped_refptr<X509Certificate> chain = CreateCertificateChainFromFile(
      GetTestCertsDirectory(), "login.trustwave.com.pem",
      X509Certificate::FORMAT_PEM_CERT_SEQUENCE);
  ASSERT_TRUE(chain);

  // Build a CRLSet that covers the target certificate.
  //
  // This way CRLSet coverage will be sufficient for EV revocation checking,
  // so this test does not depend on online revocation checking.
  ASSERT_GE(chain->intermediate_buffers().size(), 1u);
  base::StringPiece spki;
  ASSERT_TRUE(
      asn1::ExtractSPKIFromDERCert(x509_util::CryptoBufferAsStringPiece(
                                       chain->intermediate_buffers()[0].get()),
                                   &spki));
  SHA256HashValue spki_sha256;
  crypto::SHA256HashString(spki, spki_sha256.data, sizeof(spki_sha256.data));
  scoped_refptr<CRLSet> crl_set(
      CRLSet::ForTesting(false, &spki_sha256, "", "", {}));

  CertVerifyResult verify_result;
  int flags = 0;
  int error = Verify(chain.get(), "login.trustwave.com", flags, crl_set.get(),
                     CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_IS_EV);
}

// Target cert has an EV policy, and verifies successfully, but has a chain of
// length 1 because the target cert was directly trusted in the trust store.
// Should verify OK but not with STATUS_IS_EV.
TEST_P(CertVerifyProcInternalTest, TrustedTargetCertWithEVPolicy) {
  // The policy that "explicit-policy-chain.pem" target certificate asserts.
  static const char kEVTestCertPolicy[] = "1.2.3.4";
  ScopedTestEVPolicy scoped_test_ev_policy(
      EVRootCAMetadata::GetInstance(), SHA256HashValue(), kEVTestCertPolicy);

  scoped_refptr<X509Certificate> cert =
      ImportCertFromFile(GetTestCertsDirectory(), "explicit-policy-chain.pem");
  ASSERT_TRUE(cert);
  ScopedTestRoot scoped_test_root(cert.get());

  CertVerifyResult verify_result;
  int flags = 0;
  int error =
      Verify(cert.get(), "policy_test.example", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  if (ScopedTestRootCanTrustTargetCert(verify_proc_type())) {
    EXPECT_THAT(error, IsOk());
    ASSERT_TRUE(verify_result.verified_cert);
    EXPECT_TRUE(verify_result.verified_cert->intermediate_buffers().empty());
  } else {
    EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
  }
  EXPECT_FALSE(verify_result.cert_status & CERT_STATUS_IS_EV);
}

// Target cert has an EV policy, and verifies successfully with a chain of
// length 1, and its fingerprint matches the cert fingerprint for that ev
// policy. This should never happen in reality, but just test that things don't
// explode if it does.
TEST_P(CertVerifyProcInternalTest,
       TrustedTargetCertWithEVPolicyAndEVFingerprint) {
  // The policy that "explicit-policy-chain.pem" target certificate asserts.
  static const char kEVTestCertPolicy[] = "1.2.3.4";
  // This the fingerprint of the "explicit-policy-chain.pem" target certificate.
  // See net/data/ssl/certificates/explicit-policy-chain.pem
  static const SHA256HashValue kEVTestCertFingerprint = {
      {0x71, 0xac, 0xfa, 0x12, 0xa4, 0x42, 0x31, 0x3c, 0xff, 0x10, 0xd2,
       0x9d, 0xb6, 0x1b, 0x4a, 0xe8, 0x25, 0x4e, 0x77, 0xd3, 0x9f, 0xa3,
       0x2f, 0xb3, 0x19, 0x8d, 0x46, 0x9f, 0xb7, 0x73, 0x07, 0x30}};
  ScopedTestEVPolicy scoped_test_ev_policy(EVRootCAMetadata::GetInstance(),
                                           kEVTestCertFingerprint,
                                           kEVTestCertPolicy);

  scoped_refptr<X509Certificate> cert =
      ImportCertFromFile(GetTestCertsDirectory(), "explicit-policy-chain.pem");
  ASSERT_TRUE(cert);
  ScopedTestRoot scoped_test_root(cert.get());

  CertVerifyResult verify_result;
  int flags = 0;
  int error =
      Verify(cert.get(), "policy_test.example", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  if (ScopedTestRootCanTrustTargetCert(verify_proc_type())) {
    EXPECT_THAT(error, IsOk());
    ASSERT_TRUE(verify_result.verified_cert);
    EXPECT_TRUE(verify_result.verified_cert->intermediate_buffers().empty());
  } else {
    EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
  }
  // An EV Root certificate should never be used as an end-entity certificate.
  EXPECT_FALSE(verify_result.cert_status & CERT_STATUS_IS_EV);
}

// TODO(crbug.com/605457): the test expectation was incorrect on some
// configurations, so disable the test until it is fixed (better to have
// a bug to track a failing test than a false sense of security due to
// false positive).
TEST_P(CertVerifyProcInternalTest, DISABLED_PaypalNullCertParsing) {
  // A certificate for www.paypal.com with a NULL byte in the common name.
  // From http://www.gossamer-threads.com/lists/fulldisc/full-disclosure/70363
  SHA256HashValue paypal_null_fingerprint = {{0x00}};

  scoped_refptr<X509Certificate> paypal_null_cert(
      X509Certificate::CreateFromBytes(
          reinterpret_cast<const char*>(paypal_null_der),
          sizeof(paypal_null_der)));

  ASSERT_NE(static_cast<X509Certificate*>(nullptr), paypal_null_cert.get());

  EXPECT_EQ(paypal_null_fingerprint, X509Certificate::CalculateFingerprint256(
                                         paypal_null_cert->cert_buffer()));

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(paypal_null_cert.get(), "www.paypal.com", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);

  if (verify_proc_type() == CERT_VERIFY_PROC_NSS ||
      verify_proc_type() == CERT_VERIFY_PROC_ANDROID) {
    EXPECT_THAT(error, IsError(ERR_CERT_COMMON_NAME_INVALID));
  } else if (verify_proc_type() == CERT_VERIFY_PROC_IOS &&
             kTargetIsIphoneSimulator) {
    // iOS returns a ERR_CERT_INVALID error on the simulator, while returning
    // ERR_CERT_AUTHORITY_INVALID on the real device.
    EXPECT_THAT(error, IsError(ERR_CERT_INVALID));
  } else {
    // TODO(bulach): investigate why macosx and win aren't returning
    // ERR_CERT_INVALID or ERR_CERT_COMMON_NAME_INVALID.
    EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
  }

  // Either the system crypto library should correctly report a certificate
  // name mismatch, or our certificate blacklist should cause us to report an
  // invalid certificate.
  if (verify_proc_type() == CERT_VERIFY_PROC_NSS ||
      verify_proc_type() == CERT_VERIFY_PROC_WIN) {
    EXPECT_TRUE(verify_result.cert_status &
                (CERT_STATUS_COMMON_NAME_INVALID | CERT_STATUS_INVALID));
  }

  // TODO(crbug.com/649017): What expectations to use for the other verifiers?
}

// Tests the case where the target certificate is accepted by
// X509CertificateBytes, but has errors that should cause verification to fail.
TEST_P(CertVerifyProcInternalTest, InvalidTarget) {
  base::FilePath certs_dir =
      GetTestNetDataDirectory().AppendASCII("parse_certificate_unittest");
  scoped_refptr<X509Certificate> bad_cert =
      ImportCertFromFile(certs_dir, "signature_algorithm_null.pem");
  ASSERT_TRUE(bad_cert);

  scoped_refptr<X509Certificate> ok_cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(ok_cert);

  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(bssl::UpRef(ok_cert->cert_buffer()));
  scoped_refptr<X509Certificate> cert_with_bad_target(
      X509Certificate::CreateFromBuffer(bssl::UpRef(bad_cert->cert_buffer()),
                                        std::move(intermediates)));
  ASSERT_TRUE(cert_with_bad_target);
  EXPECT_EQ(1U, cert_with_bad_target->intermediate_buffers().size());

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(cert_with_bad_target.get(), "127.0.0.1", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);

  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_INVALID);
  EXPECT_THAT(error, IsError(ERR_CERT_INVALID));
}

// Tests the case where an intermediate certificate is accepted by
// X509CertificateBytes, but has errors that should prevent using it during
// verification.  The verification should succeed, since the intermediate
// wasn't necessary.
TEST_P(CertVerifyProcInternalTest, UnnecessaryInvalidIntermediate) {
  ScopedTestRoot test_root(
      ImportCertFromFile(GetTestCertsDirectory(), "root_ca_cert.pem").get());

  base::FilePath certs_dir =
      GetTestNetDataDirectory().AppendASCII("parse_certificate_unittest");
  bssl::UniquePtr<CRYPTO_BUFFER> bad_cert =
      x509_util::CreateCryptoBuffer(base::StringPiece("invalid"));
  ASSERT_TRUE(bad_cert);

  scoped_refptr<X509Certificate> ok_cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(ok_cert);

  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(std::move(bad_cert));
  scoped_refptr<X509Certificate> cert_with_bad_intermediate(
      X509Certificate::CreateFromBuffer(bssl::UpRef(ok_cert->cert_buffer()),
                                        std::move(intermediates)));
  ASSERT_TRUE(cert_with_bad_intermediate);
  EXPECT_EQ(1U, cert_with_bad_intermediate->intermediate_buffers().size());

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(cert_with_bad_intermediate.get(), "127.0.0.1", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);

  EXPECT_THAT(error, IsOk());
  EXPECT_EQ(0u, verify_result.cert_status);
}

// A regression test for http://crbug.com/31497.
TEST_P(CertVerifyProcInternalTest, IntermediateCARequireExplicitPolicy) {
  if (verify_proc_type() == CERT_VERIFY_PROC_ANDROID) {
    // Disabled on Android, as the Android verification libraries require an
    // explicit policy to be specified, even when anyPolicy is permitted.
    LOG(INFO) << "Skipping test on Android";
    return;
  }

  base::FilePath certs_dir = GetTestCertsDirectory();

  CertificateList certs = CreateCertificateListFromFile(
      certs_dir, "explicit-policy-chain.pem", X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(3U, certs.size());

  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(bssl::UpRef(certs[1]->cert_buffer()));

  scoped_refptr<X509Certificate> cert = X509Certificate::CreateFromBuffer(
      bssl::UpRef(certs[0]->cert_buffer()), std::move(intermediates));
  ASSERT_TRUE(cert.get());

  ScopedTestRoot scoped_root(certs[2].get());

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(cert.get(), "policy_test.example", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  EXPECT_EQ(0u, verify_result.cert_status);
}

TEST_P(CertVerifyProcInternalTest, RejectExpiredCert) {
  base::FilePath certs_dir = GetTestCertsDirectory();

  // Load root_ca_cert.pem into the test root store.
  ScopedTestRoot test_root(
      ImportCertFromFile(certs_dir, "root_ca_cert.pem").get());

  scoped_refptr<X509Certificate> cert = CreateCertificateChainFromFile(
      certs_dir, "expired_cert.pem", X509Certificate::FORMAT_AUTO);
  ASSERT_TRUE(cert);
  ASSERT_EQ(0U, cert->intermediate_buffers().size());

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(cert.get(), "127.0.0.1", flags, CRLSet::BuiltinCRLSet().get(),
             CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_DATE_INVALID));
  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_DATE_INVALID);
}

// Currently, only RSA and DSA keys are checked for weakness, and our example
// weak size is 768. These could change in the future.
//
// Note that this means there may be false negatives: keys for other
// algorithms and which are weak will pass this test.
static bool IsWeakKeyType(const std::string& key_type) {
  size_t pos = key_type.find("-");
  std::string size = key_type.substr(0, pos);
  std::string type = key_type.substr(pos + 1);

  if (type == "rsa" || type == "dsa")
    return size == "768";

  return false;
}

TEST_P(CertVerifyProcInternalTest, RejectWeakKeys) {
  base::FilePath certs_dir = GetTestCertsDirectory();
  typedef std::vector<std::string> Strings;
  Strings key_types;

  // generate-weak-test-chains.sh currently has:
  //     key_types="768-rsa 1024-rsa 2048-rsa prime256v1-ecdsa"
  // We must use the same key types here. The filenames generated look like:
  //     2048-rsa-ee-by-768-rsa-intermediate.pem
  key_types.push_back("768-rsa");
  key_types.push_back("1024-rsa");
  key_types.push_back("2048-rsa");
  key_types.push_back("prime256v1-ecdsa");

  // Add the root that signed the intermediates for this test.
  scoped_refptr<X509Certificate> root_cert =
      ImportCertFromFile(certs_dir, "2048-rsa-root.pem");
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), root_cert.get());
  ScopedTestRoot scoped_root(root_cert.get());

  // Now test each chain.
  for (Strings::const_iterator ee_type = key_types.begin();
       ee_type != key_types.end(); ++ee_type) {
    for (Strings::const_iterator signer_type = key_types.begin();
         signer_type != key_types.end(); ++signer_type) {
      std::string basename =
          *ee_type + "-ee-by-" + *signer_type + "-intermediate.pem";
      SCOPED_TRACE(basename);
      scoped_refptr<X509Certificate> ee_cert =
          ImportCertFromFile(certs_dir, basename);
      ASSERT_NE(static_cast<X509Certificate*>(nullptr), ee_cert.get());

      basename = *signer_type + "-intermediate.pem";
      scoped_refptr<X509Certificate> intermediate =
          ImportCertFromFile(certs_dir, basename);
      ASSERT_NE(static_cast<X509Certificate*>(nullptr), intermediate.get());

      std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
      intermediates.push_back(bssl::UpRef(intermediate->cert_buffer()));
      scoped_refptr<X509Certificate> cert_chain =
          X509Certificate::CreateFromBuffer(bssl::UpRef(ee_cert->cert_buffer()),
                                            std::move(intermediates));
      ASSERT_TRUE(cert_chain);

      CertVerifyResult verify_result;
      int error = Verify(cert_chain.get(), "127.0.0.1", 0,
                         CRLSet::BuiltinCRLSet().get(), CertificateList(),
                         &verify_result);

      if (IsWeakKeyType(*ee_type) || IsWeakKeyType(*signer_type)) {
        EXPECT_NE(OK, error);
        EXPECT_EQ(CERT_STATUS_WEAK_KEY,
                  verify_result.cert_status & CERT_STATUS_WEAK_KEY);
        EXPECT_EQ(WeakKeysAreInvalid() ? CERT_STATUS_INVALID : 0,
                  verify_result.cert_status & CERT_STATUS_INVALID);
      } else {
        EXPECT_THAT(error, IsOk());
        EXPECT_EQ(0U, verify_result.cert_status & CERT_STATUS_WEAK_KEY);
      }
    }
  }
}

// Regression test for http://crbug.com/108514.
TEST_P(CertVerifyProcInternalTest, ExtraneousMD5RootCert) {
  if (!SupportsReturningVerifiedChain()) {
    LOG(INFO) << "Skipping this test in this platform.";
    return;
  }

  if (verify_proc_type() == CERT_VERIFY_PROC_MAC) {
    // Disabled on OS X - Security.framework doesn't ignore superflous
    // certificates provided by servers.
    // TODO(eroman): Is this still needed?
    LOG(INFO) << "Skipping this test as Security.framework doesn't ignore "
                 "superflous certificates provided by servers.";
    return;
  }

  base::FilePath certs_dir = GetTestCertsDirectory();

  scoped_refptr<X509Certificate> server_cert =
      ImportCertFromFile(certs_dir, "cross-signed-leaf.pem");
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), server_cert.get());

  scoped_refptr<X509Certificate> extra_cert =
      ImportCertFromFile(certs_dir, "cross-signed-root-md5.pem");
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), extra_cert.get());

  scoped_refptr<X509Certificate> root_cert =
      ImportCertFromFile(certs_dir, "cross-signed-root-sha256.pem");
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), root_cert.get());

  ScopedTestRoot scoped_root(root_cert.get());

  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(bssl::UpRef(extra_cert->cert_buffer()));
  scoped_refptr<X509Certificate> cert_chain = X509Certificate::CreateFromBuffer(
      bssl::UpRef(server_cert->cert_buffer()), std::move(intermediates));
  ASSERT_TRUE(cert_chain);

  CertVerifyResult verify_result;
  int flags = 0;
  int error =
      Verify(cert_chain.get(), "127.0.0.1", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());

  // The extra MD5 root should be discarded
  ASSERT_TRUE(verify_result.verified_cert.get());
  ASSERT_EQ(1u, verify_result.verified_cert->intermediate_buffers().size());
  EXPECT_TRUE(x509_util::CryptoBufferEqual(
      verify_result.verified_cert->intermediate_buffers().front().get(),
      root_cert->cert_buffer()));

  EXPECT_FALSE(verify_result.has_md5);
}

// Test for bug 94673.
TEST_P(CertVerifyProcInternalTest, GoogleDigiNotarTest) {
  base::FilePath certs_dir = GetTestCertsDirectory();

  scoped_refptr<X509Certificate> server_cert =
      ImportCertFromFile(certs_dir, "google_diginotar.pem");
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), server_cert.get());

  scoped_refptr<X509Certificate> intermediate_cert =
      ImportCertFromFile(certs_dir, "diginotar_public_ca_2025.pem");
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), intermediate_cert.get());

  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(bssl::UpRef(intermediate_cert->cert_buffer()));
  scoped_refptr<X509Certificate> cert_chain = X509Certificate::CreateFromBuffer(
      bssl::UpRef(server_cert->cert_buffer()), std::move(intermediates));
  ASSERT_TRUE(cert_chain);

  CertVerifyResult verify_result;
  int flags = CertVerifyProc::VERIFY_REV_CHECKING_ENABLED;
  int error =
      Verify(cert_chain.get(), "mail.google.com", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_NE(OK, error);

  // Now turn off revocation checking.  Certificate verification should still
  // fail.
  flags = 0;
  error =
      Verify(cert_chain.get(), "mail.google.com", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_NE(OK, error);
}

TEST_P(CertVerifyProcInternalTest, NameConstraintsOk) {
  CertificateList ca_cert_list =
      CreateCertificateListFromFile(GetTestCertsDirectory(), "root_ca_cert.pem",
                                    X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, ca_cert_list.size());
  ScopedTestRoot test_root(ca_cert_list[0].get());

  scoped_refptr<X509Certificate> leaf = CreateCertificateChainFromFile(
      GetTestCertsDirectory(), "name_constraint_good.pem",
      X509Certificate::FORMAT_AUTO);
  ASSERT_TRUE(leaf);
  ASSERT_EQ(0U, leaf->intermediate_buffers().size());

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(leaf.get(), "test.example.com", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  EXPECT_EQ(0U, verify_result.cert_status);

  error =
      Verify(leaf.get(), "foo.test2.example.com", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  EXPECT_EQ(0U, verify_result.cert_status);
}

// This fixture is for testing the verification of a certificate chain which
// has some sort of mismatched signature algorithm (i.e.
// Certificate.signatureAlgorithm and TBSCertificate.algorithm are different).
class CertVerifyProcInspectSignatureAlgorithmsTest : public ::testing::Test {
 protected:
  // In the test setup, SHA384 is given special treatment as an unknown
  // algorithm.
  static constexpr DigestAlgorithm kUnknownDigestAlgorithm =
      DigestAlgorithm::Sha384;

  struct CertParams {
    // Certificate.signatureAlgorithm
    DigestAlgorithm cert_algorithm;

    // TBSCertificate.algorithm
    DigestAlgorithm tbs_algorithm;
  };

  // On some platforms trying to import a certificate with mismatched signature
  // will fail. Consequently the rest of the tests can't be performed.
  WARN_UNUSED_RESULT bool SupportsImportingMismatchedAlgorithms() const {
#if defined(OS_IOS)
    LOG(INFO) << "Skipping test on iOS because certs with mismatched "
                 "algorithms cannot be imported";
    return false;
#elif defined(OS_MACOSX)
    if (base::mac::IsAtLeastOS10_12()) {
      LOG(INFO) << "Skipping test on macOS >= 10.12 because certs with "
                   "mismatched algorithms cannot be imported";
      return false;
    }
    return true;
#else
    return true;
#endif
  }

  // Shorthand for VerifyChain() where only the leaf's parameters need
  // to be specified.
  WARN_UNUSED_RESULT int VerifyLeaf(const CertParams& leaf_params) {
    return VerifyChain({// Target
                        leaf_params,
                        // Root
                        {DigestAlgorithm::Sha256, DigestAlgorithm::Sha256}});
  }

  // Shorthand for VerifyChain() where only the intermediate's parameters need
  // to be specified.
  WARN_UNUSED_RESULT int VerifyIntermediate(
      const CertParams& intermediate_params) {
    return VerifyChain({// Target
                        {DigestAlgorithm::Sha256, DigestAlgorithm::Sha256},
                        // Intermediate
                        intermediate_params,
                        // Root
                        {DigestAlgorithm::Sha256, DigestAlgorithm::Sha256}});
  }

  // Shorthand for VerifyChain() where only the root's parameters need to be
  // specified.
  WARN_UNUSED_RESULT int VerifyRoot(const CertParams& root_params) {
    return VerifyChain({// Target
                        {DigestAlgorithm::Sha256, DigestAlgorithm::Sha256},
                        // Intermediate
                        {DigestAlgorithm::Sha256, DigestAlgorithm::Sha256},
                        // Root
                        root_params});
  }

  // Manufactures a certificate chain where each certificate has the indicated
  // signature algorithms, and then returns the result of verifying this chain.
  //
  // TODO(eroman): Instead of building certificates at runtime, move their
  //               generation to external scripts.
  WARN_UNUSED_RESULT int VerifyChain(
      const std::vector<CertParams>& chain_params) {
    auto chain = CreateChain(chain_params);
    if (!chain) {
      ADD_FAILURE() << "Failed creating certificate chain";
      return ERR_UNEXPECTED;
    }

    int flags = 0;
    CertVerifyResult dummy_result;
    CertVerifyResult verify_result;

    scoped_refptr<CertVerifyProc> verify_proc =
        new MockCertVerifyProc(dummy_result);

    return verify_proc->Verify(
        chain.get(), "test.example.com", /*ocsp_response=*/std::string(),
        /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
        CertificateList(), &verify_result);
  }

 private:
  // Overwrites the AlgorithmIdentifier pointed to by |algorithm_sequence| with
  // |algorithm|. Note this violates the constness of StringPiece.
  WARN_UNUSED_RESULT static bool SetAlgorithmSequence(
      DigestAlgorithm algorithm,
      base::StringPiece* algorithm_sequence) {
    // This string of bytes is the full SEQUENCE for an AlgorithmIdentifier.
    std::vector<uint8_t> replacement_sequence;
    switch (algorithm) {
      case DigestAlgorithm::Sha1:
        // sha1WithRSAEncryption
        replacement_sequence = {0x30, 0x0D, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
                                0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00};
        break;
      case DigestAlgorithm::Sha256:
        // sha256WithRSAEncryption
        replacement_sequence = {0x30, 0x0D, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
                                0xf7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00};
        break;
      case kUnknownDigestAlgorithm:
        // This shouldn't be anything meaningful (modified numbers at random).
        replacement_sequence = {0x30, 0x0D, 0x06, 0x09, 0x8a, 0x87, 0x18, 0x46,
                                0xd7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00};
        break;
      default:
        ADD_FAILURE() << "Unsupported digest algorithm";
        return false;
    }

    // For this simple replacement to work (without modifying any
    // other sequence lengths) the original algorithm and replacement
    // algorithm must have the same encoded length.
    if (algorithm_sequence->size() != replacement_sequence.size()) {
      ADD_FAILURE() << "AlgorithmIdentifier must have length "
                    << replacement_sequence.size();
      return false;
    }

    memcpy(const_cast<char*>(algorithm_sequence->data()),
           replacement_sequence.data(), replacement_sequence.size());
    return true;
  }

  // Locate the serial number bytes.
  WARN_UNUSED_RESULT static bool ExtractSerialNumberFromDERCert(
      base::StringPiece der_cert,
      base::StringPiece* serial_value) {
    der::Parser parser((der::Input(der_cert)));
    der::Parser certificate;
    if (!parser.ReadSequence(&certificate))
      return false;

    der::Parser tbs_certificate;
    if (!certificate.ReadSequence(&tbs_certificate))
      return false;

    bool unused;
    if (!tbs_certificate.SkipOptionalTag(
            der::kTagConstructed | der::kTagContextSpecific | 0, &unused)) {
      return false;
    }

    // serialNumber
    der::Input serial_value_der;
    if (!tbs_certificate.ReadTag(der::kInteger, &serial_value_der))
      return false;

    *serial_value = serial_value_der.AsStringPiece();
    return true;
  }

  // Creates a certificate (based on some base certificate file) using the
  // specified signature algorithms.
  static scoped_refptr<X509Certificate> CreateCertificate(
      const CertParams& params) {
    // Dosn't really matter which base certificate is used, so long as it is
    // valid and uses a signature AlgorithmIdentifier with the same encoded
    // length as sha1WithRSASignature.
    const char* kLeafFilename = "name_constraint_good.pem";

    auto cert = CreateCertificateChainFromFile(
        GetTestCertsDirectory(), kLeafFilename, X509Certificate::FORMAT_AUTO);
    if (!cert) {
      ADD_FAILURE() << "Failed to load certificate: " << kLeafFilename;
      return nullptr;
    }

    // Start with the DER bytes of a valid certificate. The der data is copied
    // to a new std::string as it will modified to create a new certificate.
    std::string cert_der(
        x509_util::CryptoBufferAsStringPiece(cert->cert_buffer()));

    // Parse the certificate and identify the locations of interest within
    // |cert_der|.
    base::StringPiece cert_algorithm_sequence;
    base::StringPiece tbs_algorithm_sequence;
    if (!asn1::ExtractSignatureAlgorithmsFromDERCert(
            cert_der, &cert_algorithm_sequence, &tbs_algorithm_sequence)) {
      ADD_FAILURE() << "Failed parsing certificate algorithms";
      return nullptr;
    }

    base::StringPiece serial_value;
    if (!ExtractSerialNumberFromDERCert(cert_der, &serial_value)) {
      ADD_FAILURE() << "Failed parsing certificate serial number";
      return nullptr;
    }

    // Give each certificate a unique serial number based on its content (which
    // in turn is a function of |params|, otherwise importing it may fail.

    // Upper bound for last entry in DigestAlgorithm
    const int kNumDigestAlgorithms = 15;
    *const_cast<char*>(serial_value.data()) +=
        static_cast<int>(params.tbs_algorithm) * kNumDigestAlgorithms +
        static_cast<int>(params.cert_algorithm);

    // Change the signature AlgorithmIdentifiers.
    if (!SetAlgorithmSequence(params.cert_algorithm,
                              &cert_algorithm_sequence) ||
        !SetAlgorithmSequence(params.tbs_algorithm, &tbs_algorithm_sequence)) {
      return nullptr;
    }

    // NOTE: The signature is NOT recomputed over TBSCertificate -- for these
    // tests it isn't needed.
    return X509Certificate::CreateFromBytes(cert_der.data(), cert_der.size());
  }

  static scoped_refptr<X509Certificate> CreateChain(
      const std::vector<CertParams>& params) {
    // Manufacture a chain with the given combinations of signature algorithms.
    // This chain isn't actually a valid chain, but it is good enough for
    // testing the base CertVerifyProc.
    CertificateList certs;
    for (const auto& cert_params : params) {
      certs.push_back(CreateCertificate(cert_params));
      if (!certs.back())
        return nullptr;
    }

    std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
    for (size_t i = 1; i < certs.size(); ++i)
      intermediates.push_back(bssl::UpRef(certs[i]->cert_buffer()));

    return X509Certificate::CreateFromBuffer(
        bssl::UpRef(certs[0]->cert_buffer()), std::move(intermediates));
  }
};

// This is a control test to make sure that the test helper
// VerifyLeaf() works as expected. There is no actual mismatch in the
// algorithms used here.
//
//  Certificate.signatureAlgorithm:  sha1WithRSASignature
//  TBSCertificate.algorithm:        sha1WithRSAEncryption
TEST_F(CertVerifyProcInspectSignatureAlgorithmsTest, LeafSha1Sha1) {
  int rv = VerifyLeaf({DigestAlgorithm::Sha1, DigestAlgorithm::Sha1});
  ASSERT_THAT(rv, IsError(ERR_CERT_WEAK_SIGNATURE_ALGORITHM));
}

// This is a control test to make sure that the test helper
// VerifyLeaf() works as expected. There is no actual mismatch in the
// algorithms used here.
//
//  Certificate.signatureAlgorithm:  sha256WithRSASignature
//  TBSCertificate.algorithm:        sha256WithRSAEncryption
TEST_F(CertVerifyProcInspectSignatureAlgorithmsTest, LeafSha256Sha256) {
  int rv = VerifyLeaf({DigestAlgorithm::Sha256, DigestAlgorithm::Sha256});
  ASSERT_THAT(rv, IsOk());
}

// Mismatched signature algorithms in the leaf certificate.
//
//  Certificate.signatureAlgorithm:  sha1WithRSASignature
//  TBSCertificate.algorithm:        sha256WithRSAEncryption
TEST_F(CertVerifyProcInspectSignatureAlgorithmsTest, LeafSha1Sha256) {
  if (!SupportsImportingMismatchedAlgorithms())
    return;

  int rv = VerifyLeaf({DigestAlgorithm::Sha1, DigestAlgorithm::Sha256});
  ASSERT_THAT(rv, IsError(ERR_CERT_INVALID));
}

// Mismatched signature algorithms in the leaf certificate.
//
//  Certificate.signatureAlgorithm:  sha256WithRSAEncryption
//  TBSCertificate.algorithm:        sha1WithRSASignature
TEST_F(CertVerifyProcInspectSignatureAlgorithmsTest, LeafSha256Sha1) {
  if (!SupportsImportingMismatchedAlgorithms())
    return;

  int rv = VerifyLeaf({DigestAlgorithm::Sha256, DigestAlgorithm::Sha1});
  ASSERT_THAT(rv, IsError(ERR_CERT_INVALID));
}

// Unrecognized signature algorithm in the leaf certificate.
//
//  Certificate.signatureAlgorithm:  sha256WithRSAEncryption
//  TBSCertificate.algorithm:        ?
TEST_F(CertVerifyProcInspectSignatureAlgorithmsTest, LeafSha256Unknown) {
  if (!SupportsImportingMismatchedAlgorithms())
    return;

  int rv = VerifyLeaf({DigestAlgorithm::Sha256, kUnknownDigestAlgorithm});
  ASSERT_THAT(rv, IsError(ERR_CERT_INVALID));
}

// Unrecognized signature algorithm in the leaf certificate.
//
//  Certificate.signatureAlgorithm:  ?
//  TBSCertificate.algorithm:        sha256WithRSAEncryption
TEST_F(CertVerifyProcInspectSignatureAlgorithmsTest, LeafUnknownSha256) {
  if (!SupportsImportingMismatchedAlgorithms())
    return;

  int rv = VerifyLeaf({kUnknownDigestAlgorithm, DigestAlgorithm::Sha256});
  ASSERT_THAT(rv, IsError(ERR_CERT_INVALID));
}

// Mismatched signature algorithms in the intermediate certificate.
//
//  Certificate.signatureAlgorithm:  sha1WithRSASignature
//  TBSCertificate.algorithm:        sha256WithRSAEncryption
TEST_F(CertVerifyProcInspectSignatureAlgorithmsTest, IntermediateSha1Sha256) {
  if (!SupportsImportingMismatchedAlgorithms())
    return;

  int rv = VerifyIntermediate({DigestAlgorithm::Sha1, DigestAlgorithm::Sha256});
  ASSERT_THAT(rv, IsError(ERR_CERT_INVALID));
}

// Mismatched signature algorithms in the intermediate certificate.
//
//  Certificate.signatureAlgorithm:  sha256WithRSAEncryption
//  TBSCertificate.algorithm:        sha1WithRSASignature
TEST_F(CertVerifyProcInspectSignatureAlgorithmsTest, IntermediateSha256Sha1) {
  if (!SupportsImportingMismatchedAlgorithms())
    return;

  int rv = VerifyIntermediate({DigestAlgorithm::Sha256, DigestAlgorithm::Sha1});
  ASSERT_THAT(rv, IsError(ERR_CERT_INVALID));
}

// Mismatched signature algorithms in the root certificate.
//
//  Certificate.signatureAlgorithm:  sha256WithRSAEncryption
//  TBSCertificate.algorithm:        sha1WithRSASignature
TEST_F(CertVerifyProcInspectSignatureAlgorithmsTest, RootSha256Sha1) {
  if (!SupportsImportingMismatchedAlgorithms())
    return;

  int rv = VerifyRoot({DigestAlgorithm::Sha256, DigestAlgorithm::Sha1});
  ASSERT_THAT(rv, IsOk());
}

// Unrecognized signature algorithm in the root certificate.
//
//  Certificate.signatureAlgorithm:  ?
//  TBSCertificate.algorithm:        sha256WithRSAEncryption
TEST_F(CertVerifyProcInspectSignatureAlgorithmsTest, RootUnknownSha256) {
  if (!SupportsImportingMismatchedAlgorithms())
    return;

  int rv = VerifyRoot({kUnknownDigestAlgorithm, DigestAlgorithm::Sha256});
  ASSERT_THAT(rv, IsOk());
}

TEST_P(CertVerifyProcInternalTest, NameConstraintsFailure) {
  if (!SupportsReturningVerifiedChain()) {
    LOG(INFO) << "Skipping this test in this platform.";
    return;
  }

  CertificateList ca_cert_list =
      CreateCertificateListFromFile(GetTestCertsDirectory(), "root_ca_cert.pem",
                                    X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, ca_cert_list.size());
  ScopedTestRoot test_root(ca_cert_list[0].get());

  CertificateList cert_list = CreateCertificateListFromFile(
      GetTestCertsDirectory(), "name_constraint_bad.pem",
      X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, cert_list.size());

  scoped_refptr<X509Certificate> leaf = X509Certificate::CreateFromBuffer(
      bssl::UpRef(cert_list[0]->cert_buffer()), {});
  ASSERT_TRUE(leaf);

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(leaf.get(), "test.example.com", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_NAME_CONSTRAINT_VIOLATION));
  EXPECT_EQ(CERT_STATUS_NAME_CONSTRAINT_VIOLATION,
            verify_result.cert_status & CERT_STATUS_NAME_CONSTRAINT_VIOLATION);
}

TEST(CertVerifyProcTest, TestHasTooLongValidity) {
  struct {
    const char* const file;
    bool is_valid_too_long;
  } tests[] = {
      {"daltonridgeapts.com-chain.pem", false},
      {"start_after_expiry.pem", true},
      {"pre_br_validity_ok.pem", false},
      {"pre_br_validity_bad_121.pem", true},
      {"pre_br_validity_bad_2020.pem", true},
      {"10_year_validity.pem", false},
      {"11_year_validity.pem", true},
      {"39_months_after_2015_04.pem", false},
      {"40_months_after_2015_04.pem", true},
      {"60_months_after_2012_07.pem", false},
      {"61_months_after_2012_07.pem", true},
      {"825_days_after_2018_03_01.pem", false},
      {"826_days_after_2018_03_01.pem", true},
      {"825_days_1_second_after_2018_03_01.pem", true},
      {"39_months_based_on_last_day.pem", false},
  };

  base::FilePath certs_dir = GetTestCertsDirectory();

  for (const auto& test : tests) {
    SCOPED_TRACE(test.file);

    scoped_refptr<X509Certificate> certificate =
        ImportCertFromFile(certs_dir, test.file);
    ASSERT_TRUE(certificate);
    EXPECT_EQ(test.is_valid_too_long,
              CertVerifyProc::HasTooLongValidity(*certificate));
  }
}

TEST_P(CertVerifyProcInternalTest, TestKnownRoot) {
  base::FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> cert_chain = CreateCertificateChainFromFile(
      certs_dir, "daltonridgeapts.com-chain.pem", X509Certificate::FORMAT_AUTO);
  ASSERT_TRUE(cert_chain);

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(cert_chain.get(), "daltonridgeapts.com", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk()) << "This test relies on a real certificate that "
                             << "expires on May 28, 2021. If failing on/after "
                             << "that date, please disable and file a bug "
                             << "against rsleevi.";
  EXPECT_TRUE(verify_result.is_issued_by_known_root);
}

// This tests that on successful certificate verification,
// CertVerifyResult::public_key_hashes is filled with a SHA1 and SHA256 hash
// for each of the certificates in the chain.
TEST_P(CertVerifyProcInternalTest, PublicKeyHashes) {
  if (!SupportsReturningVerifiedChain()) {
    LOG(INFO) << "Skipping this test in this platform.";
    return;
  }

  base::FilePath certs_dir = GetTestCertsDirectory();
  CertificateList certs = CreateCertificateListFromFile(
      certs_dir, "x509_verify_results.chain.pem", X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(3U, certs.size());

  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(bssl::UpRef(certs[1]->cert_buffer()));
  intermediates.push_back(bssl::UpRef(certs[2]->cert_buffer()));

  ScopedTestRoot scoped_root(certs[2].get());
  scoped_refptr<X509Certificate> cert_chain = X509Certificate::CreateFromBuffer(
      bssl::UpRef(certs[0]->cert_buffer()), std::move(intermediates));
  ASSERT_TRUE(cert_chain);
  ASSERT_EQ(2U, cert_chain->intermediate_buffers().size());

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(cert_chain.get(), "127.0.0.1", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());

  EXPECT_EQ(3u, verify_result.public_key_hashes.size());

  // Convert |public_key_hashes| to strings for ease of comparison.
  std::vector<std::string> public_key_hash_strings;
  for (const auto& public_key_hash : verify_result.public_key_hashes)
    public_key_hash_strings.push_back(public_key_hash.ToString());

  std::vector<std::string> expected_public_key_hashes = {
      // Target
      "sha256/jpsUnwFFTO7e+l5zQDYhutkf7uA+dCVsWfRvv0UDX40=",

      // Intermediate
      "sha256/D9u0epgvPYlG9YiVp7V+IMT+xhUpB5BhsS/INjDXc4Y=",

      // Trust anchor
      "sha256/VypP3VWL7OaqTJ7mIBehWYlv8khPuFHpWiearZI2YjI="};

  // |public_key_hashes| does not have an ordering guarantee.
  EXPECT_THAT(expected_public_key_hashes,
              testing::UnorderedElementsAreArray(public_key_hash_strings));
}

// A regression test for http://crbug.com/70293.
// The certificate in question has a key purpose of clientAuth, and also lacks
// the required key usage for serverAuth.
TEST_P(CertVerifyProcInternalTest, WrongKeyPurpose) {
  base::FilePath certs_dir = GetTestCertsDirectory();

  scoped_refptr<X509Certificate> server_cert =
      ImportCertFromFile(certs_dir, "invalid_key_usage_cert.der");
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), server_cert.get());

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(server_cert.get(), "jira.aquameta.com", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);

  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_COMMON_NAME_INVALID);

  // TODO(crbug.com/649017): Don't special-case builtin verifier.
  if (verify_proc_type() != CERT_VERIFY_PROC_BUILTIN)
    EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_INVALID);

  // TODO(wtc): fix http://crbug.com/75520 to get all the certificate errors
  // from NSS.
  if (verify_proc_type() != CERT_VERIFY_PROC_NSS &&
      verify_proc_type() != CERT_VERIFY_PROC_ANDROID) {
    // The certificate is issued by an unknown CA.
    EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_AUTHORITY_INVALID);
  }

  // TODO(crbug.com/649017): Don't special-case builtin verifier.
  if (verify_proc_type() == CERT_VERIFY_PROC_BUILTIN) {
    EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
  } else {
    EXPECT_THAT(error, IsError(ERR_CERT_INVALID));
  }
}

// Tests that a Netscape Server Gated crypto is accepted in place of a
// serverAuth EKU.
// TODO(crbug.com/843735): Deprecate support for this.
TEST_P(CertVerifyProcInternalTest, Sha1IntermediateUsesServerGatedCrypto) {
  base::FilePath certs_dir =
      GetTestNetDataDirectory()
          .AppendASCII("verify_certificate_chain_unittest")
          .AppendASCII("intermediate-eku-server-gated-crypto");

  scoped_refptr<X509Certificate> cert_chain = CreateCertificateChainFromFile(
      certs_dir, "sha1-chain.pem", X509Certificate::FORMAT_AUTO);

  ASSERT_TRUE(cert_chain);
  ASSERT_FALSE(cert_chain->intermediate_buffers().empty());

  auto root = X509Certificate::CreateFromBuffer(
      bssl::UpRef(cert_chain->intermediate_buffers().back().get()), {});

  ScopedTestRoot scoped_root(root.get());

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(cert_chain.get(), "test.example", flags,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);

  if (AreSHA1IntermediatesAllowed()) {
    EXPECT_THAT(error, IsOk());
    EXPECT_EQ(CERT_STATUS_SHA1_SIGNATURE_PRESENT, verify_result.cert_status);
  } else {
    EXPECT_THAT(error, IsError(ERR_CERT_WEAK_SIGNATURE_ALGORITHM));
    EXPECT_EQ(CERT_STATUS_WEAK_SIGNATURE_ALGORITHM |
                  CERT_STATUS_SHA1_SIGNATURE_PRESENT,
              verify_result.cert_status);
  }
}

// Basic test for returning the chain in CertVerifyResult. Note that the
// returned chain may just be a reflection of the originally supplied chain;
// that is, if any errors occur, the default chain returned is an exact copy
// of the certificate to be verified. The remaining VerifyReturn* tests are
// used to ensure that the actual, verified chain is being returned by
// Verify().
TEST_P(CertVerifyProcInternalTest, VerifyReturnChainBasic) {
  if (!SupportsReturningVerifiedChain()) {
    LOG(INFO) << "Skipping this test in this platform.";
    return;
  }

  base::FilePath certs_dir = GetTestCertsDirectory();
  CertificateList certs = CreateCertificateListFromFile(
      certs_dir, "x509_verify_results.chain.pem", X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(3U, certs.size());

  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(bssl::UpRef(certs[1]->cert_buffer()));
  intermediates.push_back(bssl::UpRef(certs[2]->cert_buffer()));

  ScopedTestRoot scoped_root(certs[2].get());

  scoped_refptr<X509Certificate> google_full_chain =
      X509Certificate::CreateFromBuffer(bssl::UpRef(certs[0]->cert_buffer()),
                                        std::move(intermediates));
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), google_full_chain.get());
  ASSERT_EQ(2U, google_full_chain->intermediate_buffers().size());

  CertVerifyResult verify_result;
  EXPECT_EQ(static_cast<X509Certificate*>(nullptr),
            verify_result.verified_cert.get());
  int error =
      Verify(google_full_chain.get(), "127.0.0.1", 0,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  ASSERT_NE(static_cast<X509Certificate*>(nullptr),
            verify_result.verified_cert.get());

  EXPECT_NE(google_full_chain, verify_result.verified_cert);
  EXPECT_TRUE(
      x509_util::CryptoBufferEqual(google_full_chain->cert_buffer(),
                                   verify_result.verified_cert->cert_buffer()));
  const auto& return_intermediates =
      verify_result.verified_cert->intermediate_buffers();
  ASSERT_EQ(2U, return_intermediates.size());
  EXPECT_TRUE(x509_util::CryptoBufferEqual(return_intermediates[0].get(),
                                           certs[1]->cert_buffer()));
  EXPECT_TRUE(x509_util::CryptoBufferEqual(return_intermediates[1].get(),
                                           certs[2]->cert_buffer()));
}

// Test that certificates issued for 'intranet' names (that is, containing no
// known public registry controlled domain information) issued by well-known
// CAs are flagged appropriately, while certificates that are issued by
// internal CAs are not flagged.
TEST(CertVerifyProcTest, IntranetHostsRejected) {
  CertificateList cert_list = CreateCertificateListFromFile(
      GetTestCertsDirectory(), "reject_intranet_hosts.pem",
      X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, cert_list.size());
  scoped_refptr<X509Certificate> cert(cert_list[0]);

  CertVerifyResult verify_result;
  int error = 0;

  // Intranet names for public CAs should be flagged:
  CertVerifyResult dummy_result;
  dummy_result.is_issued_by_known_root = true;
  scoped_refptr<CertVerifyProc> verify_proc =
      new MockCertVerifyProc(dummy_result);
  error = verify_proc->Verify(
      cert.get(), "webmail", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), 0, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_NON_UNIQUE_NAME);

  // However, if the CA is not well known, these should not be flagged:
  dummy_result.Reset();
  dummy_result.is_issued_by_known_root = false;
  verify_proc = base::MakeRefCounted<MockCertVerifyProc>(dummy_result);
  error = verify_proc->Verify(
      cert.get(), "webmail", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), 0, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  EXPECT_FALSE(verify_result.cert_status & CERT_STATUS_NON_UNIQUE_NAME);
}

// Tests that certificates issued by Symantec's legacy infrastructure
// are rejected according to the policies outlined in
// https://security.googleblog.com/2017/09/chromes-plan-to-distrust-symantec.html
// unless the caller has explicitly disabled that enforcement.
TEST(CertVerifyProcTest, SymantecCertsRejected) {
  constexpr SHA256HashValue kSymantecHashValue = {
      {0xb2, 0xde, 0xf5, 0x36, 0x2a, 0xd3, 0xfa, 0xcd, 0x04, 0xbd, 0x29,
       0x04, 0x7a, 0x43, 0x84, 0x4f, 0x76, 0x70, 0x34, 0xea, 0x48, 0x92,
       0xf8, 0x0e, 0x56, 0xbe, 0xe6, 0x90, 0x24, 0x3e, 0x25, 0x02}};
  constexpr SHA256HashValue kGoogleHashValue = {
      {0xec, 0x72, 0x29, 0x69, 0xcb, 0x64, 0x20, 0x0a, 0xb6, 0x63, 0x8f,
       0x68, 0xac, 0x53, 0x8e, 0x40, 0xab, 0xab, 0x5b, 0x19, 0xa6, 0x48,
       0x56, 0x61, 0x04, 0x2a, 0x10, 0x61, 0xc4, 0x61, 0x27, 0x76}};

  // Test that certificates from the legacy Symantec infrastructure are
  // rejected:
  // - dec_2017.pem : A certificate issued after 2017-12-01, which is rejected
  //                  as of M65
  // - pre_june_2016.pem : A certificate issued prior to 2016-06-01, which is
  //                       rejected as of M66.
  for (const char* rejected_cert : {"dec_2017.pem", "pre_june_2016.pem"}) {
    scoped_refptr<X509Certificate> cert = CreateCertificateChainFromFile(
        GetTestCertsDirectory(), rejected_cert, X509Certificate::FORMAT_AUTO);
    ASSERT_TRUE(cert);

    scoped_refptr<CertVerifyProc> verify_proc;
    int error = 0;

    // Test that a legacy Symantec certificate is rejected.
    CertVerifyResult symantec_result;
    symantec_result.verified_cert = cert;
    symantec_result.public_key_hashes.push_back(HashValue(kSymantecHashValue));
    symantec_result.is_issued_by_known_root = true;
    verify_proc = base::MakeRefCounted<MockCertVerifyProc>(symantec_result);

    CertVerifyResult test_result_1;
    error = verify_proc->Verify(
        cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
        /*sct_list=*/std::string(), 0, CRLSet::BuiltinCRLSet().get(),
        CertificateList(), &test_result_1);
    EXPECT_THAT(error, IsError(ERR_CERT_SYMANTEC_LEGACY));
    EXPECT_TRUE(test_result_1.cert_status & CERT_STATUS_SYMANTEC_LEGACY);

    // ... Unless the Symantec cert chains through a whitelisted intermediate.
    CertVerifyResult whitelisted_result;
    whitelisted_result.verified_cert = cert;
    whitelisted_result.public_key_hashes.push_back(
        HashValue(kSymantecHashValue));
    whitelisted_result.public_key_hashes.push_back(HashValue(kGoogleHashValue));
    whitelisted_result.is_issued_by_known_root = true;
    verify_proc = base::MakeRefCounted<MockCertVerifyProc>(whitelisted_result);

    CertVerifyResult test_result_2;
    error = verify_proc->Verify(
        cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
        /*sct_list=*/std::string(), 0, CRLSet::BuiltinCRLSet().get(),
        CertificateList(), &test_result_2);
    EXPECT_THAT(error, IsOk());
    EXPECT_FALSE(test_result_2.cert_status & CERT_STATUS_AUTHORITY_INVALID);

    // ... Or the caller disabled enforcement of Symantec policies.
    CertVerifyResult test_result_3;
    error = verify_proc->Verify(
        cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
        /*sct_list=*/std::string(),
        CertVerifyProc::VERIFY_DISABLE_SYMANTEC_ENFORCEMENT,
        CRLSet::BuiltinCRLSet().get(), CertificateList(), &test_result_3);
    EXPECT_THAT(error, IsOk());
    EXPECT_FALSE(test_result_3.cert_status & CERT_STATUS_SYMANTEC_LEGACY);
  }

  // Test that certificates from the legacy Symantec infrastructure issued
  // after 2016-06-01 approriately accept or reject based on the base::Feature
  // flag.
  for (bool feature_flag_enabled : {false, true}) {
    base::test::ScopedFeatureList scoped_feature_list;
    scoped_feature_list.InitWithFeatureState(
        CertVerifyProc::kLegacySymantecPKIEnforcement, feature_flag_enabled);

    scoped_refptr<X509Certificate> cert = CreateCertificateChainFromFile(
        GetTestCertsDirectory(), "post_june_2016.pem",
        X509Certificate::FORMAT_AUTO);
    ASSERT_TRUE(cert);

    scoped_refptr<CertVerifyProc> verify_proc;
    int error = 0;

    // Test that a legacy Symantec certificate is rejected if the feature
    // flag is enabled, and accepted if it is not.
    CertVerifyResult symantec_result;
    symantec_result.verified_cert = cert;
    symantec_result.public_key_hashes.push_back(HashValue(kSymantecHashValue));
    symantec_result.is_issued_by_known_root = true;
    verify_proc = base::MakeRefCounted<MockCertVerifyProc>(symantec_result);

    CertVerifyResult test_result_1;
    error = verify_proc->Verify(
        cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
        /*sct_list=*/std::string(), 0, CRLSet::BuiltinCRLSet().get(),
        CertificateList(), &test_result_1);
    if (feature_flag_enabled) {
      EXPECT_THAT(error, IsError(ERR_CERT_SYMANTEC_LEGACY));
      EXPECT_TRUE(test_result_1.cert_status & CERT_STATUS_SYMANTEC_LEGACY);
    } else {
      EXPECT_THAT(error, IsOk());
      EXPECT_FALSE(test_result_1.cert_status & CERT_STATUS_SYMANTEC_LEGACY);
    }

    // ... Unless the Symantec cert chains through a whitelisted intermediate.
    CertVerifyResult whitelisted_result;
    whitelisted_result.verified_cert = cert;
    whitelisted_result.public_key_hashes.push_back(
        HashValue(kSymantecHashValue));
    whitelisted_result.public_key_hashes.push_back(HashValue(kGoogleHashValue));
    whitelisted_result.is_issued_by_known_root = true;
    verify_proc = base::MakeRefCounted<MockCertVerifyProc>(whitelisted_result);

    CertVerifyResult test_result_2;
    error = verify_proc->Verify(
        cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
        /*sct_list=*/std::string(), 0, CRLSet::BuiltinCRLSet().get(),
        CertificateList(), &test_result_2);
    EXPECT_THAT(error, IsOk());
    EXPECT_FALSE(test_result_2.cert_status & CERT_STATUS_AUTHORITY_INVALID);

    // ... Or the caller disabled enforcement of Symantec policies.
    CertVerifyResult test_result_3;
    error = verify_proc->Verify(
        cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
        /*sct_list=*/std::string(),
        CertVerifyProc::VERIFY_DISABLE_SYMANTEC_ENFORCEMENT,
        CRLSet::BuiltinCRLSet().get(), CertificateList(), &test_result_3);
    EXPECT_THAT(error, IsOk());
    EXPECT_FALSE(test_result_3.cert_status & CERT_STATUS_SYMANTEC_LEGACY);
  }
}

// Test that the certificate returned in CertVerifyResult is able to reorder
// certificates that are not ordered from end-entity to root. While this is
// a protocol violation if sent during a TLS handshake, if multiple sources
// of intermediate certificates are combined, it's possible that order may
// not be maintained.
TEST_P(CertVerifyProcInternalTest, VerifyReturnChainProperlyOrdered) {
  if (!SupportsReturningVerifiedChain()) {
    LOG(INFO) << "Skipping this test in this platform.";
    return;
  }

  base::FilePath certs_dir = GetTestCertsDirectory();
  CertificateList certs = CreateCertificateListFromFile(
      certs_dir, "x509_verify_results.chain.pem", X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(3U, certs.size());

  // Construct the chain out of order.
  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(bssl::UpRef(certs[2]->cert_buffer()));
  intermediates.push_back(bssl::UpRef(certs[1]->cert_buffer()));

  ScopedTestRoot scoped_root(certs[2].get());

  scoped_refptr<X509Certificate> google_full_chain =
      X509Certificate::CreateFromBuffer(bssl::UpRef(certs[0]->cert_buffer()),
                                        std::move(intermediates));
  ASSERT_TRUE(google_full_chain);
  ASSERT_EQ(2U, google_full_chain->intermediate_buffers().size());

  CertVerifyResult verify_result;
  EXPECT_FALSE(verify_result.verified_cert);
  int error =
      Verify(google_full_chain.get(), "127.0.0.1", 0,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  ASSERT_TRUE(verify_result.verified_cert);

  EXPECT_NE(google_full_chain, verify_result.verified_cert);
  EXPECT_TRUE(
      x509_util::CryptoBufferEqual(google_full_chain->cert_buffer(),
                                   verify_result.verified_cert->cert_buffer()));
  const auto& return_intermediates =
      verify_result.verified_cert->intermediate_buffers();
  ASSERT_EQ(2U, return_intermediates.size());
  EXPECT_TRUE(x509_util::CryptoBufferEqual(return_intermediates[0].get(),
                                           certs[1]->cert_buffer()));
  EXPECT_TRUE(x509_util::CryptoBufferEqual(return_intermediates[1].get(),
                                           certs[2]->cert_buffer()));
}

// Test that Verify() filters out certificates which are not related to
// or part of the certificate chain being verified.
TEST_P(CertVerifyProcInternalTest, VerifyReturnChainFiltersUnrelatedCerts) {
  if (!SupportsReturningVerifiedChain()) {
    LOG(INFO) << "Skipping this test in this platform.";
    return;
  }

  base::FilePath certs_dir = GetTestCertsDirectory();
  CertificateList certs = CreateCertificateListFromFile(
      certs_dir, "x509_verify_results.chain.pem", X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(3U, certs.size());
  ScopedTestRoot scoped_root(certs[2].get());

  scoped_refptr<X509Certificate> unrelated_certificate =
      ImportCertFromFile(certs_dir, "duplicate_cn_1.pem");
  scoped_refptr<X509Certificate> unrelated_certificate2 =
      ImportCertFromFile(certs_dir, "aia-cert.pem");
  ASSERT_NE(static_cast<X509Certificate*>(nullptr),
            unrelated_certificate.get());
  ASSERT_NE(static_cast<X509Certificate*>(nullptr),
            unrelated_certificate2.get());

  // Interject unrelated certificates into the list of intermediates.
  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(bssl::UpRef(unrelated_certificate->cert_buffer()));
  intermediates.push_back(bssl::UpRef(certs[1]->cert_buffer()));
  intermediates.push_back(bssl::UpRef(unrelated_certificate2->cert_buffer()));
  intermediates.push_back(bssl::UpRef(certs[2]->cert_buffer()));

  scoped_refptr<X509Certificate> google_full_chain =
      X509Certificate::CreateFromBuffer(bssl::UpRef(certs[0]->cert_buffer()),
                                        std::move(intermediates));
  ASSERT_TRUE(google_full_chain);
  ASSERT_EQ(4U, google_full_chain->intermediate_buffers().size());

  CertVerifyResult verify_result;
  EXPECT_FALSE(verify_result.verified_cert);
  int error =
      Verify(google_full_chain.get(), "127.0.0.1", 0,
             CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  ASSERT_TRUE(verify_result.verified_cert);

  EXPECT_NE(google_full_chain, verify_result.verified_cert);
  EXPECT_TRUE(
      x509_util::CryptoBufferEqual(google_full_chain->cert_buffer(),
                                   verify_result.verified_cert->cert_buffer()));
  const auto& return_intermediates =
      verify_result.verified_cert->intermediate_buffers();
  ASSERT_EQ(2U, return_intermediates.size());
  EXPECT_TRUE(x509_util::CryptoBufferEqual(return_intermediates[0].get(),
                                           certs[1]->cert_buffer()));
  EXPECT_TRUE(x509_util::CryptoBufferEqual(return_intermediates[1].get(),
                                           certs[2]->cert_buffer()));
}

TEST_P(CertVerifyProcInternalTest, AdditionalTrustAnchors) {
  if (!SupportsAdditionalTrustAnchors()) {
    LOG(INFO) << "Skipping this test in this platform.";
    return;
  }

  // |ca_cert| is the issuer of |cert|.
  CertificateList ca_cert_list =
      CreateCertificateListFromFile(GetTestCertsDirectory(), "root_ca_cert.pem",
                                    X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, ca_cert_list.size());
  scoped_refptr<X509Certificate> ca_cert(ca_cert_list[0]);

  CertificateList cert_list = CreateCertificateListFromFile(
      GetTestCertsDirectory(), "ok_cert.pem", X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, cert_list.size());
  scoped_refptr<X509Certificate> cert(cert_list[0]);

  // Verification of |cert| fails when |ca_cert| is not in the trust anchors
  // list.
  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(cert.get(), "127.0.0.1", flags, CRLSet::BuiltinCRLSet().get(),
             CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
  EXPECT_EQ(CERT_STATUS_AUTHORITY_INVALID, verify_result.cert_status);
  EXPECT_FALSE(verify_result.is_issued_by_additional_trust_anchor);

  // Now add the |ca_cert| to the |trust_anchors|, and verification should pass.
  CertificateList trust_anchors;
  trust_anchors.push_back(ca_cert);
  error = Verify(cert.get(), "127.0.0.1", flags, CRLSet::BuiltinCRLSet().get(),
                 trust_anchors, &verify_result);
  EXPECT_THAT(error, IsOk());
  EXPECT_EQ(0U, verify_result.cert_status);
  EXPECT_TRUE(verify_result.is_issued_by_additional_trust_anchor);

  // Clearing the |trust_anchors| makes verification fail again (the cache
  // should be skipped).
  error = Verify(cert.get(), "127.0.0.1", flags, CRLSet::BuiltinCRLSet().get(),
                 CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
  EXPECT_EQ(CERT_STATUS_AUTHORITY_INVALID, verify_result.cert_status);
  EXPECT_FALSE(verify_result.is_issued_by_additional_trust_anchor);
}

// Tests that certificates issued by user-supplied roots are not flagged as
// issued by a known root. This should pass whether or not the platform supports
// detecting known roots.
TEST_P(CertVerifyProcInternalTest, IsIssuedByKnownRootIgnoresTestRoots) {
  // Load root_ca_cert.pem into the test root store.
  ScopedTestRoot test_root(
      ImportCertFromFile(GetTestCertsDirectory(), "root_ca_cert.pem").get());

  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));

  // Verification should pass.
  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(cert.get(), "127.0.0.1", flags, CRLSet::BuiltinCRLSet().get(),
             CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  EXPECT_EQ(0U, verify_result.cert_status);
  // But should not be marked as a known root.
  EXPECT_FALSE(verify_result.is_issued_by_known_root);
}

// Test verification with a leaf that does not contain embedded SCTs, and which
// has a notBefore date after 2018/10/15, and with no |sct_list|.
// On recent macOS and iOS versions this should fail to verify.
// The iOS simulator has different verifier behavior than a real device, and
// verification succeeds on all currently available versions. If this test
// fails on iossim in the future, disable the test on TARGET_IPHONE_SIMULATOR
// and file a bug against mattm.
TEST_P(CertVerifyProcInternalTest, LeafNewerThan20181015NoScts) {
  scoped_refptr<X509Certificate> chain = CreateCertificateChainFromFile(
      GetTestCertsDirectory(), "treadclimber.pem",
      X509Certificate::FORMAT_PEM_CERT_SEQUENCE);
  ASSERT_TRUE(chain);
  if (base::Time::Now() > chain->valid_expiry()) {
    FAIL() << "This test uses a certificate chain which is now expired. Please "
              "disable and file a bug against mattm.";
    return;
  }

  // Verification should pass, except on recent macOS / iOS systems.
  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc()->Verify(
      chain.get(), "treadclimber.com", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);

#if defined(OS_IOS) && !TARGET_IPHONE_SIMULATOR
  if (verify_proc_type() == CERT_VERIFY_PROC_IOS) {
    if (__builtin_available(iOS 12.2, *)) {
      // TODO(mattm): Check if this can this be mapped to some better error.
      EXPECT_THAT(error, IsError(ERR_CERT_INVALID));
      EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_INVALID);
      return;
    }
  }
#elif defined(OS_MACOSX)
  if (verify_proc_type() == CERT_VERIFY_PROC_MAC) {
    if (__builtin_available(macOS 10.14.2, *)) {
      // TODO(mattm): SecTrustEvaluate just gives a generic
      // CSSMERR_TP_VERIFY_ACTION_FAILED error. Not sure there's much that
      // could be done about that.
      EXPECT_THAT(error, IsError(ERR_CERT_INVALID));
      EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_INVALID);
      return;
    }
  }
#endif
  // On older macOS and iOS versions, and all other verifiers, verification
  // should succeed:
  EXPECT_THAT(error, IsOk());
  EXPECT_EQ(0U, verify_result.cert_status);
  EXPECT_TRUE(verify_result.is_issued_by_known_root);
}

// Test verification with a leaf that does not contain embedded SCTs, and which
// has a notBefore date after 2018/10/15, and passing a valid |sct_list| to
// Verify(). Verification should succeed on all platforms. (Assuming the
// verifier trusts the SCT Logs used in |sct_list|.)
TEST_P(CertVerifyProcInternalTest, LeafNewerThan20181015WithTlsSctList) {
  scoped_refptr<X509Certificate> chain = CreateCertificateChainFromFile(
      GetTestCertsDirectory(), "treadclimber.pem",
      X509Certificate::FORMAT_PEM_CERT_SEQUENCE);
  ASSERT_TRUE(chain);
  if (base::Time::Now() > chain->valid_expiry()) {
    FAIL() << "This test uses a certificate chain which is now expired. Please "
              "disable and file a bug against mattm.";
    return;
  }

  std::string sct_list;
  ASSERT_TRUE(base::ReadFileToString(
      GetTestCertsDirectory().AppendASCII("treadclimber.sctlist"), &sct_list));

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc()->Verify(chain.get(), "treadclimber.com",
                                    /*ocsp_response=*/std::string(), sct_list,
                                    flags, CRLSet::BuiltinCRLSet().get(),
                                    CertificateList(), &verify_result);

  // Since the valid |sct_list| was passed to Verify, verification should
  // succeed on all verifiers and OS versions.
  EXPECT_THAT(error, IsOk());
  EXPECT_EQ(0U, verify_result.cert_status);
  EXPECT_TRUE(verify_result.is_issued_by_known_root);
}

// Test that CRLSets are effective in making a certificate appear to be
// revoked.
TEST_P(CertVerifyProcInternalTest, CRLSet) {
  if (!SupportsCRLSet()) {
    LOG(INFO) << "Skipping test as verifier doesn't support CRLSet";
    return;
  }

  CertificateList ca_cert_list =
      CreateCertificateListFromFile(GetTestCertsDirectory(), "root_ca_cert.pem",
                                    X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, ca_cert_list.size());
  ScopedTestRoot test_root(ca_cert_list[0].get());

  CertificateList cert_list = CreateCertificateListFromFile(
      GetTestCertsDirectory(), "ok_cert.pem", X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, cert_list.size());
  scoped_refptr<X509Certificate> cert(cert_list[0]);

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(cert.get(), "127.0.0.1", flags, CRLSet::BuiltinCRLSet().get(),
             CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  EXPECT_EQ(0U, verify_result.cert_status);

  scoped_refptr<CRLSet> crl_set;
  std::string crl_set_bytes;

  // First test blocking by SPKI.
  EXPECT_TRUE(base::ReadFileToString(
      GetTestCertsDirectory().AppendASCII("crlset_by_leaf_spki.raw"),
      &crl_set_bytes));
  ASSERT_TRUE(CRLSet::Parse(crl_set_bytes, &crl_set));

  error = Verify(cert.get(), "127.0.0.1", flags, crl_set.get(),
                 CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_REVOKED));

  // Second, test revocation by serial number of a cert directly under the
  // root.
  crl_set_bytes.clear();
  EXPECT_TRUE(base::ReadFileToString(
      GetTestCertsDirectory().AppendASCII("crlset_by_root_serial.raw"),
      &crl_set_bytes));
  ASSERT_TRUE(CRLSet::Parse(crl_set_bytes, &crl_set));

  error = Verify(cert.get(), "127.0.0.1", flags, crl_set.get(),
                 CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_REVOKED));
}

TEST_P(CertVerifyProcInternalTest, CRLSetLeafSerial) {
  if (!SupportsCRLSet()) {
    LOG(INFO) << "Skipping test as verifier doesn't support CRLSet";
    return;
  }

  CertificateList ca_cert_list =
      CreateCertificateListFromFile(GetTestCertsDirectory(), "root_ca_cert.pem",
                                    X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, ca_cert_list.size());
  ScopedTestRoot test_root(ca_cert_list[0].get());

  scoped_refptr<X509Certificate> leaf = CreateCertificateChainFromFile(
      GetTestCertsDirectory(), "ok_cert_by_intermediate.pem",
      X509Certificate::FORMAT_AUTO);
  ASSERT_TRUE(leaf);
  ASSERT_EQ(1U, leaf->intermediate_buffers().size());

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(leaf.get(), "127.0.0.1", flags, CRLSet::BuiltinCRLSet().get(),
             CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());

  // Test revocation by serial number of a certificate not under the root.
  scoped_refptr<CRLSet> crl_set;
  std::string crl_set_bytes;
  ASSERT_TRUE(base::ReadFileToString(
      GetTestCertsDirectory().AppendASCII("crlset_by_intermediate_serial.raw"),
      &crl_set_bytes));
  ASSERT_TRUE(CRLSet::Parse(crl_set_bytes, &crl_set));

  error = Verify(leaf.get(), "127.0.0.1", flags, crl_set.get(),
                 CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_REVOKED));
}

// Tests that CertVerifyProc implementations apply CRLSet revocations by
// subject.
TEST_P(CertVerifyProcInternalTest, CRLSetRevokedBySubject) {
  if (!SupportsCRLSet()) {
    LOG(INFO) << "Skipping test as verifier doesn't support CRLSet";
    return;
  }

  scoped_refptr<X509Certificate> root(
      ImportCertFromFile(GetTestCertsDirectory(), "root_ca_cert.pem"));
  ASSERT_TRUE(root);

  scoped_refptr<X509Certificate> leaf(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(leaf);

  ScopedTestRoot scoped_root(root.get());

  int flags = 0;
  CertVerifyResult verify_result;

  // Confirm that verifying the certificate chain with an empty CRLSet succeeds.
  scoped_refptr<CRLSet> crl_set = CRLSet::EmptyCRLSetForTesting();
  int error = Verify(leaf.get(), "127.0.0.1", flags, crl_set.get(),
                     CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());

  std::string crl_set_bytes;

  // Revoke the leaf by subject. Verification should now fail.
  ASSERT_TRUE(base::ReadFileToString(
      GetTestCertsDirectory().AppendASCII("crlset_by_leaf_subject_no_spki.raw"),
      &crl_set_bytes));
  ASSERT_TRUE(CRLSet::Parse(crl_set_bytes, &crl_set));

  error = Verify(leaf.get(), "127.0.0.1", flags, crl_set.get(),
                 CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_REVOKED));

  // Revoke the root by subject. Verification should now fail.
  ASSERT_TRUE(base::ReadFileToString(
      GetTestCertsDirectory().AppendASCII("crlset_by_root_subject_no_spki.raw"),
      &crl_set_bytes));
  ASSERT_TRUE(CRLSet::Parse(crl_set_bytes, &crl_set));

  error = Verify(leaf.get(), "127.0.0.1", flags, crl_set.get(),
                 CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_REVOKED));

  // Revoke the leaf by subject, but only if the SPKI doesn't match the given
  // one. Verification should pass when using the certificate's actual SPKI.
  ASSERT_TRUE(base::ReadFileToString(
      GetTestCertsDirectory().AppendASCII("crlset_by_root_subject.raw"),
      &crl_set_bytes));
  ASSERT_TRUE(CRLSet::Parse(crl_set_bytes, &crl_set));

  error = Verify(leaf.get(), "127.0.0.1", flags, crl_set.get(),
                 CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
}

// Tests that CRLSets participate in path building functions, and that as
// long as a valid path exists within the verification graph, verification
// succeeds.
//
// In this test, there are two roots (D and E), and three possible paths
// to validate a leaf (A):
// 1. A(B) -> B(C) -> C(D) -> D(D)
// 2. A(B) -> B(C) -> C(E) -> E(E)
// 3. A(B) -> B(F) -> F(E) -> E(E)
//
// Each permutation of revocation is tried:
// 1. Revoking E by SPKI, so that only Path 1 is valid (as E is in Paths 2 & 3)
// 2. Revoking C(D) and F(E) by serial, so that only Path 2 is valid.
// 3. Revoking C by SPKI, so that only Path 3 is valid (as C is in Paths 1 & 2)
TEST_P(CertVerifyProcInternalTest, CRLSetDuringPathBuilding) {
  if (!SupportsCRLSetsInPathBuilding()) {
    LOG(INFO) << "Skipping this test on this platform.";
    return;
  }

  CertificateList path_1_certs;
  ASSERT_TRUE(
      LoadCertificateFiles({"multi-root-A-by-B.pem", "multi-root-B-by-C.pem",
                            "multi-root-C-by-D.pem", "multi-root-D-by-D.pem"},
                           &path_1_certs));

  CertificateList path_2_certs;
  ASSERT_TRUE(
      LoadCertificateFiles({"multi-root-A-by-B.pem", "multi-root-B-by-C.pem",
                            "multi-root-C-by-E.pem", "multi-root-E-by-E.pem"},
                           &path_2_certs));

  CertificateList path_3_certs;
  ASSERT_TRUE(
      LoadCertificateFiles({"multi-root-A-by-B.pem", "multi-root-B-by-F.pem",
                            "multi-root-F-by-E.pem", "multi-root-E-by-E.pem"},
                           &path_3_certs));

  // Add D and E as trust anchors.
  ScopedTestRoot test_root_D(path_1_certs[3].get());  // D-by-D
  ScopedTestRoot test_root_E(path_2_certs[3].get());  // E-by-E

  // Create a chain that contains all the certificate paths possible.
  // CertVerifyProcInternalTest.VerifyReturnChainFiltersUnrelatedCerts already
  // ensures that it's safe to send additional certificates as inputs, and
  // that they're ignored if not necessary.
  // This is to avoid relying on AIA or internal object caches when
  // interacting with the underlying library.
  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(
      bssl::UpRef(path_1_certs[1]->cert_buffer()));  // B-by-C
  intermediates.push_back(
      bssl::UpRef(path_1_certs[2]->cert_buffer()));  // C-by-D
  intermediates.push_back(
      bssl::UpRef(path_2_certs[2]->cert_buffer()));  // C-by-E
  intermediates.push_back(
      bssl::UpRef(path_3_certs[1]->cert_buffer()));  // B-by-F
  intermediates.push_back(
      bssl::UpRef(path_3_certs[2]->cert_buffer()));  // F-by-E
  scoped_refptr<X509Certificate> cert = X509Certificate::CreateFromBuffer(
      bssl::UpRef(path_1_certs[0]->cert_buffer()), std::move(intermediates));
  ASSERT_TRUE(cert);

  struct TestPermutations {
    const char* crlset;
    bool expect_valid;
    scoped_refptr<X509Certificate> expected_intermediate;
  } kTests[] = {
      {"multi-root-crlset-D-and-E.raw", false, nullptr},
      {"multi-root-crlset-E.raw", true, path_1_certs[2].get()},
      {"multi-root-crlset-CD-and-FE.raw", true, path_2_certs[2].get()},
      {"multi-root-crlset-C.raw", true, path_3_certs[2].get()},
      {"multi-root-crlset-unrelated.raw", true, nullptr}};

  for (const auto& testcase : kTests) {
    SCOPED_TRACE(testcase.crlset);
    scoped_refptr<CRLSet> crl_set;
    std::string crl_set_bytes;
    EXPECT_TRUE(base::ReadFileToString(
        GetTestCertsDirectory().AppendASCII(testcase.crlset), &crl_set_bytes));
    ASSERT_TRUE(CRLSet::Parse(crl_set_bytes, &crl_set));

    int flags = 0;
    CertVerifyResult verify_result;
    int error = Verify(cert.get(), "127.0.0.1", flags, crl_set.get(),
                       CertificateList(), &verify_result);

    if (!testcase.expect_valid) {
      EXPECT_NE(OK, error);
      EXPECT_NE(0U, verify_result.cert_status);
      continue;
    }

    ASSERT_THAT(error, IsOk());
    ASSERT_EQ(0U, verify_result.cert_status);
    ASSERT_TRUE(verify_result.verified_cert.get());

    if (!testcase.expected_intermediate)
      continue;

    const auto& verified_intermediates =
        verify_result.verified_cert->intermediate_buffers();
    ASSERT_EQ(3U, verified_intermediates.size());

    scoped_refptr<X509Certificate> intermediate =
        X509Certificate::CreateFromBuffer(
            bssl::UpRef(verified_intermediates[1].get()), {});
    ASSERT_TRUE(intermediate);

    EXPECT_TRUE(testcase.expected_intermediate->EqualsExcludingChain(
        intermediate.get()))
        << "Expected: " << testcase.expected_intermediate->subject().common_name
        << " issued by " << testcase.expected_intermediate->issuer().common_name
        << "; Got: " << intermediate->subject().common_name << " issued by "
        << intermediate->issuer().common_name;
  }
}

class CertVerifyProcNameNormalizationTest : public CertVerifyProcInternalTest {
 protected:
  void SetUp() override {
    CertVerifyProcInternalTest::SetUp();

    scoped_refptr<X509Certificate> root_cert =
        ImportCertFromFile(GetTestCertsDirectory(), "ocsp-test-root.pem");
    ASSERT_TRUE(root_cert);
    test_root_.reset(new ScopedTestRoot(root_cert.get()));
  }

  std::string HistogramName() const {
    std::string prefix("Net.CertVerifier.NameNormalizationPrivateRoots.");
    switch (verify_proc_type()) {
      case CERT_VERIFY_PROC_NSS:
        return prefix + "NSS";
      case CERT_VERIFY_PROC_ANDROID:
        return prefix + "Android";
      case CERT_VERIFY_PROC_IOS:
        return prefix + "IOS";
      case CERT_VERIFY_PROC_MAC:
        return prefix + "Mac";
      case CERT_VERIFY_PROC_WIN:
        return prefix + "Win";
      case CERT_VERIFY_PROC_BUILTIN:
        return prefix + "Builtin";
    }
  }

  void ExpectNormalizationHistogram(int verify_error) {
    if (verify_error == OK) {
      histograms_.ExpectUniqueSample(
          HistogramName(), CertVerifyProc::NameNormalizationResult::kNormalized,
          1);
    } else {
      histograms_.ExpectTotalCount(HistogramName(), 0);
    }
  }

  void ExpectByteEqualHistogram() {
    histograms_.ExpectUniqueSample(
        HistogramName(), CertVerifyProc::NameNormalizationResult::kByteEqual,
        1);
  }

 private:
  std::unique_ptr<ScopedTestRoot> test_root_;
  base::HistogramTester histograms_;
};

INSTANTIATE_TEST_SUITE_P(,
                         CertVerifyProcNameNormalizationTest,
                         testing::ValuesIn(kAllCertVerifiers),
                         VerifyProcTypeToName);

// Tries to verify a chain where the leaf's issuer CN is PrintableString, while
// the intermediate's subject CN is UTF8String, and verifies the proper
// histogram is logged.
TEST_P(CertVerifyProcNameNormalizationTest, StringType) {
  scoped_refptr<X509Certificate> chain = CreateCertificateChainFromFile(
      GetTestCertsDirectory(), "name-normalization-printable-utf8.pem",
      X509Certificate::FORMAT_PEM_CERT_SEQUENCE);
  ASSERT_TRUE(chain);

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(chain.get(), "example.test", flags, CRLSet::BuiltinCRLSet().get(),
             CertificateList(), &verify_result);

  switch (verify_proc_type()) {
    case CERT_VERIFY_PROC_NSS:
    case CERT_VERIFY_PROC_IOS:
    case CERT_VERIFY_PROC_MAC:
    case CERT_VERIFY_PROC_WIN:
      EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
      break;
    case CERT_VERIFY_PROC_ANDROID:
    case CERT_VERIFY_PROC_BUILTIN:
      EXPECT_THAT(error, IsOk());
      break;
  }

  ExpectNormalizationHistogram(error);
}

// Tries to verify a chain where the leaf's issuer CN and intermediate's
// subject CN are both PrintableString but have differing case on the first
// character, and verifies the proper histogram is logged.
TEST_P(CertVerifyProcNameNormalizationTest, CaseFolding) {
  scoped_refptr<X509Certificate> chain = CreateCertificateChainFromFile(
      GetTestCertsDirectory(), "name-normalization-case-folding.pem",
      X509Certificate::FORMAT_PEM_CERT_SEQUENCE);
  ASSERT_TRUE(chain);

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(chain.get(), "example.test", flags, CRLSet::BuiltinCRLSet().get(),
             CertificateList(), &verify_result);

  switch (verify_proc_type()) {
    case CERT_VERIFY_PROC_NSS:
    case CERT_VERIFY_PROC_WIN:
      EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
      break;
    case CERT_VERIFY_PROC_ANDROID:
    case CERT_VERIFY_PROC_IOS:
    case CERT_VERIFY_PROC_MAC:
    case CERT_VERIFY_PROC_BUILTIN:
      EXPECT_THAT(error, IsOk());
      break;
  }

  ExpectNormalizationHistogram(error);
}

// Confirms that a chain generated by the generate-name-normalization-certs.py
// script which does not require normalization validates ok, and that the
// ByteEqual histogram is logged.
TEST_P(CertVerifyProcNameNormalizationTest, ByteEqual) {
  scoped_refptr<X509Certificate> chain = CreateCertificateChainFromFile(
      GetTestCertsDirectory(), "name-normalization-byteequal.pem",
      X509Certificate::FORMAT_PEM_CERT_SEQUENCE);
  ASSERT_TRUE(chain);

  int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(chain.get(), "example.test", flags, CRLSet::BuiltinCRLSet().get(),
             CertificateList(), &verify_result);

  EXPECT_THAT(error, IsOk());
  ExpectByteEqualHistogram();
}

// This is the same as CertVerifyProcInternalTest, but it additionally sets up
// networking capabilities for the cert verifiers, and a test server that can be
// used to serve mock responses for AIA/OCSP/CRL.
//
// An actual HTTP test server is used rather than simply mocking the network
// layer, since the certificate fetching networking layer is not mockable for
// all of the cert verifier implementations.
//
// The approach taken in this test fixture is to generate certificates
// on the fly so they use randomly chosen URLs, subjects, and serial
// numbers, in order to defeat global caching effects from the platform
// verifiers. Moreover, the AIA needs to be chosen dynamically since the
// test server's port number cannot be known statically.
class CertVerifyProcInternalWithNetFetchingTest
    : public CertVerifyProcInternalTest {
 protected:
  CertVerifyProcInternalWithNetFetchingTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::DEFAULT) {}

  void SetUp() override {
    // Create a network thread to be used for network fetches, and wait for
    // initialization to complete on that thread.
    base::Thread::Options options(base::MessageLoop::TYPE_IO, 0);
    network_thread_ = std::make_unique<base::Thread>("network_thread");
    CHECK(network_thread_->StartWithOptions(options));

    base::WaitableEvent initialization_complete_event(
        base::WaitableEvent::ResetPolicy::MANUAL,
        base::WaitableEvent::InitialState::NOT_SIGNALED);
    network_thread_->task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&SetUpOnNetworkThread, &context_, &cert_net_fetcher_,
                       &initialization_complete_event));
    initialization_complete_event.Wait();
    EXPECT_TRUE(cert_net_fetcher_);

    CertVerifyProcInternalTest::SetUpWithCertNetFetcher(cert_net_fetcher_);

    EXPECT_FALSE(test_server_.Started());

    // Register a single request handler with the EmbeddedTestServer, that in
    // turn dispatches to the internally managed registry of request handlers.
    //
    // This allows registering subsequent handlers dynamically during the course
    // of the test, since EmbeddedTestServer requires its handlers be registered
    // prior to Start().
    test_server_.RegisterRequestHandler(base::BindRepeating(
        &CertVerifyProcInternalWithNetFetchingTest::DispatchToRequestHandler,
        base::Unretained(this)));
    EXPECT_TRUE(test_server_.Start());
  }

  void TearDown() override {
    // Do cleanup on network thread.
    network_thread_->task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&ShutdownOnNetworkThread, &context_,
                                  &cert_net_fetcher_));
    network_thread_->Stop();
    network_thread_.reset();

    CertVerifyProcInternalTest::TearDown();
  }

  // Registers a handler with the test server that responds with the given
  // Content-Type, HTTP status code, and response body, for GET requests
  // to |path|.
  void RegisterSimpleTestServerHandler(std::string path,
                                       HttpStatusCode status_code,
                                       std::string content_type,
                                       std::string content) {
    base::AutoLock lock(request_handlers_lock_);
    request_handlers_.push_back(base::BindRepeating(
        &SimpleTestServerHandler, std::move(path), status_code,
        std::move(content_type), std::move(content)));
  }

  // Returns a random URL path (starting with /) that has the given suffix.
  static std::string MakeRandomPath(base::StringPiece suffix) {
    return "/" + MakeRandomHexString(12) + suffix.as_string();
  }

  // Returns a URL to |path| for the current test server.
  GURL GetTestServerAbsoluteUrl(const std::string& path) {
    return test_server_.GetURL(path);
  }

  // Creates a certificate chain for www.example.com, where the leaf certificate
  // has an AIA URL pointing to the test server.
  void CreateSimpleChainWithAIA(
      scoped_refptr<X509Certificate>* out_leaf,
      std::string* ca_issuers_path,
      bssl::UniquePtr<CRYPTO_BUFFER>* out_intermediate,
      scoped_refptr<X509Certificate>* out_root) {
    const char kHostname[] = "www.example.com";

    base::FilePath certs_dir =
        GetTestNetDataDirectory()
            .AppendASCII("verify_certificate_chain_unittest")
            .AppendASCII("target-and-intermediate");

    CertificateList orig_certs = CreateCertificateListFromFile(
        certs_dir, "chain.pem", X509Certificate::FORMAT_AUTO);
    ASSERT_EQ(3U, orig_certs.size());

    // Build a slightly modified variant of |orig_certs|.
    CertBuilder root(orig_certs[2]->cert_buffer(), nullptr);
    CertBuilder intermediate(orig_certs[1]->cert_buffer(), &root);
    CertBuilder leaf(orig_certs[0]->cert_buffer(), &intermediate);

    // Make the leaf certificate have an AIA (CA Issuers) that points to the
    // embedded test server. This uses a random URL for predictable behavior in
    // the presence of global caching.
    *ca_issuers_path = MakeRandomPath(".cer");
    GURL ca_issuers_url = GetTestServerAbsoluteUrl(*ca_issuers_path);
    leaf.SetCaIssuersUrl(ca_issuers_url);
    leaf.SetSubjectAltName(kHostname);

    // The chain being verified is solely the leaf certificate (missing the
    // intermediate and root).
    *out_leaf = leaf.GetX509Certificate();
    *out_root = root.GetX509Certificate();
    *out_intermediate = intermediate.DupCertBuffer();
  }

 private:
  std::unique_ptr<test_server::HttpResponse> DispatchToRequestHandler(
      const test_server::HttpRequest& request) {
    // Called on the embedded test server's IO thread.
    base::AutoLock lock(request_handlers_lock_);
    for (const auto& handler : request_handlers_) {
      auto response = handler.Run(request);
      if (response)
        return response;
    }

    return nullptr;
  }

  // Serves (|status_code|, |content_type|, |content|) in response to GET
  // requests for |path|.
  static std::unique_ptr<test_server::HttpResponse> SimpleTestServerHandler(
      const std::string& path,
      HttpStatusCode status_code,
      const std::string& content_type,
      const std::string& content,
      const test_server::HttpRequest& request) {
    if (request.relative_url != path)
      return nullptr;

    auto http_response = std::make_unique<test_server::BasicHttpResponse>();

    http_response->set_code(status_code);
    http_response->set_content_type(content_type);
    http_response->set_content(content);
    return http_response;
  }

  static void SetUpOnNetworkThread(
      std::unique_ptr<URLRequestContext>* context,
      scoped_refptr<CertNetFetcherImpl>* cert_net_fetcher,
      base::WaitableEvent* initialization_complete_event) {
    URLRequestContextBuilder url_request_context_builder;
    url_request_context_builder.set_user_agent("cert_verify_proc_unittest/0.1");
    url_request_context_builder.set_proxy_config_service(
        std::make_unique<ProxyConfigServiceFixed>(ProxyConfigWithAnnotation()));
    *context = url_request_context_builder.Build();

#if defined(USE_NSS_CERTS)
    SetURLRequestContextForNSSHttpIO(context->get());
#endif
    *cert_net_fetcher = base::MakeRefCounted<net::CertNetFetcherImpl>();
    (*cert_net_fetcher)->SetURLRequestContext(context->get());
    initialization_complete_event->Signal();
  }

  static void ShutdownOnNetworkThread(
      std::unique_ptr<URLRequestContext>* context,
      scoped_refptr<net::CertNetFetcherImpl>* cert_net_fetcher) {
#if defined(USE_NSS_CERTS)
    SetURLRequestContextForNSSHttpIO(nullptr);
#endif
    (*cert_net_fetcher)->Shutdown();
    cert_net_fetcher->reset();
    context->reset();
  }

  base::test::ScopedTaskEnvironment scoped_task_environment_;

  std::unique_ptr<base::Thread> network_thread_;

  // Owned by this thread, but initialized, used, and shutdown on the network
  // thread.
  std::unique_ptr<URLRequestContext> context_;
  scoped_refptr<CertNetFetcherImpl> cert_net_fetcher_;

  EmbeddedTestServer test_server_;

  // The list of registered handlers. Can only be accessed when the lock is
  // held, as this data is shared between the embedded server's IO thread, and
  // the test main thread.
  base::Lock request_handlers_lock_;
  std::vector<test_server::EmbeddedTestServer::HandleRequestCallback>
      request_handlers_;
};

INSTANTIATE_TEST_SUITE_P(,
                         CertVerifyProcInternalWithNetFetchingTest,
                         testing::ValuesIn(kAllCertVerifiers),
                         VerifyProcTypeToName);

// Tries verifying a certificate chain that is missing an intermediate. The
// intermediate is available via AIA, however the server responds with a 404.
//
// NOTE: This test is separate from IntermediateFromAia200 as a different URL
// needs to be used to avoid having the result depend on globally cached success
// or failure of the fetch.
// Test is flaky on iOS crbug.com/860189
#if defined(OS_IOS)
#define MAYBE_IntermediateFromAia404 DISABLED_IntermediateFromAia404
#else
#define MAYBE_IntermediateFromAia404 IntermediateFromAia404
#endif
TEST_P(CertVerifyProcInternalWithNetFetchingTest, MAYBE_IntermediateFromAia404) {
  const char kHostname[] = "www.example.com";

  // Create a chain where the leaf has an AIA that points to test server.
  scoped_refptr<X509Certificate> leaf;
  std::string ca_issuers_path;
  bssl::UniquePtr<CRYPTO_BUFFER> intermediate;
  scoped_refptr<X509Certificate> root;
  CreateSimpleChainWithAIA(&leaf, &ca_issuers_path, &intermediate, &root);

  // Serve a 404 for the AIA url.
  RegisterSimpleTestServerHandler(ca_issuers_path, HTTP_NOT_FOUND, "text/plain",
                                  "Not Found");

  // Trust the root certificate.
  ScopedTestRoot scoped_root(root.get());

  // The chain being verified is solely the leaf certificate (missing the
  // intermediate and root).
  ASSERT_EQ(0u, leaf->intermediate_buffers().size());

  const int flags = 0;
  int error;
  CertVerifyResult verify_result;

  // Verifying the chain should fail as the intermediate is missing, and
  // cannot be fetched via AIA.
  error = Verify(leaf.get(), kHostname, flags, CRLSet::BuiltinCRLSet().get(),
                 CertificateList(), &verify_result);
  EXPECT_NE(OK, error);

  if (verify_proc_type() == CERT_VERIFY_PROC_WIN) {
    // CertVerifyProcWin has a flaky result of ERR_CERT_AUTHORITY_INVALID or
    // ERR_CERT_INVALID (https://crbug.com/859387) - accept either.
    EXPECT_TRUE(error == ERR_CERT_AUTHORITY_INVALID || ERR_CERT_INVALID)
        << "Unexpected error: " << error;
  } else {
    EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
  }
}
#undef MAYBE_IntermediateFromAia404

// Tries verifying a certificate chain that is missing an intermediate. The
// intermediate is available via AIA.
// TODO(crbug.com/860189): Failing on iOS
#if defined(OS_IOS)
#define MAYBE_IntermediateFromAia200Der DISABLED_IntermediateFromAia200Der
#else
#define MAYBE_IntermediateFromAia200Der IntermediateFromAia200Der
#endif
TEST_P(CertVerifyProcInternalWithNetFetchingTest,
       MAYBE_IntermediateFromAia200Der) {
  const char kHostname[] = "www.example.com";

  // Create a chain where the leaf has an AIA that points to test server.
  scoped_refptr<X509Certificate> leaf;
  std::string ca_issuers_path;
  bssl::UniquePtr<CRYPTO_BUFFER> intermediate;
  scoped_refptr<X509Certificate> root;
  CreateSimpleChainWithAIA(&leaf, &ca_issuers_path, &intermediate, &root);

  // Setup the test server to reply with the correct intermediate.
  RegisterSimpleTestServerHandler(
      ca_issuers_path, HTTP_OK, "application/pkix-cert",
      x509_util::CryptoBufferAsStringPiece(intermediate.get()).as_string());

  // Trust the root certificate.
  ScopedTestRoot scoped_root(root.get());

  // The chain being verified is solely the leaf certificate (missing the
  // intermediate and root).
  ASSERT_EQ(0u, leaf->intermediate_buffers().size());

  const int flags = 0;
  int error;
  CertVerifyResult verify_result;

  // Verifying the chain should succeed as the missing intermediate can be
  // fetched via AIA.
  error = Verify(leaf.get(), kHostname, flags, CRLSet::BuiltinCRLSet().get(),
                 CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
}

// This test is the same as IntermediateFromAia200Der, except the certificate is
// served as PEM rather than DER.
//
// Tries verifying a certificate chain that is missing an intermediate. The
// intermediate is available via AIA, however is served as a PEM file rather
// than DER.
// TODO(crbug.com/860189): Failing on iOS
#if defined(OS_IOS)
#define MAYBE_IntermediateFromAia200Pem DISABLED_IntermediateFromAia200Pem
#else
#define MAYBE_IntermediateFromAia200Pem IntermediateFromAia200Pem
#endif
TEST_P(CertVerifyProcInternalWithNetFetchingTest,
       MAYBE_IntermediateFromAia200Pem) {
  const char kHostname[] = "www.example.com";

  // Create a chain where the leaf has an AIA that points to test server.
  scoped_refptr<X509Certificate> leaf;
  std::string ca_issuers_path;
  bssl::UniquePtr<CRYPTO_BUFFER> intermediate;
  scoped_refptr<X509Certificate> root;
  CreateSimpleChainWithAIA(&leaf, &ca_issuers_path, &intermediate, &root);

  std::string intermediate_pem;
  ASSERT_TRUE(
      X509Certificate::GetPEMEncoded(intermediate.get(), &intermediate_pem));

  // Setup the test server to reply with the correct intermediate.
  RegisterSimpleTestServerHandler(
      ca_issuers_path, HTTP_OK, "application/x-x509-ca-cert", intermediate_pem);

  // Trust the root certificate.
  ScopedTestRoot scoped_root(root.get());

  // The chain being verified is solely the leaf certificate (missing the
  // intermediate and root).
  ASSERT_EQ(0u, leaf->intermediate_buffers().size());

  const int flags = 0;
  int error;
  CertVerifyResult verify_result;

  // Verifying the chain should succeed as the missing intermediate can be
  // fetched via AIA.
  error = Verify(leaf.get(), kHostname, flags, CRLSet::BuiltinCRLSet().get(),
                 CertificateList(), &verify_result);

  if (verify_proc_type() == CERT_VERIFY_PROC_ANDROID) {
    // Android doesn't support PEM - https://crbug.com/725180
    EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
  } else {
    EXPECT_THAT(error, IsOk());
  }
}

// This test is the same as IntermediateFromAia200Pem, but with a different
// formatting on the PEM data.
//
// TODO(crbug.com/860189): Failing on iOS
#if defined(OS_IOS)
#define MAYBE_IntermediateFromAia200Pem2 DISABLED_IntermediateFromAia200Pem2
#else
#define MAYBE_IntermediateFromAia200Pem2 IntermediateFromAia200Pem2
#endif
TEST_P(CertVerifyProcInternalWithNetFetchingTest,
       MAYBE_IntermediateFromAia200Pem2) {
  const char kHostname[] = "www.example.com";

  // Create a chain where the leaf has an AIA that points to test server.
  scoped_refptr<X509Certificate> leaf;
  std::string ca_issuers_path;
  bssl::UniquePtr<CRYPTO_BUFFER> intermediate;
  scoped_refptr<X509Certificate> root;
  CreateSimpleChainWithAIA(&leaf, &ca_issuers_path, &intermediate, &root);

  std::string intermediate_pem;
  ASSERT_TRUE(
      X509Certificate::GetPEMEncoded(intermediate.get(), &intermediate_pem));
  intermediate_pem = "Text at start of file\n" + intermediate_pem;

  // Setup the test server to reply with the correct intermediate.
  RegisterSimpleTestServerHandler(
      ca_issuers_path, HTTP_OK, "application/x-x509-ca-cert", intermediate_pem);

  // Trust the root certificate.
  ScopedTestRoot scoped_root(root.get());

  // The chain being verified is solely the leaf certificate (missing the
  // intermediate and root).
  ASSERT_EQ(0u, leaf->intermediate_buffers().size());

  const int flags = 0;
  int error;
  CertVerifyResult verify_result;

  // Verifying the chain should succeed as the missing intermediate can be
  // fetched via AIA.
  error = Verify(leaf.get(), kHostname, flags, CRLSet::BuiltinCRLSet().get(),
                 CertificateList(), &verify_result);

  if (verify_proc_type() == CERT_VERIFY_PROC_ANDROID) {
    // Android doesn't support PEM - https://crbug.com/725180
    EXPECT_THAT(error, IsError(ERR_CERT_AUTHORITY_INVALID));
  } else {
    EXPECT_THAT(error, IsOk());
  }
}

// Tries verifying a certificate chain that uses a SHA1 intermediate,
// however, chasing the AIA can discover a SHA256 version of the intermediate.
//
// Path building should discover the stronger intermediate and use it.
TEST_P(CertVerifyProcInternalWithNetFetchingTest,
       Sha1IntermediateButAIAHasSha256) {
  const char kHostname[] = "www.example.com";

  base::FilePath certs_dir =
      GetTestNetDataDirectory()
          .AppendASCII("verify_certificate_chain_unittest")
          .AppendASCII("target-and-intermediate");

  CertificateList orig_certs = CreateCertificateListFromFile(
      certs_dir, "chain.pem", X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(3U, orig_certs.size());

  // Build slightly modified variants of |orig_certs|.
  CertBuilder root(orig_certs[2]->cert_buffer(), nullptr);
  CertBuilder intermediate(orig_certs[1]->cert_buffer(), &root);
  CertBuilder leaf(orig_certs[0]->cert_buffer(), &intermediate);

  // Make the leaf certificate have an AIA (CA Issuers) that points to the
  // embedded test server. This uses a random URL for predictable behavior in
  // the presence of global caching.
  std::string ca_issuers_path = MakeRandomPath(".cer");
  GURL ca_issuers_url = GetTestServerAbsoluteUrl(ca_issuers_path);
  leaf.SetCaIssuersUrl(ca_issuers_url);
  leaf.SetSubjectAltName(kHostname);

  // Make two versions of the intermediate - one that is SHA256 signed, and one
  // that is SHA1 signed.
  intermediate.SetSignatureAlgorithmRsaPkca1(DigestAlgorithm::Sha256);
  intermediate.SetRandomSerialNumber();
  auto intermediate_sha256 = intermediate.DupCertBuffer();

  intermediate.SetSignatureAlgorithmRsaPkca1(DigestAlgorithm::Sha1);
  intermediate.SetRandomSerialNumber();
  auto intermediate_sha1 = intermediate.DupCertBuffer();

  // Trust the root certificate.
  auto root_cert = root.GetX509Certificate();
  ScopedTestRoot scoped_root(root_cert.get());

  // Setup the test server to reply with the SHA256 intermediate.
  RegisterSimpleTestServerHandler(
      ca_issuers_path, HTTP_OK, "application/pkix-cert",
      x509_util::CryptoBufferAsStringPiece(intermediate_sha256.get())
          .as_string());

  // Build a chain to verify that includes the SHA1 intermediate.
  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
  intermediates.push_back(bssl::UpRef(intermediate_sha1.get()));
  scoped_refptr<X509Certificate> chain_sha1 = X509Certificate::CreateFromBuffer(
      leaf.DupCertBuffer(), std::move(intermediates));
  ASSERT_TRUE(chain_sha1.get());

  const int flags = 0;
  CertVerifyResult verify_result;
  int error =
      Verify(chain_sha1.get(), kHostname, flags, CRLSet::BuiltinCRLSet().get(),
             CertificateList(), &verify_result);

  if (verify_proc_type() == CERT_VERIFY_PROC_BUILTIN ||
      verify_proc_type() == CERT_VERIFY_PROC_MAC) {
    // Should have built a chain through the SHA256 intermediate. This was only
    // available via AIA, and not the (SHA1) one provided directly to path
    // building.
    ASSERT_EQ(2u, verify_result.verified_cert->intermediate_buffers().size());
    EXPECT_TRUE(x509_util::CryptoBufferEqual(
        verify_result.verified_cert->intermediate_buffers()[0].get(),
        intermediate_sha256.get()));
    ASSERT_EQ(2u, verify_result.verified_cert->intermediate_buffers().size());

    EXPECT_FALSE(verify_result.has_sha1);
    EXPECT_THAT(error, IsOk());
  } else if (verify_proc_type() == CERT_VERIFY_PROC_WIN) {
    // TODO(eroman): Make these test expectations exact.
    // This seemed to be working on Windows when !AreSHA1IntermediatesAllowed()
    // from previous testing, but then failed on the Windows 10 bot.
    if (error != OK) {
      EXPECT_TRUE(verify_result.has_sha1);
      EXPECT_THAT(error, IsError(ERR_CERT_WEAK_SIGNATURE_ALGORITHM));
    }
  } else {
    EXPECT_TRUE(verify_result.has_sha1);
    EXPECT_THAT(error, IsError(ERR_CERT_WEAK_SIGNATURE_ALGORITHM));
  }
}

TEST(CertVerifyProcTest, RejectsMD2) {
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(cert);

  CertVerifyResult result;
  result.has_md2 = true;
  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_INVALID));
  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_INVALID);
}

TEST(CertVerifyProcTest, RejectsMD4) {
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(cert);

  CertVerifyResult result;
  result.has_md4 = true;
  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_INVALID));
  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_INVALID);
}

TEST(CertVerifyProcTest, RejectsMD5) {
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(cert);

  CertVerifyResult result;
  result.has_md5 = true;
  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_WEAK_SIGNATURE_ALGORITHM));
  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_WEAK_SIGNATURE_ALGORITHM);
}

TEST(CertVerifyProcTest, RejectsPublicSHA1Leaves) {
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(cert);

  CertVerifyResult result;
  result.has_sha1 = true;
  result.has_sha1_leaf = true;
  result.is_issued_by_known_root = true;
  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_WEAK_SIGNATURE_ALGORITHM));
  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_WEAK_SIGNATURE_ALGORITHM);
}

TEST(CertVerifyProcTest, RejectsPublicSHA1IntermediatesUnlessAllowed) {
  scoped_refptr<X509Certificate> cert(ImportCertFromFile(
      GetTestCertsDirectory(), "39_months_after_2015_04.pem"));
  ASSERT_TRUE(cert);

  CertVerifyResult result;
  result.has_sha1 = true;
  result.has_sha1_leaf = false;
  result.is_issued_by_known_root = true;
  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  if (AreSHA1IntermediatesAllowed()) {
    EXPECT_THAT(error, IsOk());
    EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_SHA1_SIGNATURE_PRESENT);
  } else {
    EXPECT_THAT(error, IsError(ERR_CERT_WEAK_SIGNATURE_ALGORITHM));
    EXPECT_TRUE(verify_result.cert_status &
                CERT_STATUS_WEAK_SIGNATURE_ALGORITHM);
  }
}

TEST(CertVerifyProcTest, RejectsPrivateSHA1UnlessFlag) {
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(cert);

  CertVerifyResult result;
  result.has_sha1 = true;
  result.has_sha1_leaf = true;
  result.is_issued_by_known_root = false;
  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  // SHA-1 should be rejected by default for private roots...
  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_THAT(error, IsError(ERR_CERT_WEAK_SIGNATURE_ALGORITHM));
  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_SHA1_SIGNATURE_PRESENT);

  // ... unless VERIFY_ENABLE_SHA1_LOCAL_ANCHORS was supplied.
  flags = CertVerifyProc::VERIFY_ENABLE_SHA1_LOCAL_ANCHORS;
  verify_result.Reset();
  error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_THAT(error, IsOk());
  EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_SHA1_SIGNATURE_PRESENT);
}

enum ExpectedAlgorithms {
  EXPECT_MD2 = 1 << 0,
  EXPECT_MD4 = 1 << 1,
  EXPECT_MD5 = 1 << 2,
  EXPECT_SHA1 = 1 << 3,
  EXPECT_SHA1_LEAF = 1 << 4,
};

struct WeakDigestTestData {
  const char* root_cert_filename;
  const char* intermediate_cert_filename;
  const char* ee_cert_filename;
  int expected_algorithms;
};

const char* StringOrDefault(const char* str, const char* default_value) {
  if (!str)
    return default_value;
  return str;
}

// GTest 'magic' pretty-printer, so that if/when a test fails, it knows how
// to output the parameter that was passed. Without this, it will simply
// attempt to print out the first twenty bytes of the object, which depending
// on platform and alignment, may result in an invalid read.
void PrintTo(const WeakDigestTestData& data, std::ostream* os) {
  *os << "root: " << StringOrDefault(data.root_cert_filename, "none")
      << "; intermediate: "
      << StringOrDefault(data.intermediate_cert_filename, "none")
      << "; end-entity: " << data.ee_cert_filename;
}

class CertVerifyProcWeakDigestTest
    : public testing::TestWithParam<WeakDigestTestData> {
 public:
  CertVerifyProcWeakDigestTest() = default;
  virtual ~CertVerifyProcWeakDigestTest() = default;
};

// Tests that the CertVerifyProc::Verify() properly surfaces the (weak) hash
// algorithms used in the chain.
TEST_P(CertVerifyProcWeakDigestTest, VerifyDetectsAlgorithm) {
  WeakDigestTestData data = GetParam();
  base::FilePath certs_dir = GetTestCertsDirectory();

  // Build |intermediates| as the full chain (including trust anchor).
  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;

  if (data.intermediate_cert_filename) {
    scoped_refptr<X509Certificate> intermediate_cert =
        ImportCertFromFile(certs_dir, data.intermediate_cert_filename);
    ASSERT_TRUE(intermediate_cert);
    intermediates.push_back(bssl::UpRef(intermediate_cert->cert_buffer()));
  }

  if (data.root_cert_filename) {
    scoped_refptr<X509Certificate> root_cert =
        ImportCertFromFile(certs_dir, data.root_cert_filename);
    ASSERT_TRUE(root_cert);
    intermediates.push_back(bssl::UpRef(root_cert->cert_buffer()));
  }

  scoped_refptr<X509Certificate> ee_cert =
      ImportCertFromFile(certs_dir, data.ee_cert_filename);
  ASSERT_TRUE(ee_cert);

  scoped_refptr<X509Certificate> ee_chain = X509Certificate::CreateFromBuffer(
      bssl::UpRef(ee_cert->cert_buffer()), std::move(intermediates));
  ASSERT_TRUE(ee_chain);

  int flags = 0;
  CertVerifyResult verify_result;

  // Use a mock CertVerifyProc that returns success with a verified_cert of
  // |ee_chain|.
  //
  // This is sufficient for the purposes of this test, as the checking for weak
  // hash algorithms is done by CertVerifyProc::Verify().
  scoped_refptr<CertVerifyProc> proc =
      new MockCertVerifyProc(CertVerifyResult());
  proc->Verify(ee_chain.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
               /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
               CertificateList(), &verify_result);
  EXPECT_EQ(!!(data.expected_algorithms & EXPECT_MD2), verify_result.has_md2);
  EXPECT_EQ(!!(data.expected_algorithms & EXPECT_MD4), verify_result.has_md4);
  EXPECT_EQ(!!(data.expected_algorithms & EXPECT_MD5), verify_result.has_md5);
  EXPECT_EQ(!!(data.expected_algorithms & EXPECT_SHA1), verify_result.has_sha1);
  EXPECT_EQ(!!(data.expected_algorithms & EXPECT_SHA1_LEAF),
            verify_result.has_sha1_leaf);
}

// The signature algorithm of the root CA should not matter.
const WeakDigestTestData kVerifyRootCATestData[] = {
    {"weak_digest_md5_root.pem", "weak_digest_sha1_intermediate.pem",
     "weak_digest_sha1_ee.pem", EXPECT_SHA1 | EXPECT_SHA1_LEAF},
    {"weak_digest_md4_root.pem", "weak_digest_sha1_intermediate.pem",
     "weak_digest_sha1_ee.pem", EXPECT_SHA1 | EXPECT_SHA1_LEAF},
    {"weak_digest_md2_root.pem", "weak_digest_sha1_intermediate.pem",
     "weak_digest_sha1_ee.pem", EXPECT_SHA1 | EXPECT_SHA1_LEAF},
};
INSTANTIATE_TEST_SUITE_P(VerifyRoot,
                         CertVerifyProcWeakDigestTest,
                         testing::ValuesIn(kVerifyRootCATestData));

// The signature algorithm of intermediates should be properly detected.
const WeakDigestTestData kVerifyIntermediateCATestData[] = {
    {"weak_digest_sha1_root.pem", "weak_digest_md5_intermediate.pem",
     "weak_digest_sha1_ee.pem", EXPECT_MD5 | EXPECT_SHA1 | EXPECT_SHA1_LEAF},
    {"weak_digest_sha1_root.pem", "weak_digest_md4_intermediate.pem",
     "weak_digest_sha1_ee.pem", EXPECT_MD4 | EXPECT_SHA1 | EXPECT_SHA1_LEAF},
    {"weak_digest_sha1_root.pem", "weak_digest_md2_intermediate.pem",
     "weak_digest_sha1_ee.pem", EXPECT_MD2 | EXPECT_SHA1 | EXPECT_SHA1_LEAF},
};

INSTANTIATE_TEST_SUITE_P(VerifyIntermediate,
                         CertVerifyProcWeakDigestTest,
                         testing::ValuesIn(kVerifyIntermediateCATestData));

// The signature algorithm of end-entity should be properly detected.
const WeakDigestTestData kVerifyEndEntityTestData[] = {
    {"weak_digest_sha1_root.pem", "weak_digest_sha1_intermediate.pem",
     "weak_digest_md5_ee.pem", EXPECT_MD5 | EXPECT_SHA1},
    {"weak_digest_sha1_root.pem", "weak_digest_sha1_intermediate.pem",
     "weak_digest_md4_ee.pem", EXPECT_MD4 | EXPECT_SHA1},
    {"weak_digest_sha1_root.pem", "weak_digest_sha1_intermediate.pem",
     "weak_digest_md2_ee.pem", EXPECT_MD2 | EXPECT_SHA1},
};

INSTANTIATE_TEST_SUITE_P(VerifyEndEntity,
                         CertVerifyProcWeakDigestTest,
                         testing::ValuesIn(kVerifyEndEntityTestData));

// Incomplete chains do not report the status of the intermediate.
// Note: really each of these tests should also expect the digest algorithm of
// the intermediate (included as a comment). However CertVerifyProc::Verify() is
// unable to distinguish that this is an intermediate and not a trust anchor, so
// this intermediate is treated like a trust anchor.
const WeakDigestTestData kVerifyIncompleteIntermediateTestData[] = {
    {nullptr, "weak_digest_md5_intermediate.pem", "weak_digest_sha1_ee.pem",
     /*EXPECT_MD5 |*/ EXPECT_SHA1 | EXPECT_SHA1_LEAF},
    {nullptr, "weak_digest_md4_intermediate.pem", "weak_digest_sha1_ee.pem",
     /*EXPECT_MD4 |*/ EXPECT_SHA1 | EXPECT_SHA1_LEAF},
    {nullptr, "weak_digest_md2_intermediate.pem", "weak_digest_sha1_ee.pem",
     /*EXPECT_MD2 |*/ EXPECT_SHA1 | EXPECT_SHA1_LEAF},
};

INSTANTIATE_TEST_SUITE_P(
    MAYBE_VerifyIncompleteIntermediate,
    CertVerifyProcWeakDigestTest,
    testing::ValuesIn(kVerifyIncompleteIntermediateTestData));

// Incomplete chains should report the status of the end-entity.
// Note: really each of these tests should also expect EXPECT_SHA1 (included as
// a comment). However CertVerifyProc::Verify() is unable to distinguish that
// this is an intermediate and not a trust anchor, so this intermediate is
// treated like a trust anchor.
const WeakDigestTestData kVerifyIncompleteEETestData[] = {
    {nullptr, "weak_digest_sha1_intermediate.pem", "weak_digest_md5_ee.pem",
     /*EXPECT_SHA1 |*/ EXPECT_MD5},
    {nullptr, "weak_digest_sha1_intermediate.pem", "weak_digest_md4_ee.pem",
     /*EXPECT_SHA1 |*/ EXPECT_MD4},
    {nullptr, "weak_digest_sha1_intermediate.pem", "weak_digest_md2_ee.pem",
     /*EXPECT_SHA1 |*/ EXPECT_MD2},
};

INSTANTIATE_TEST_SUITE_P(VerifyIncompleteEndEntity,
                         CertVerifyProcWeakDigestTest,
                         testing::ValuesIn(kVerifyIncompleteEETestData));

// Differing algorithms between the intermediate and the EE should still be
// reported.
const WeakDigestTestData kVerifyMixedTestData[] = {
    {"weak_digest_sha1_root.pem", "weak_digest_md5_intermediate.pem",
     "weak_digest_md2_ee.pem", EXPECT_MD2 | EXPECT_MD5},
    {"weak_digest_sha1_root.pem", "weak_digest_md2_intermediate.pem",
     "weak_digest_md5_ee.pem", EXPECT_MD2 | EXPECT_MD5},
    {"weak_digest_sha1_root.pem", "weak_digest_md4_intermediate.pem",
     "weak_digest_md2_ee.pem", EXPECT_MD2 | EXPECT_MD4},
};

INSTANTIATE_TEST_SUITE_P(VerifyMixed,
                         CertVerifyProcWeakDigestTest,
                         testing::ValuesIn(kVerifyMixedTestData));

// The EE is a trusted certificate. Even though it uses weak hashes, these
// should not be reported.
const WeakDigestTestData kVerifyTrustedEETestData[] = {
    {nullptr, nullptr, "weak_digest_md5_ee.pem", 0},
    {nullptr, nullptr, "weak_digest_md4_ee.pem", 0},
    {nullptr, nullptr, "weak_digest_md2_ee.pem", 0},
    {nullptr, nullptr, "weak_digest_sha1_ee.pem", 0},
};

INSTANTIATE_TEST_SUITE_P(VerifyTrustedEE,
                         CertVerifyProcWeakDigestTest,
                         testing::ValuesIn(kVerifyTrustedEETestData));

// Test fixture for verifying certificate names.
class CertVerifyProcNameTest : public ::testing::Test {
 protected:
  void VerifyCertName(const char* hostname, bool valid) {
    scoped_refptr<X509Certificate> cert(ImportCertFromFile(
        GetTestCertsDirectory(), "subjectAltName_sanity_check.pem"));
    ASSERT_TRUE(cert);
    CertVerifyResult result;
    result.is_issued_by_known_root = false;
    scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

    CertVerifyResult verify_result;
    int error = verify_proc->Verify(
        cert.get(), hostname, /*ocsp_response=*/std::string(),
        /*sct_list=*/std::string(), 0, CRLSet::BuiltinCRLSet().get(),
        CertificateList(), &verify_result);
    if (valid) {
      EXPECT_THAT(error, IsOk());
      EXPECT_FALSE(verify_result.cert_status & CERT_STATUS_COMMON_NAME_INVALID);
    } else {
      EXPECT_THAT(error, IsError(ERR_CERT_COMMON_NAME_INVALID));
      EXPECT_TRUE(verify_result.cert_status & CERT_STATUS_COMMON_NAME_INVALID);
    }
  }
};

// Don't match the common name
TEST_F(CertVerifyProcNameTest, DontMatchCommonName) {
  VerifyCertName("127.0.0.1", false);
}

// Matches the iPAddress SAN (IPv4)
TEST_F(CertVerifyProcNameTest, MatchesIpSanIpv4) {
  VerifyCertName("127.0.0.2", true);
}

// Matches the iPAddress SAN (IPv6)
TEST_F(CertVerifyProcNameTest, MatchesIpSanIpv6) {
  VerifyCertName("FE80:0:0:0:0:0:0:1", true);
}

// Should not match the iPAddress SAN
TEST_F(CertVerifyProcNameTest, DoesntMatchIpSanIpv6) {
  VerifyCertName("[FE80:0:0:0:0:0:0:1]", false);
}

// Compressed form matches the iPAddress SAN (IPv6)
TEST_F(CertVerifyProcNameTest, MatchesIpSanCompressedIpv6) {
  VerifyCertName("FE80::1", true);
}

// IPv6 mapped form should NOT match iPAddress SAN
TEST_F(CertVerifyProcNameTest, DoesntMatchIpSanIPv6Mapped) {
  VerifyCertName("::127.0.0.2", false);
}

// Matches the dNSName SAN
TEST_F(CertVerifyProcNameTest, MatchesDnsSan) {
  VerifyCertName("test.example", true);
}

// Matches the dNSName SAN (trailing . ignored)
TEST_F(CertVerifyProcNameTest, MatchesDnsSanTrailingDot) {
  VerifyCertName("test.example.", true);
}

// Should not match the dNSName SAN
TEST_F(CertVerifyProcNameTest, DoesntMatchDnsSan) {
  VerifyCertName("www.test.example", false);
}

// Should not match the dNSName SAN
TEST_F(CertVerifyProcNameTest, DoesntMatchDnsSanInvalid) {
  VerifyCertName("test..example", false);
}

// Should not match the dNSName SAN
TEST_F(CertVerifyProcNameTest, DoesntMatchDnsSanTwoTrailingDots) {
  VerifyCertName("test.example..", false);
}

// Should not match the dNSName SAN
TEST_F(CertVerifyProcNameTest, DoesntMatchDnsSanLeadingAndTrailingDot) {
  VerifyCertName(".test.example.", false);
}

// Should not match the dNSName SAN
TEST_F(CertVerifyProcNameTest, DoesntMatchDnsSanTrailingDot) {
  VerifyCertName(".test.example", false);
}

// Tests that CertVerifyProc records a histogram correctly when a
// certificate chaining to a private root contains the TLS feature
// extension and does not have a stapled OCSP response.
TEST(CertVerifyProcTest, HasTLSFeatureExtensionUMA) {
  base::HistogramTester histograms;
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "tls_feature_extension.pem"));
  ASSERT_TRUE(cert);
  CertVerifyResult result;
  result.is_issued_by_known_root = false;
  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  histograms.ExpectTotalCount(kTLSFeatureExtensionHistogram, 0);
  histograms.ExpectTotalCount(kTLSFeatureExtensionOCSPHistogram, 0);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_EQ(OK, error);
  histograms.ExpectTotalCount(kTLSFeatureExtensionHistogram, 1);
  histograms.ExpectBucketCount(kTLSFeatureExtensionHistogram, true, 1);
  histograms.ExpectTotalCount(kTLSFeatureExtensionOCSPHistogram, 1);
  histograms.ExpectBucketCount(kTLSFeatureExtensionOCSPHistogram, false, 1);
}

// Tests that CertVerifyProc records a histogram correctly when a
// certificate chaining to a private root contains the TLS feature
// extension and does have a stapled OCSP response.
TEST(CertVerifyProcTest, HasTLSFeatureExtensionWithStapleUMA) {
  base::HistogramTester histograms;
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "tls_feature_extension.pem"));
  ASSERT_TRUE(cert);
  CertVerifyResult result;
  result.is_issued_by_known_root = false;
  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  histograms.ExpectTotalCount(kTLSFeatureExtensionHistogram, 0);
  histograms.ExpectTotalCount(kTLSFeatureExtensionOCSPHistogram, 0);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", "dummy response", /*sct_list=*/std::string(),
      flags, CRLSet::BuiltinCRLSet().get(), CertificateList(), &verify_result);
  EXPECT_EQ(OK, error);
  histograms.ExpectTotalCount(kTLSFeatureExtensionHistogram, 1);
  histograms.ExpectBucketCount(kTLSFeatureExtensionHistogram, true, 1);
  histograms.ExpectTotalCount(kTLSFeatureExtensionOCSPHistogram, 1);
  histograms.ExpectBucketCount(kTLSFeatureExtensionOCSPHistogram, true, 1);
}

// Tests that CertVerifyProc records a histogram correctly when a
// certificate chaining to a private root does not contain the TLS feature
// extension.
TEST(CertVerifyProcTest, DoesNotHaveTLSFeatureExtensionUMA) {
  base::HistogramTester histograms;
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(cert);
  CertVerifyResult result;
  result.is_issued_by_known_root = false;
  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  histograms.ExpectTotalCount(kTLSFeatureExtensionHistogram, 0);
  histograms.ExpectTotalCount(kTLSFeatureExtensionOCSPHistogram, 0);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_EQ(OK, error);
  histograms.ExpectTotalCount(kTLSFeatureExtensionHistogram, 1);
  histograms.ExpectBucketCount(kTLSFeatureExtensionHistogram, false, 1);
  histograms.ExpectTotalCount(kTLSFeatureExtensionOCSPHistogram, 0);
}

// Tests that CertVerifyProc does not record a histogram when a
// certificate contains the TLS feature extension but chains to a public
// root.
TEST(CertVerifyProcTest, HasTLSFeatureExtensionWithPublicRootUMA) {
  base::HistogramTester histograms;
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "tls_feature_extension.pem"));
  ASSERT_TRUE(cert);
  CertVerifyResult result;
  result.is_issued_by_known_root = true;
  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  histograms.ExpectTotalCount(kTLSFeatureExtensionHistogram, 0);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_EQ(OK, error);
  histograms.ExpectTotalCount(kTLSFeatureExtensionHistogram, 0);
  histograms.ExpectTotalCount(kTLSFeatureExtensionOCSPHistogram, 0);
}

// Test that trust anchors are appropriately recorded via UMA.
TEST(CertVerifyProcTest, HasTrustAnchorVerifyUMA) {
  base::HistogramTester histograms;
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(cert);

  CertVerifyResult result;

  // Simulate a certificate chain issued by "C=US, O=Google Trust Services LLC,
  // CN=GTS Root R4". This publicly-trusted root was chosen as it was included
  // in 2017 and is not anticipated to be removed from all supported platforms
  // for a few decades.
  // Note: The actual cert in |cert| does not matter for this testing, so long
  // as it's not violating any CertVerifyProc::Verify() policies.
  SHA256HashValue leaf_hash = {{0}};
  SHA256HashValue intermediate_hash = {{1}};
  SHA256HashValue root_hash = {
      {0x98, 0x47, 0xe5, 0x65, 0x3e, 0x5e, 0x9e, 0x84, 0x75, 0x16, 0xe5,
       0xcb, 0x81, 0x86, 0x06, 0xaa, 0x75, 0x44, 0xa1, 0x9b, 0xe6, 0x7f,
       0xd7, 0x36, 0x6d, 0x50, 0x69, 0x88, 0xe8, 0xd8, 0x43, 0x47}};
  result.public_key_hashes.push_back(HashValue(leaf_hash));
  result.public_key_hashes.push_back(HashValue(intermediate_hash));
  result.public_key_hashes.push_back(HashValue(root_hash));

  const base::HistogramBase::Sample kGTSRootR4HistogramID = 486;

  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  histograms.ExpectTotalCount(kTrustAnchorVerifyHistogram, 0);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_EQ(OK, error);
  histograms.ExpectUniqueSample(kTrustAnchorVerifyHistogram,
                                kGTSRootR4HistogramID, 1);
}

// Test that certificates with multiple trust anchors present result in
// only a single trust anchor being recorded, and that being the most specific
// trust anchor.
TEST(CertVerifyProcTest, LogsOnlyMostSpecificTrustAnchorUMA) {
  base::HistogramTester histograms;
  scoped_refptr<X509Certificate> cert(
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem"));
  ASSERT_TRUE(cert);

  CertVerifyResult result;

  // Simulate a chain of "C=US, O=Google Trust Services LLC, CN=GTS Root R4"
  // signing "C=US, O=Google Trust Services LLC, CN=GTS Root R3" signing an
  // intermediate and a leaf.
  // Note: The actual cert in |cert| does not matter for this testing, so long
  // as it's not violating any CertVerifyProc::Verify() policies.
  SHA256HashValue leaf_hash = {{0}};
  SHA256HashValue intermediate_hash = {{1}};
  SHA256HashValue gts_root_r3_hash = {
      {0x41, 0x79, 0xed, 0xd9, 0x81, 0xef, 0x74, 0x74, 0x77, 0xb4, 0x96,
       0x26, 0x40, 0x8a, 0xf4, 0x3d, 0xaa, 0x2c, 0xa7, 0xab, 0x7f, 0x9e,
       0x08, 0x2c, 0x10, 0x60, 0xf8, 0x40, 0x96, 0x77, 0x43, 0x48}};
  SHA256HashValue gts_root_r4_hash = {
      {0x98, 0x47, 0xe5, 0x65, 0x3e, 0x5e, 0x9e, 0x84, 0x75, 0x16, 0xe5,
       0xcb, 0x81, 0x86, 0x06, 0xaa, 0x75, 0x44, 0xa1, 0x9b, 0xe6, 0x7f,
       0xd7, 0x36, 0x6d, 0x50, 0x69, 0x88, 0xe8, 0xd8, 0x43, 0x47}};
  result.public_key_hashes.push_back(HashValue(leaf_hash));
  result.public_key_hashes.push_back(HashValue(intermediate_hash));
  result.public_key_hashes.push_back(HashValue(gts_root_r3_hash));
  result.public_key_hashes.push_back(HashValue(gts_root_r4_hash));

  const base::HistogramBase::Sample kGTSRootR3HistogramID = 485;

  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  histograms.ExpectTotalCount(kTrustAnchorVerifyHistogram, 0);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_EQ(OK, error);

  // Only GTS Root R3 should be recorded.
  histograms.ExpectUniqueSample(kTrustAnchorVerifyHistogram,
                                kGTSRootR3HistogramID, 1);
}

// Test that trust anchors histograms record whether or not
// is_issued_by_known_root was derived from the OS.
TEST(CertVerifyProcTest, HasTrustAnchorVerifyOutOfDateUMA) {
  base::HistogramTester histograms;
  scoped_refptr<X509Certificate> cert(ImportCertFromFile(
      GetTestCertsDirectory(), "39_months_based_on_last_day.pem"));
  ASSERT_TRUE(cert);

  CertVerifyResult result;

  // Simulate a certificate chain that is recognized as trusted (from a known
  // root), but no certificates in the chain are tracked as known trust
  // anchors.
  SHA256HashValue leaf_hash = {{0}};
  SHA256HashValue intermediate_hash = {{1}};
  SHA256HashValue root_hash = {{2}};
  result.public_key_hashes.push_back(HashValue(leaf_hash));
  result.public_key_hashes.push_back(HashValue(intermediate_hash));
  result.public_key_hashes.push_back(HashValue(root_hash));
  result.is_issued_by_known_root = true;

  scoped_refptr<CertVerifyProc> verify_proc = new MockCertVerifyProc(result);

  histograms.ExpectTotalCount(kTrustAnchorVerifyHistogram, 0);
  histograms.ExpectTotalCount(kTrustAnchorVerifyOutOfDateHistogram, 0);

  int flags = 0;
  CertVerifyResult verify_result;
  int error = verify_proc->Verify(
      cert.get(), "127.0.0.1", /*ocsp_response=*/std::string(),
      /*sct_list=*/std::string(), flags, CRLSet::BuiltinCRLSet().get(),
      CertificateList(), &verify_result);
  EXPECT_EQ(OK, error);
  const base::HistogramBase::Sample kUnknownRootHistogramID = 0;
  histograms.ExpectUniqueSample(kTrustAnchorVerifyHistogram,
                                kUnknownRootHistogramID, 1);
  histograms.ExpectUniqueSample(kTrustAnchorVerifyOutOfDateHistogram, true, 1);
}

}  // namespace net
