// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file._

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_CERT_UTILS_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_CERT_UTILS_H_

#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quic/platform/impl/quic_cert_utils_impl.h"

namespace quic {

class QuicCertUtils {
 public:
  static bool ExtractSubjectNameFromDERCert(QuicStringPiece cert,
                                            QuicStringPiece* subject_out) {
    return QuicCertUtilsImpl::ExtractSubjectNameFromDERCert(cert, subject_out);
  }
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_CERT_UTILS_H_
