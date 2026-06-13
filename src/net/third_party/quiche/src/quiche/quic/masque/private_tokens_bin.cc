// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "openssl/base.h"
#include "openssl/rsa.h"
#include "quiche/quic/masque/private_tokens.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_status_utils.h"

// This tool exists to help test out private tokens as defined in RFCs 9577 and
// 9578.

// To generate a config based on existing keys in PEM files:
// blaze run //quiche/quic/masque:private_tokens -- --alsologtostderr
//   --private_key_file=/path/to/private_key.pem
//   --public_key_file==/path/to/public_key.pem

// To test a token against a given public key in base64 format:
// blaze run //quiche/quic/masque:private_tokens -- --alsologtostderr
//   --encoded_public_key="$PUBLIC_KEY" --token="$TOKEN"

// To test out whether a token matches an issuer URL:
// blaze run //quiche/quic/masque:private_tokens -- --alsologtostderr
// --token="$TOKEN" --encoded_public_key="$(
//   curl --silent -H "Accept: application/private-token-issuer-directory"
//   "$ISSUER_URL" | jq -r '.["token-keys"] | map(.["token-key"]) | join(",")')"

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, private_key_file, "",
                                "Path to the PEM-encoded RSA private key.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, public_key_file, "",
                                "Path to the PEM-encoded RSA public key.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, encoded_public_key, "",
    "Base64-encoded public key to use for token validation. Multiple entries "
    "may be passed in by separating them with commas.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, token, "", "Token to validate.");

namespace quic {
namespace {

absl::Status RunPrivateTokens(int argc, char* argv[]) {
  const char* usage = "Usage: private_tokens";
  std::vector<std::string> params =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  const std::string private_key_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_private_key_file);
  const std::string public_key_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_public_key_file);
  const std::string encoded_public_key_from_flags =
      quiche::GetQuicheCommandLineFlag(FLAGS_encoded_public_key);
  std::vector<std::string> encoded_public_keys = absl::StrSplit(
      encoded_public_key_from_flags, ',', absl::SkipWhitespace());
  const std::string token_from_flags =
      quiche::GetQuicheCommandLineFlag(FLAGS_token);

  bssl::UniquePtr<RSA> public_key;
  std::string encoded_public_key;
  if (!public_key_file.empty()) {
    QUICHE_ASSIGN_OR_RETURN(public_key, ParseRsaPublicKeyFile(public_key_file));
    QUICHE_ASSIGN_OR_RETURN(encoded_public_key,
                            EncodePrivacyPassPublicKey(public_key.get()));
    if (!encoded_public_keys.empty()) {
      if (std::find(encoded_public_keys.begin(), encoded_public_keys.end(),
                    encoded_public_key) == encoded_public_keys.end()) {
        return absl::InvalidArgumentError(
            "Public key from --public_key_file does not match "
            "--encoded_public_key");
      }
    } else {
      encoded_public_keys.push_back(encoded_public_key);

      std::string issuer_config = absl::StrCat(
          "{\n  \"issuer-request-uri\": "
          "\"https://issuer.example.net/request\",\n",
          "  \"token-keys\": [\n    {\n      \"token-type\": 2,\n",
          "      \"token-key\": \"", encoded_public_key, "\",\n    }\n  ]\n}");

      QUICHE_LOG(INFO) << "The issuer config could look like:\n"
                       << issuer_config;
    }
  }

  if (!token_from_flags.empty()) {
    QUICHE_RETURN_IF_ERROR(
        TokenValidatesFromAtLeastOneKey(encoded_public_keys, token_from_flags));
    QUICHE_LOG(INFO) << "Validated token from --token";
  }
  if (!private_key_file.empty()) {
    QUICHE_ASSIGN_OR_RETURN(bssl::UniquePtr<RSA> private_key,
                            ParseRsaPrivateKeyFile(private_key_file));
    if (public_key == nullptr) {
      return absl::InvalidArgumentError(
          "--public_key_file is required when --private_key_file is set.");
    }
    QUICHE_ASSIGN_OR_RETURN(
        std::string generated_token,
        CreateTokenLocally(private_key.get(), public_key.get()));

    std::string auth_header = absl::StrCat(
        "Authorization: PrivateToken token=\"", generated_token, "\"");

    QUICHE_LOG(INFO) << "The generated auth header would look like:\n"
                     << auth_header;

    QUICHE_RETURN_IF_ERROR(
        TokenValidatesFromAtLeastOneKey(encoded_public_keys, generated_token));
    QUICHE_LOG(INFO) << "Validated locally-generated token";
  }
  return absl::OkStatus();
}

}  // namespace
}  // namespace quic

int main(int argc, char* argv[]) {
  absl::Status status = quic::RunPrivateTokens(argc, argv);
  if (!status.ok()) {
    QUICHE_LOG(ERROR) << status.message();
    return 1;
  }
  return 0;
}
