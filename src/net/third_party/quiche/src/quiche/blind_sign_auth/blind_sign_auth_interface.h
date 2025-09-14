// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "anonymous_tokens/cpp/privacy_pass/token_encodings.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

using ::anonymous_tokens::GeoHint;

// ProxyLayer indicates which proxy layer that tokens will be used with.
enum class ProxyLayer : int {
  kProxyA,
  kProxyB,
  kTerminalLayer,
};

// BlindSignAuthServiceType indicates which service that tokens will be
// authenticated with.
enum class BlindSignAuthServiceType {
  kChromeIpBlinding,
  kCronetIpBlinding,
  kWebviewIpBlinding,
  kPrivateAratea,
};

// A BlindSignToken is used to authenticate a request to a privacy proxy.
// The token string contains a serialized SpendTokenData proto.
// The token cannot be successfully redeemed after the expiration time.
struct QUICHE_EXPORT BlindSignToken {
  std::string token;
  absl::Time expiration;
  GeoHint geo_hint;
};

using SignedTokenCallback =
    SingleUseCallback<void(absl::StatusOr<absl::Span<BlindSignToken>>)>;

// This callback is used by the caller to return generated
// attestation data and a token challenge to the BlindSignAuth library.
using AttestAndSignCallback = SingleUseCallback<void(
    absl::StatusOr<std::string>, std::optional<std::string>)>;

// AttestationDataCallback returns a serialized
// privacy::ppn::PrepareAttestationData proto, which contains an attestation
// challenge from the issuer server.
// If the request fails, the callback will return an appropriate error based on
// the response's HTTP status code.
// If the request succeeds but the server does not issue a challenge, the
// callback will return an absl::InternalError.
// The second callback is used by the caller to return the
// attestation data to the BlindSignAuth library.
using AttestationDataCallback =
    SingleUseCallback<void(absl::string_view, AttestAndSignCallback)>;

// BlindSignAuth provides signed, unblinded tokens to callers.
class QUICHE_EXPORT BlindSignAuthInterface {
 public:
  virtual ~BlindSignAuthInterface() = default;

  // Returns signed unblinded tokens in a callback. Tokens are single-use.
  virtual void GetTokens(std::optional<std::string> oauth_token, int num_tokens,
                         ProxyLayer proxy_layer,
                         BlindSignAuthServiceType service_type,
                         SignedTokenCallback callback) = 0;

  // Returns signed unblinded tokens and their expiration time in a
  // SignedTokenCallback. Errors will be returned in the SignedTokenCallback
  // only. Tokens are single-use and restricted to the PI use case.
  // GetAttestationTokens callback will run on the same thread as the
  // BlindSignMessageInterface callbacks.
  // Callers can make multiple concurrent requests to GetAttestationTokens.
  // In the AttestationDataCallback, the caller must call the
  // AttestAndSignCallback and provide AttestationData generated using Keystore
  // and the challenge returned in AttestationDataCallback. If a token challenge
  // is provided in the AttestAndSignCallback, it will be used in creating the
  // token. Otherwise a default challenge will be used containing the issuer
  // hostname.
  virtual void GetAttestationTokens(
      int num_tokens, ProxyLayer layer,
      AttestationDataCallback attestation_data_callback,
      SignedTokenCallback token_callback) = 0;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_
