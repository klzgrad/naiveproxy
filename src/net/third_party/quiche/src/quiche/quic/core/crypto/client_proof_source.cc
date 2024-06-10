// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/client_proof_source.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace quic {

bool DefaultClientProofSource::AddCertAndKey(
    std::vector<std::string> server_hostnames,
    quiche::QuicheReferenceCountedPointer<Chain> chain,
    CertificatePrivateKey private_key) {
  if (!ValidateCertAndKey(chain, private_key)) {
    return false;
  }

  auto cert_and_key =
      std::make_shared<CertAndKey>(std::move(chain), std::move(private_key));
  for (const std::string& domain : server_hostnames) {
    cert_and_keys_[domain] = cert_and_key;
  }
  return true;
}

std::shared_ptr<const ClientProofSource::CertAndKey>
DefaultClientProofSource::GetCertAndKey(absl::string_view hostname) const {
  if (std::shared_ptr<const CertAndKey> result = LookupExact(hostname);
      result || hostname == "*") {
    return result;
  }

  // Either a full or a wildcard domain lookup failed. In the former case,
  // derive the wildcard domain and look it up.
  if (hostname.size() > 1 && !absl::StartsWith(hostname, "*.")) {
    auto dot_pos = hostname.find('.');
    if (dot_pos != std::string::npos) {
      std::string wildcard = absl::StrCat("*", hostname.substr(dot_pos));
      std::shared_ptr<const CertAndKey> result = LookupExact(wildcard);
      if (result != nullptr) {
        return result;
      }
    }
  }

  // Return default cert, if any.
  return LookupExact("*");
}

std::shared_ptr<const ClientProofSource::CertAndKey>
DefaultClientProofSource::LookupExact(absl::string_view map_key) const {
  const auto it = cert_and_keys_.find(map_key);
  QUIC_DVLOG(1) << "LookupExact(" << map_key
                << ") found:" << (it != cert_and_keys_.end());
  if (it != cert_and_keys_.end()) {
    return it->second;
  }
  return nullptr;
}

}  // namespace quic
