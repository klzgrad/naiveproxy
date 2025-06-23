// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/masque/masque_connection_pool.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/binary_http/binary_http_message.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"
#include "quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "quiche/oblivious_http/buffers/oblivious_http_response.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"
#include "quiche/oblivious_http/oblivious_http_client.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_certificate_verification, false,
    "If true, don't verify the server certificate.");

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

using quiche::BinaryHttpRequest;
using quiche::BinaryHttpResponse;
using quiche::ObliviousHttpClient;
using quiche::ObliviousHttpHeaderKeyConfig;
using quiche::ObliviousHttpKeyConfigs;
using quiche::ObliviousHttpRequest;
using quiche::ObliviousHttpResponse;

namespace quic {
namespace {

class MasqueOhttpClient : public MasqueConnectionPool::Visitor {
 public:
  using RequestId = MasqueConnectionPool::RequestId;
  using Message = MasqueConnectionPool::Message;
  explicit MasqueOhttpClient(QuicEventLoop *event_loop, SSL_CTX *ssl_ctx,
                             std::vector<std::string> urls,
                             bool disable_certificate_verification,
                             int address_family_for_lookup,
                             const std::string &post_data)
      : urls_(urls),
        post_data_(post_data),
        connection_pool_(event_loop, ssl_ctx, disable_certificate_verification,
                         address_family_for_lookup, this) {}

  bool Start() {
    if (urls_.empty()) {
      QUICHE_LOG(ERROR) << "No URLs to request";
      Abort();
      return false;
    }
    if (!StartKeyFetch(urls_[0])) {
      Abort();
      return false;
    }
    return true;
  }
  bool IsDone() {
    if (aborted_) {
      return true;
    }
    if (!ohttp_client_.has_value()) {
      // Key fetch request is still pending.
      return false;
    }
    return pending_ohttp_requests_.empty();
  }

  // From MasqueConnectionPool::Visitor.
  void OnResponse(MasqueConnectionPool * /*pool*/, RequestId request_id,
                  const absl::StatusOr<Message> &response) override {
    if (key_fetch_request_id_.has_value() &&
        *key_fetch_request_id_ == request_id) {
      key_fetch_request_id_ = std::nullopt;
      HandleKeyResponse(response);
    } else {
      auto it = pending_ohttp_requests_.find(request_id);
      if (it == pending_ohttp_requests_.end()) {
        QUICHE_LOG(ERROR) << "Received unexpected response for unknown request "
                          << request_id;
        Abort();
        return;
      }
      if (response.ok()) {
        if (!ohttp_client_.has_value()) {
          QUICHE_LOG(FATAL) << "Received OHTTP response without OHTTP client";
          return;
        }
        absl::StatusOr<ObliviousHttpResponse> ohttp_response =
            ohttp_client_->DecryptObliviousHttpResponse(response->body,
                                                        it->second);
        if (ohttp_response.ok()) {
          QUICHE_LOG(INFO) << "Received OHTTP response for " << request_id;
          absl::StatusOr<BinaryHttpResponse> binary_response =
              BinaryHttpResponse::Create(ohttp_response->GetPlaintextData());
          if (binary_response.ok()) {
            QUICHE_LOG(INFO) << "Successfully decoded OHTTP response:";
            for (const quiche::BinaryHttpMessage::Field &field :
                 binary_response->GetHeaderFields()) {
              QUICHE_LOG(INFO) << field.name << ": " << field.value;
            }
            QUICHE_LOG(INFO) << "Body:" << std::endl << binary_response->body();
          } else {
            QUICHE_LOG(ERROR) << "Failed to parse binary response: "
                              << binary_response.status();
          }
        } else {
          QUICHE_LOG(ERROR) << "Failed to decrypt OHTTP response: "
                            << ohttp_response.status();
        }
      } else {
        QUICHE_LOG(ERROR) << "OHTTP request " << request_id
                          << " failed: " << response.status();
      }
      pending_ohttp_requests_.erase(it);
    }
  }

 private:
  bool StartKeyFetch(const std::string &url_string) {
    QuicUrl url(url_string, "https");
    if (url.host().empty() && !absl::StrContains(url_string, "://")) {
      url = QuicUrl(absl::StrCat("https://", url_string));
    }
    if (url.host().empty()) {
      QUICHE_LOG(ERROR) << "Failed to parse key URL \"" << url_string << "\"";
      return false;
    }
    Message request;
    request.headers[":method"] = "GET";
    request.headers[":scheme"] = url.scheme();
    request.headers[":authority"] = url.HostPort();
    request.headers[":path"] = url.path();
    request.headers["host"] = url.HostPort();
    request.headers["accept"] = "application/ohttp-keys";
    request.headers["content-type"] = "application/ohttp-keys";
    absl::StatusOr<RequestId> request_id =
        connection_pool_.SendRequest(request);
    if (!request_id.ok()) {
      QUICHE_LOG(ERROR) << "Failed to send request: " << request_id.status();
      return false;
    }
    key_fetch_request_id_ = *request_id;
    return true;
  }

  void HandleKeyResponse(const absl::StatusOr<Message> &response) {
    if (!response.ok()) {
      QUICHE_LOG(ERROR) << "Failed to fetch key: " << response.status();
      return;
    }
    QUICHE_LOG(INFO) << "Received key response: "
                     << response->headers.DebugString();
    absl::StatusOr<ObliviousHttpKeyConfigs> key_configs =
        ObliviousHttpKeyConfigs::ParseConcatenatedKeys(response->body);
    if (!key_configs.ok()) {
      QUICHE_LOG(ERROR) << "Failed to parse OHTTP keys: "
                        << key_configs.status();
      Abort();
      return;
    }
    QUICHE_LOG(INFO) << "Successfully got " << key_configs->NumKeys()
                     << " OHTTP keys: " << std::endl
                     << key_configs->DebugString();
    if (urls_.size() <= 2) {
      QUICHE_LOG(INFO) << "No OHTTP URLs to request, exiting.";
      Abort();
      return;
    }
    relay_url_ = QuicUrl(urls_[1], "https");
    if (relay_url_.host().empty() && !absl::StrContains(urls_[1], "://")) {
      relay_url_ = QuicUrl(absl::StrCat("https://", urls_[1]));
    }
    QUICHE_LOG(INFO) << "Using relay URL: " << relay_url_.ToString();
    ObliviousHttpHeaderKeyConfig key_config = key_configs->PreferredConfig();
    absl::StatusOr<absl::string_view> public_key =
        key_configs->GetPublicKeyForId(key_config.GetKeyId());
    if (!public_key.ok()) {
      QUICHE_LOG(ERROR) << "Failed to get public key for key ID "
                        << static_cast<int>(key_config.GetKeyId()) << ": "
                        << public_key.status();
      Abort();
      return;
    }
    absl::StatusOr<ObliviousHttpClient> ohttp_client =
        ObliviousHttpClient::Create(*public_key, key_config);
    if (!ohttp_client.ok()) {
      QUICHE_LOG(ERROR) << "Failed to create OHTTP client: "
                        << ohttp_client.status();
      Abort();
      return;
    }
    ohttp_client_.emplace(std::move(*ohttp_client));
    for (size_t i = 2; i < urls_.size(); ++i) {
      SendOhttpRequestForUrl(urls_[i]);
    }
  }

  void SendOhttpRequestForUrl(const std::string &url_string) {
    QuicUrl url(url_string, "https");
    if (url.host().empty() && !absl::StrContains(url_string, "://")) {
      url = QuicUrl(absl::StrCat("https://", url_string));
    }
    if (url.host().empty()) {
      QUICHE_LOG(ERROR) << "Failed to parse key URL \"" << url_string << "\"";
      return;
    }
    BinaryHttpRequest::ControlData control_data;
    control_data.method = post_data_.empty() ? "GET" : "POST";
    control_data.scheme = url.scheme();
    control_data.authority = url.HostPort();
    control_data.path = url.path();
    BinaryHttpRequest binary_request(control_data);
    binary_request.set_body(post_data_);
    absl::StatusOr<std::string> encoded_request = binary_request.Serialize();
    if (!encoded_request.ok()) {
      QUICHE_LOG(ERROR) << "Failed to encode request: "
                        << encoded_request.status();
      return;
    }
    if (!ohttp_client_.has_value()) {
      QUICHE_LOG(FATAL) << "Cannot send OHTTP request without OHTTP client";
      return;
    }
    absl::StatusOr<ObliviousHttpRequest> ohttp_request =
        ohttp_client_->CreateObliviousHttpRequest(*encoded_request);
    if (!ohttp_request.ok()) {
      QUICHE_LOG(ERROR) << "Failed to create OHTTP request: "
                        << ohttp_request.status();
      return;
    }
    Message request;
    request.headers[":method"] = "POST";
    request.headers[":scheme"] = relay_url_.scheme();
    request.headers[":authority"] = relay_url_.HostPort();
    request.headers[":path"] = relay_url_.path();
    request.headers["host"] = relay_url_.HostPort();
    request.headers["content-type"] = "message/ohttp-req";
    request.body = ohttp_request->EncapsulateAndSerialize();
    absl::StatusOr<RequestId> request_id =
        connection_pool_.SendRequest(request);
    if (!request_id.ok()) {
      QUICHE_LOG(ERROR) << "Failed to send request: " << request_id.status();
      return;
    }
    QUICHE_LOG(INFO) << "Sent OHTTP request for " << url_string;
    auto context = std::move(*ohttp_request).ReleaseContext();
    pending_ohttp_requests_.insert({*request_id, std::move(context)});
  }

  void Abort() {
    QUICHE_LOG(INFO) << "Aborting";
    aborted_ = true;
  }

  std::vector<std::string> urls_;
  std::string post_data_;
  MasqueConnectionPool connection_pool_;
  std::optional<RequestId> key_fetch_request_id_;
  bool aborted_ = false;
  std::optional<ObliviousHttpClient> ohttp_client_;
  QuicUrl relay_url_;
  absl::flat_hash_map<RequestId, ObliviousHttpRequest::Context>
      pending_ohttp_requests_;
};

int RunMasqueOhttpClient(int argc, char *argv[]) {
  const char *usage =
      "Usage: masque_ohttp_client <key-url> <relay-url> <url>...";
  std::vector<std::string> urls =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);

  quiche::QuicheSystemEventLoop system_event_loop("masque_ohttp_client");
  const bool disable_certificate_verification =
      quiche::GetQuicheCommandLineFlag(FLAGS_disable_certificate_verification);

  absl::StatusOr<bssl::UniquePtr<SSL_CTX>> ssl_ctx =
      MasqueConnectionPool::CreateSslCtx(
          quiche::GetQuicheCommandLineFlag(FLAGS_client_cert_file),
          quiche::GetQuicheCommandLineFlag(FLAGS_client_cert_key_file));
  if (!ssl_ctx.ok()) {
    QUICHE_LOG(ERROR) << "Failed to create SSL context: " << ssl_ctx.status();
    return 1;
  }

  const int address_family =
      quiche::GetQuicheCommandLineFlag(FLAGS_address_family);
  int address_family_for_lookup;
  if (address_family == 0) {
    address_family_for_lookup = AF_UNSPEC;
  } else if (address_family == 4) {
    address_family_for_lookup = AF_INET;
  } else if (address_family == 6) {
    address_family_for_lookup = AF_INET6;
  } else {
    QUICHE_LOG(ERROR) << "Invalid address_family " << address_family;
    return 1;
  }
  std::unique_ptr<QuicEventLoop> event_loop =
      GetDefaultEventLoop()->Create(QuicDefaultClock::Get());
  std::string post_data = quiche::GetQuicheCommandLineFlag(FLAGS_post_data);

  MasqueOhttpClient masque_ohttp_client(event_loop.get(), ssl_ctx->get(), urls,
                                        disable_certificate_verification,
                                        address_family_for_lookup, post_data);
  if (!masque_ohttp_client.Start()) {
    return 1;
  }
  while (!masque_ohttp_client.IsDone()) {
    event_loop->RunEventLoopOnce(QuicTime::Delta::FromMilliseconds(50));
  }
  return 0;
}

}  // namespace
}  // namespace quic

int main(int argc, char *argv[]) {
  return quic::RunMasqueOhttpClient(argc, argv);
}
