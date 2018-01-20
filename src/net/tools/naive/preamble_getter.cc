// Copyright 2025 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/tools/naive/preamble_getter.h"

#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/embedder_support/user_agent_utils.h"
#include "net/base/io_buffer.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/privacy_mode.h"
#include "net/base/url_util.h"
#include "net/filter/filter_source_stream.h"
#include "net/filter/source_stream.h"
#include "net/http/structured_headers.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/stream_socket.h"
#include "net/spdy/spdy_session.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"

namespace net {

namespace {
constexpr int kBufferSize = 64 * 1024;

template <typename T>
  requires(std::is_constructible_v<net::structured_headers::Item, T>)
const std::string SerializeHeaderString(const T& value) {
  return net::structured_headers::SerializeItem(
             net::structured_headers::Item(value))
      .value_or(std::string());
}
}  // namespace

class PreambleGetter::PreambleGetterSourceStream : public SourceStream {
 public:
  explicit PreambleGetterSourceStream(StreamSocket* socket)
      : SourceStream(SourceStreamType::kNone), socket_(socket) {}
  PreambleGetterSourceStream(const PreambleGetterSourceStream&) = delete;
  PreambleGetterSourceStream& operator=(const PreambleGetterSourceStream&) =
      delete;

  ~PreambleGetterSourceStream() override = default;

  // SourceStream implementation:
  int Read(IOBuffer* dest_buffer,
           int buffer_size,
           CompletionOnceCallback callback) override {
    return socket_->Read(dest_buffer, buffer_size, std::move(callback));
  }

  std::string Description() const override { return std::string(); }

  bool MayHaveMoreBytes() const override { return true; }

 private:
  const raw_ptr<StreamSocket> socket_;
};

PreambleGetter::Request::Request() = default;
PreambleGetter::Request::~Request() = default;

PreambleGetter::PreambleGetter(
    const ProxyInfo& proxy_info,
    HttpNetworkSession* session,
    const NetworkAnonymizationKey& network_anonymization_key,
    const NetLogWithSource& net_log)
    : proxy_info_(proxy_info),
      session_(session),
      network_anonymization_key_(network_anonymization_key),
      net_log_(net_log) {
  CHECK(!proxy_info_.proxy_chain().is_direct());
  proxy_server_ = &proxy_info_.proxy_chain().Last();
  CHECK(proxy_server_->is_secure_http_like()) << *proxy_server_;
  requests_.push_back(std::make_unique<Request>());
  requests_[0]->path = "/";
  root_ = GURL("https://" + proxy_server_->host_port_pair().ToString())
              .GetAsReferrer();
  blink::UserAgentMetadata ua_metadata =
      embedder_support::GetUserAgentMetadata();
  sec_ch_ua_ = ua_metadata.SerializeBrandMajorVersionList();
  sec_ch_ua_mobile_ = SerializeHeaderString(ua_metadata.mobile);
  sec_ch_ua_platform_ = SerializeHeaderString(ua_metadata.platform);
  user_agent_ = embedder_support::GetUserAgent();
}

PreambleGetter::~PreambleGetter() = default;

void PreambleGetter::OnIOComplete(size_t preamble_index, int result) {
  CHECK_LT(preamble_index, requests_.size());
  Request& req = *requests_[preamble_index];
  DCHECK_NE(req.next_state, STATE_NONE);
  int rv = DoLoop(preamble_index, result);
  if (rv != ERR_IO_PENDING) {
    DoCallback(preamble_index, rv);
  }
}

int PreambleGetter::DoLoop(size_t preamble_index, int last_io_result) {
  CHECK_LT(preamble_index, requests_.size());
  Request& req = *requests_[preamble_index];
  DCHECK_NE(req.next_state, STATE_NONE);
  int rv = last_io_result;
  do {
    State state = req.next_state;
    req.next_state = STATE_NONE;
    switch (state) {
      case STATE_CONNECT_SERVER_COMPLETE:
        rv = DoConnectServerComplete(preamble_index, rv);
        break;
      case STATE_READ:
        rv = DoRead(preamble_index);
        break;
      case STATE_READ_COMPLETE:
        rv = DoReadComplete(preamble_index, rv);
        break;
      default:
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && req.next_state != STATE_NONE);
  return rv;
}

void PreambleGetter::AddRootHeaders(HttpRequestHeaders& headers) {
  headers.SetHeader("_method", "GET");
  headers.SetHeader("_path", "/");

  headers.SetHeader("sec-ch-ua", sec_ch_ua_);
  headers.SetHeader("sec-ch-ua-mobile", sec_ch_ua_mobile_);
  headers.SetHeader("sec-ch-ua-platform", sec_ch_ua_platform_);
  headers.SetHeader("upgrade-insecure-requests", "1");
  headers.SetHeader("user-agent", user_agent_);

  headers.SetHeader(
      "accept",
      "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/"
      "webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7");

  headers.SetHeader("sec-fetch-site", "none");
  headers.SetHeader("sec-fetch-mode", "navigate");
  headers.SetHeader("sec-fetch-user", "?1");
  headers.SetHeader("sec-fetch-dest", "document");

  headers.SetHeader("accept-encoding", "gzip, deflate, br, zstd");
  headers.SetHeader("accept-language", "en-US,en;q=0.9");
  headers.SetHeader("priority", "u=0, i");
}

void PreambleGetter::AddHeaders(const std::string& path,
                                const std::string& ext,
                                HttpRequestHeaders& headers) {
  if (ext == "css" || ext == "js") {
    headers.SetHeader("_method", "GET");
  } else {
    headers.SetHeader("_method", "HEAD");
  }
  headers.SetHeader("_path", path);

  headers.SetHeader("sec-ch-ua-platform", sec_ch_ua_platform_);
  headers.SetHeader("user-agent", user_agent_);
  headers.SetHeader("sec-ch-ua", sec_ch_ua_);
  headers.SetHeader("sec-ch-ua-mobile", sec_ch_ua_mobile_);

  if (ext == "css") {
    headers.SetHeader("accept", "text/css,*/*;q=0.1");
  } else if (ext == "js") {
    headers.SetHeader("accept", "*/*");
  } else {
    headers.SetHeader(
        "accept",
        "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8");
  }

  headers.SetHeader("sec-fetch-site", "same-origin");
  headers.SetHeader("sec-fetch-mode", "no-cors");
  if (ext == "css") {
    headers.SetHeader("sec-fetch-dest", "style");
  } else if (ext == "js") {
    headers.SetHeader("sec-fetch-dest", "script");
  } else {
    headers.SetHeader("sec-fetch-dest", "image");
  }
  headers.SetHeader("referer", root_.spec());

  headers.SetHeader("accept-encoding", "gzip, deflate, br, zstd");
  headers.SetHeader("accept-language", "en-US,en;q=0.9");
  if (ext == "css") {
    headers.SetHeader("priority", "u=0");
  } else if (ext == "js") {
    headers.SetHeader("priority", "u=2");
  } else {
    headers.SetHeader("priority", "i");
  }
}

int PreambleGetter::Start(size_t preamble_index,
                          CompletionOnceCallback callback,
                          bool log_url) {
  CHECK_LT(preamble_index, requests_.size());
  Request& req = *requests_[preamble_index];
  req.next_state = STATE_CONNECT_SERVER_COMPLETE;

  url::SchemeHostPort endpoint("http", "preamble", preamble_index,
                               url::SchemeHostPort::ALREADY_CANONICALIZED);
  CHECK(endpoint.IsValid());

  req.callback = std::move(callback);

  HttpRequestHeaders headers;
  if (preamble_index == 0) {
    AddRootHeaders(headers);
  } else {
    AddHeaders(req.path, req.ext, headers);
  }
  naive_proxy_delegate()->SetPreambleRequestHeaders(*proxy_server_,
                                                    preamble_index, headers);

  if (log_url) {
    LOG(INFO) << "Preamble " << root_.Resolve(req.path).spec();
  }
  RequestPriority priority = LOWEST;
  if (req.path == "/" || req.ext == "css") {
    priority = HIGHEST;
  } else if (req.ext == ".js") {
    priority = LOW;
  }
  // The preamble requests have to be sent through this API instead of regular
  // URLRequests because regular URLRequests are tunneled first in CONNECT
  // requests. The purpose of preamble is to send regular GET requests.
  return InitSocketHandleForHttpRequest(
      std::move(endpoint), LOAD_NORMAL, priority, session_, proxy_info_, {},
      PRIVACY_MODE_DISABLED, network_anonymization_key_,
      SecureDnsPolicy::kDisable, SocketTag(), handles::kInvalidNetworkHandle,
      net_log_, req.server_socket_handle.get(),
      base::BindOnce(&PreambleGetter::OnIOComplete,
                     weak_ptr_factory_.GetWeakPtr(), preamble_index),
      ClientSocketPool::ProxyAuthCallback());
}

int PreambleGetter::DoConnectServerComplete(size_t preamble_index, int result) {
  if (result < 0) {
    return result;
  }

  CHECK_LT(preamble_index, requests_.size());
  Request& req = *requests_[preamble_index];
  const HttpResponseHeaders* headers =
      naive_proxy_delegate()->GetPreambleResponseHeaders(*proxy_server_,
                                                         preamble_index);
  if (headers == nullptr) {
    LOG(ERROR) << "Failed to get preamble response headers";
    return ERR_INVALID_ARGUMENT;
  }

  // Only cares about decoding the first preamble request
  if (preamble_index == 0) {
    std::vector<SourceStreamType> types =
        FilterSourceStream::GetContentEncodingTypes(std::nullopt, *headers);
    req.upstream = FilterSourceStream::CreateDecodingSourceStream(
        std::make_unique<PreambleGetterSourceStream>(
            req.server_socket_handle->socket()),
        types);
  } else {
    req.upstream = std::make_unique<PreambleGetterSourceStream>(
        req.server_socket_handle->socket());
  }
  req.next_state = STATE_READ;
  return OK;
}

void PreambleGetter::DoCallback(size_t preamble_index, int result) {
  DCHECK_NE(result, ERR_IO_PENDING);

  CHECK_LT(preamble_index, requests_.size());
  Request& req = *requests_[preamble_index];
  if (req.callback) {
    std::move(req.callback).Run(result);
  }
  req.read_buffer.reset();
}

int PreambleGetter::DoRead(size_t preamble_index) {
  CHECK_LT(preamble_index, requests_.size());
  Request& req = *requests_[preamble_index];

  req.next_state = STATE_READ_COMPLETE;
  int read_size = kBufferSize;
  req.read_buffer = base::MakeRefCounted<IOBufferWithSize>(kBufferSize);
  return req.upstream->Read(
      req.read_buffer.get(), read_size,
      base::BindOnce(&PreambleGetter::OnIOComplete,
                     weak_ptr_factory_.GetWeakPtr(), preamble_index));
}

namespace {

bool ConsumeAttrValue(std::string_view input,
                      size_t* i,
                      std::string_view* out) {
  size_t pos = *i;
  while (pos < input.size() && base::IsAsciiWhitespace(input[pos])) {
    ++pos;
  }

  if (pos >= input.size() || input[pos] != '=') {
    *i = (pos < input.size()) ? pos + 1 : pos;
    return false;
  }
  ++pos;

  while (pos < input.size() && base::IsAsciiWhitespace(input[pos])) {
    ++pos;
  }

  if (pos >= input.size()) {
    *i = pos;
    return false;
  }

  if (input[pos] == '"' || input[pos] == '\'') {
    char quote = input[pos];
    ++pos;
    size_t start = pos;
    while (pos < input.size() && input[pos] != quote) {
      ++pos;
    }

    *out = input.substr(start, pos - start);
    if (pos < input.size()) {
      ++pos;
    }
  } else {
    size_t start = pos;
    while (pos < input.size() && !base::IsAsciiWhitespace(input[pos]) &&
           input[pos] != '>') {
      ++pos;
    }
    *out = input.substr(start, pos - start);
  }

  *i = pos;
  return true;
}

bool StartsWithTag(std::string_view tag, const char* name) {
  return base::StartsWith(tag, name, base::CompareCase::INSENSITIVE_ASCII);
}

std::vector<std::string_view> ExtractLinkAndScriptURLs(std::string_view html) {
  std::vector<std::string_view> results;

  size_t pos = 0;
  while (pos < html.size()) {
    size_t lt = html.find('<', pos);
    if (lt == std::string_view::npos)
      break;

    size_t gt = html.find('>', lt);
    if (gt == std::string_view::npos)
      break;

    std::string_view tag = base::TrimWhitespaceASCII(
        html.substr(lt + 1, gt - lt - 1), base::TRIM_LEADING);

    bool is_link = StartsWithTag(tag, "link");
    bool is_script = StartsWithTag(tag, "script");
    bool is_img = StartsWithTag(tag, "img");

    if (!is_link && !is_script && !is_img) {
      pos = gt + 1;
      continue;
    }

    size_t i = 0;

    while (i < tag.size()) {
      while (i < tag.size() && base::IsAsciiWhitespace(tag[i]))
        ++i;

      size_t name_start = i;
      while (i < tag.size() && !base::IsAsciiWhitespace(tag[i]) &&
             tag[i] != '=') {
        ++i;
      }

      std::string_view attr = tag.substr(name_start, i - name_start);

      std::string_view value;

      if (is_link && base::EqualsCaseInsensitiveASCII(attr, "href")) {
        if (ConsumeAttrValue(tag, &i, &value)) {
          results.push_back(value);
        }
      } else if (is_script && base::EqualsCaseInsensitiveASCII(attr, "src")) {
        if (ConsumeAttrValue(tag, &i, &value)) {
          results.push_back(value);
        }
      } else if (is_img && base::EqualsCaseInsensitiveASCII(attr, "src")) {
        if (ConsumeAttrValue(tag, &i, &value)) {
          results.push_back(value);
        }
      } else {
        size_t tmp = i;
        std::string_view dummy;
        if (ConsumeAttrValue(tag, &tmp, &dummy)) {
          i = tmp;
        } else {
          // Advance at least one char to avoid infinite loop
          i += (i < tag.size());
        }
      }
    }

    pos = gt + 1;
  }

  return results;
}

}  // namespace

int PreambleGetter::DoReadComplete(size_t preamble_index, int result) {
  if (result <= 0) {
    return result;
  }

  CHECK_LT(preamble_index, requests_.size());
  Request& req = *requests_[preamble_index];

  std::string_view html(req.read_buffer->data(), static_cast<size_t>(result));
  std::vector<std::string_view> links = ExtractLinkAndScriptURLs(html);
  for (std::string_view link : links) {
    GURL gurl = root_.Resolve(link);
    if (!gurl.is_valid())
      continue;
    if (gurl.host() != proxy_server_->GetHost() ||
        gurl.EffectiveIntPort() != proxy_server_->GetPort())
      continue;

    std::string_view path = gurl.PathForRequestPiece();
    bool added = false;
    for (const std::unique_ptr<Request>& r : requests_) {
      if (r->path == path) {
        added = true;
        break;
      }
    }
    if (added)
      continue;
    std::string filename = gurl.ExtractFileName();
    std::string ext;
    auto pos = filename.find_last_of('.');
    if (pos != std::string_view::npos) {
      ext = filename.substr(pos + 1);
    }

    bool ext_ok = false;
    for (const char* pat : {"css", "js", "png", "gif", "jpg", "jpeg", "webp",
                            "bmp", "avif", "jxl"}) {
      if (ext == pat) {
        ext_ok = true;
        break;
      }
    }
    if (!ext_ok)
      continue;
    requests_.push_back(std::make_unique<Request>());
    requests_.back()->path = path;
    requests_.back()->ext = ext;
    (void)Start(requests_.size() - 1, {});
  }
  req.next_state = STATE_READ;
  return OK;
}

void PreambleGetter::StartOne() {
  if (requests_.empty())
    return;
  int index = base::RandIntInclusive(0, requests_.size() - 1);
  (void)Start(index, {}, /*log_url=*/false);
}

NaiveProxyDelegate* PreambleGetter::naive_proxy_delegate() const {
  auto* proxy_delegate =
      static_cast<NaiveProxyDelegate*>(session_->context().proxy_delegate);
  DCHECK(proxy_delegate);
  return proxy_delegate;
}

}  // namespace net
