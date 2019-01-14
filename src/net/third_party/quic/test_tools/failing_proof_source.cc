// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/test_tools/failing_proof_source.h"

namespace quic {
namespace test {

void FailingProofSource::GetProof(const QuicSocketAddress& server_address,
                                  const QuicString& hostname,
                                  const QuicString& server_config,
                                  QuicTransportVersion transport_version,
                                  QuicStringPiece chlo_hash,
                                  std::unique_ptr<Callback> callback) {
  callback->Run(false, nullptr, QuicCryptoProof(), nullptr);
}

QuicReferenceCountedPointer<ProofSource::Chain>
FailingProofSource::GetCertChain(const QuicSocketAddress& server_address,
                                 const QuicString& hostname) {
  return QuicReferenceCountedPointer<Chain>();
}

void FailingProofSource::ComputeTlsSignature(
    const QuicSocketAddress& server_address,
    const QuicString& hostname,
    uint16_t signature_algorithm,
    QuicStringPiece in,
    std::unique_ptr<SignatureCallback> callback) {
  callback->Run(false, "");
}

}  // namespace test
}  // namespace quic
