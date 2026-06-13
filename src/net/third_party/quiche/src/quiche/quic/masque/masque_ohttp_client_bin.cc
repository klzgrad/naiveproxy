// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "quiche/quic/masque/masque_connection_pool.h"
#include "quiche/quic/masque/masque_ohttp_client.h"
#include "quiche/quic/masque/private_tokens.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_file_utils.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_status_utils.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_certificate_verification, false,
    "If true, don't verify the server certificate.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, use_mtls_for_key_fetch, false,
    "If true, use mTLS when fetching the OHTTP/HPKE keys.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(int, address_family, 0,
                                "IP address family to use. Must be 0, 4 or 6. "
                                "Defaults to 0 which means any.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, client_cert_file, "",
                                "Path to the client certificate chain.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, client_cert_key_file, "",
    "Path to the pkcs8 client certificate private key.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, post_data, "",
    "When set, the client will send a POST request with this data.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, post_data_file, "",
    "When set, the client will send a POST request with the contents of this "
    "file.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::optional<std::string>, method, std::nullopt,
    "Sets the method of the encapsulated request. Defaults to GET, or POST if "
    "--post_data or --post_data_file is set.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    int, num_bhttp_chunks, -1,
    "Number of indeterminate-length BHTTP chunks to split post data into. If "
    "not set or if set to -1, it will match the chunked mode (see "
    "--num_ohttp_chunks). If set to "
    "0, the client will use known-length BHTTP.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    int, num_ohttp_chunks, 0,
    "Number of OHTTP chunks to split serialized BHTTP request into. If not set "
    "or if set to 0, the client will use standard non-chunked OHTTP.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, ping_pong_mode, false,
    "If true, enables ping-pong mode for chunked OHTTP requests. Limitations: "
    "num_ohttp_chunks must be > 0 and there can only be one request.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::vector<std::string>, header, {},
    "Adds a header field to the encapsulated binary request. Separate the "
    "header name and value with a colon. Can be specified multiple times.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::vector<std::string>, key_fetch_header, {},
    "Adds a header field to the key fetch request. Separate the header name "
    "and value with a colon. Can be specified multiple times.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::vector<std::string>, outer_header, {},
    "Adds a header field to the outer gateway request. Separate the header "
    "name and value with a colon. Can be specified multiple times.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, private_token, "",
    "When set, the client will attach a base64-encoded private token to the "
    "encapsulated request. Accepts any base64 encoding.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, private_token_private_key_file, "",
    "Path to the PEM-encoded RSA private key for private tokens.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, private_token_public_key_file, "",
    "Path to the PEM-encoded RSA public key for private tokens.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, dns_override, "",
    "Allows replacing DNS resolution results, similar to curl --connect-to. "
    "Format is HOST1:PORT1:HOST2:PORT2 where HOST1:PORT1 will be replaced by "
    "HOST2:PORT2. HOST1 and PORT1 can be empty, which matches any host and "
    "port. PORT2 can be empty to not override ports. Multiple overrides can be "
    "specified separated by semi-colons.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::optional<std::string>,
                                expect_gateway_error, std::nullopt,
                                "If set, the client will expect this text in "
                                "the error message for the gateway response.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::optional<int16_t>, expect_gateway_response_code, std::nullopt,
    "If set, the client will expect this response code from the gateway.");

namespace quic {
namespace {
absl::Status RunMasqueOhttpClient(int argc, char* argv[]) {
  const char* usage =
      "Usage: masque_ohttp_client <key-url> <relay-url> <url>...";
  std::vector<std::string> urls =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);

  const bool disable_certificate_verification =
      quiche::GetQuicheCommandLineFlag(FLAGS_disable_certificate_verification);
  const bool use_mtls_for_key_fetch =
      quiche::GetQuicheCommandLineFlag(FLAGS_use_mtls_for_key_fetch);
  const std::string client_cert_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_client_cert_file);
  const std::string client_cert_key_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_client_cert_key_file);
  const std::optional<std::string> expect_gateway_error =
      quiche::GetQuicheCommandLineFlag(FLAGS_expect_gateway_error);
  const std::optional<int16_t> expect_gateway_response_code =
      quiche::GetQuicheCommandLineFlag(FLAGS_expect_gateway_response_code);
  const bool ping_pong_mode =
      quiche::GetQuicheCommandLineFlag(FLAGS_ping_pong_mode);

  MasqueConnectionPool::DnsConfig dns_config;
  QUICHE_RETURN_IF_ERROR(dns_config.SetAddressFamily(
      quiche::GetQuicheCommandLineFlag(FLAGS_address_family)));
  QUICHE_RETURN_IF_ERROR(dns_config.SetOverrides(
      quiche::GetQuicheCommandLineFlag(FLAGS_dns_override)));
  std::string post_data = quiche::GetQuicheCommandLineFlag(FLAGS_post_data);
  std::string post_data_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_post_data_file);
  if (!post_data_file.empty()) {
    if (!post_data.empty()) {
      return absl::InvalidArgumentError(
          "Only one of --post_data and --post_data_file can be set.");
    }
    std::optional<std::string> post_data_from_file =
        quiche::ReadFileContents(post_data_file);
    if (!post_data_from_file.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to read post data from file \"", post_data_file, "\""));
    }
    post_data = *post_data_from_file;
  }
  std::optional<std::string> method =
      quiche::GetQuicheCommandLineFlag(FLAGS_method);
  const int num_ohttp_chunks =
      quiche::GetQuicheCommandLineFlag(FLAGS_num_ohttp_chunks);
  const int num_bhttp_chunks =
      quiche::GetQuicheCommandLineFlag(FLAGS_num_bhttp_chunks);
  std::vector<std::string> headers =
      quiche::GetQuicheCommandLineFlag(FLAGS_header);
  std::vector<std::string> key_fetch_headers =
      quiche::GetQuicheCommandLineFlag(FLAGS_key_fetch_header);
  std::vector<std::string> outer_headers =
      quiche::GetQuicheCommandLineFlag(FLAGS_outer_header);
  std::string private_token =
      quiche::GetQuicheCommandLineFlag(FLAGS_private_token);
  std::string private_token_private_key_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_private_token_private_key_file);
  std::string private_token_public_key_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_private_token_public_key_file);
  if (!private_token_private_key_file.empty() && !private_token.empty()) {
    return absl::InvalidArgumentError(
        "Cannot use both --private_token and "
        "--private_token_private_key_file.");
  } else if (private_token_private_key_file.empty() !=
             private_token_public_key_file.empty()) {
    return absl::InvalidArgumentError(
        "Both or neither of --private_token_private_key_file and "
        "--private_token_public_key_file must be set.");
  }
  bssl::UniquePtr<RSA> private_token_private_key;
  if (!private_token_private_key_file.empty()) {
    QUICHE_ASSIGN_OR_RETURN(
        private_token_private_key,
        ParseRsaPrivateKeyFile(private_token_private_key_file));
  }
  bssl::UniquePtr<RSA> private_token_public_key;
  if (!private_token_public_key_file.empty()) {
    QUICHE_ASSIGN_OR_RETURN(
        private_token_public_key,
        ParseRsaPublicKeyFile(private_token_public_key_file));
  }

  if (urls.size() < 3) {
    return absl::InvalidArgumentError(usage);
  }
  MasqueOhttpClient::Config config(/*key_fetch_url=*/urls[0],
                                   /*relay_url=*/urls[1]);
  if (use_mtls_for_key_fetch) {
    QUICHE_RETURN_IF_ERROR(config.ConfigureKeyFetchClientCert(
        client_cert_file, client_cert_key_file));
  }
  QUICHE_RETURN_IF_ERROR(
      config.ConfigureOhttpMtls(client_cert_file, client_cert_key_file));
  config.SetDisableCertificateVerification(disable_certificate_verification);
  config.SetDnsConfig(dns_config);
  QUICHE_RETURN_IF_ERROR(config.AddKeyFetchHeaders(key_fetch_headers));
  for (size_t i = 2; i < urls.size(); ++i) {
    MasqueOhttpClient::Config::PerRequestConfig per_request_config(urls[i]);
    per_request_config.SetPostData(post_data);
    if (method.has_value()) {
      per_request_config.SetMethod(*method);
    }
    QUICHE_RETURN_IF_ERROR(per_request_config.AddHeaders(headers));
    QUICHE_RETURN_IF_ERROR(per_request_config.AddOuterHeaders(outer_headers));
    if (!private_token.empty()) {
      QUICHE_RETURN_IF_ERROR(per_request_config.AddPrivateToken(private_token));
    } else if (private_token_private_key != nullptr &&
               private_token_public_key != nullptr) {
      QUICHE_ASSIGN_OR_RETURN(
          std::string generated_private_token,
          CreateTokenLocally(private_token_private_key.get(),
                             private_token_public_key.get()));
      QUICHE_RETURN_IF_ERROR(
          per_request_config.AddPrivateToken(generated_private_token));
    }
    per_request_config.SetNumOhttpChunks(num_ohttp_chunks);
    per_request_config.SetNumBhttpChunks(num_bhttp_chunks);
    per_request_config.SetPingPongMode(ping_pong_mode);
    if (expect_gateway_error.has_value()) {
      per_request_config.SetExpectedGatewayError(*expect_gateway_error);
    }
    if (expect_gateway_response_code.has_value()) {
      per_request_config.SetExpectedGatewayStatusCode(
          *expect_gateway_response_code);
    }
    config.AddPerRequestConfig(per_request_config);
  }
  return MasqueOhttpClient::Run(std::move(config));
}

}  // namespace
}  // namespace quic

int main(int argc, char* argv[]) {
  absl::Status status = quic::RunMasqueOhttpClient(argc, argv);
  if (!status.ok()) {
    QUICHE_LOG(ERROR) << status.message();
    return 1;
  }
  QUICHE_LOG(INFO) << "MasqueOhttpClient finished successfully";
  return 0;
}
