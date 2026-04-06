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

DEFINE_QUICHE_COMMAND_LINE_FLAG(bool, chunked, false,
                                "If true, use chunked OHTTP.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::optional<bool>, indeterminate_length,
                                std::nullopt,
                                "If set, overrides whether to use the "
                                "indeterminate length binary HTTP encoding. If "
                                "unset, uses the value of --chunked.");

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
    std::vector<std::string>, header, {},
    "Adds a header field to the encapsulated binary request. Separate the "
    "header name and value with a colon. Can be specified multiple times.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, private_token, "",
    "When set, the client will attach a base64-encoded private token to the "
    "encapsulated request. Accepts any base64 encoding.");

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
  const bool use_chunked_ohttp =
      quiche::GetQuicheCommandLineFlag(FLAGS_chunked);
  const std::optional<bool> indeterminate_length =
      quiche::GetQuicheCommandLineFlag(FLAGS_indeterminate_length);
  const std::string client_cert_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_client_cert_file);
  const std::string client_cert_key_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_client_cert_key_file);
  const std::optional<std::string> expect_gateway_error =
      quiche::GetQuicheCommandLineFlag(FLAGS_expect_gateway_error);
  const std::optional<int16_t> expect_gateway_response_code =
      quiche::GetQuicheCommandLineFlag(FLAGS_expect_gateway_response_code);

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
  std::vector<std::string> headers =
      quiche::GetQuicheCommandLineFlag(FLAGS_header);
  std::string private_token =
      quiche::GetQuicheCommandLineFlag(FLAGS_private_token);

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
  for (size_t i = 2; i < urls.size(); ++i) {
    MasqueOhttpClient::Config::PerRequestConfig per_request_config(urls[i]);
    per_request_config.SetPostData(post_data);
    QUICHE_RETURN_IF_ERROR(per_request_config.AddHeaders(headers));
    if (!private_token.empty()) {
      QUICHE_RETURN_IF_ERROR(per_request_config.AddPrivateToken(private_token));
    }
    per_request_config.SetUseChunkedOhttp(use_chunked_ohttp);
    per_request_config.SetUseIndeterminateLength(indeterminate_length);
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
