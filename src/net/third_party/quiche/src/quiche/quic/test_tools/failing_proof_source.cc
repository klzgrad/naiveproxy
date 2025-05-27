// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/failing_proof_source.h"

#include <memory>
#include <string>

#include "absl/strings/string_view.h"

namespace quic {
namespace test {

void FailingProofSource::GetProof(const QuicSocketAddress& /*server_address*/,
                                  const QuicSocketAddress& /*client_address*/,
                                  const std::string& /*hostname*/,
                                  const std::string& /*server_config*/,
                                  QuicTransportVersion /*transport_version*/,
                                  absl::string_view /*chlo_hash*/,
                                  std::unique_ptr<Callback> callback) {
  callback->Run(false, nullptr, QuicCryptoProof(), nullptr);
}

quiche::QuicheReferenceCountedPointer<ProofSource::Chain>
FailingProofSource::GetCertChain(const QuicSocketAddress& /*server_address*/,
                                 const QuicSocketAddress& /*client_address*/,
                                 const std::string& /*hostname*/,
                                 bool* cert_matched_sni) {
  *cert_matched_sni = false;
  return quiche::QuicheReferenceCountedPointer<Chain>();
}

void FailingProofSource::ComputeTlsSignature(
    const QuicSocketAddress& /*server_address*/,
    const QuicSocketAddress& /*client_address*/,
    const std::string& /*hostname*/, uint16_t /*signature_algorithm*/,
    absl::string_view /*in*/, std::unique_ptr<SignatureCallback> callback) {
  callback->Run(false, "", nullptr);
}

}  // namespace test
}  // namespace quic
