// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_ASN1_UTIL_H_
#define NET_CERT_ASN1_UTIL_H_

#include <vector>

#include "base/strings/string_piece.h"
#include "net/base/net_export.h"

namespace net {

namespace asn1 {

// ExtractSubjectFromDERCert parses the DER encoded certificate in |cert| and
// extracts the bytes of the X.501 Subject. On successful return, |subject_out|
// is set to contain the Subject, pointing into |cert|.
NET_EXPORT_PRIVATE bool ExtractSubjectFromDERCert(
    base::StringPiece cert,
    base::StringPiece* subject_out);

// ExtractSPKIFromDERCert parses the DER encoded certificate in |cert| and
// extracts the bytes of the SubjectPublicKeyInfo. On successful return,
// |spki_out| is set to contain the SPKI, pointing into |cert|.
NET_EXPORT_PRIVATE bool ExtractSPKIFromDERCert(base::StringPiece cert,
                                               base::StringPiece* spki_out);

// ExtractSubjectPublicKeyFromSPKI parses the DER encoded SubjectPublicKeyInfo
// in |spki| and extracts the bytes of the SubjectPublicKey. On successful
// return, |spk_out| is set to contain the public key, pointing into |spki|.
NET_EXPORT_PRIVATE bool ExtractSubjectPublicKeyFromSPKI(
    base::StringPiece spki,
    base::StringPiece* spk_out);

// HasTLSFeatureExtension parses the DER encoded certificate in |cert|
// and extracts the TLS feature extension
// (https://tools.ietf.org/html/rfc7633) if present. Returns true if the
// TLS feature extension was present, and false if the extension was not
// present or if there was a parsing failure.
NET_EXPORT_PRIVATE bool HasTLSFeatureExtension(base::StringPiece cert);

// Extracts the two (SEQUENCE) tag-length-values for the signature
// AlgorithmIdentifiers in a DER encoded certificate. Does not use strict
// parsing or validate the resulting AlgorithmIdentifiers.
//
// On success returns true, and assigns |cert_signature_algorithm_sequence| and
// |tbs_signature_algorithm_sequence| to point into |cert|:
//
// * |cert_signature_algorithm_sequence| points at the TLV for
//   Certificate.signatureAlgorithm.
//
// * |tbs_signature_algorithm_sequence| points at the TLV for
//   TBSCertificate.algorithm.
NET_EXPORT_PRIVATE bool ExtractSignatureAlgorithmsFromDERCert(
    base::StringPiece cert,
    base::StringPiece* cert_signature_algorithm_sequence,
    base::StringPiece* tbs_signature_algorithm_sequence);

} // namespace asn1

} // namespace net

#endif // NET_CERT_ASN1_UTIL_H_
