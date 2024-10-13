// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_memory_cache_backend.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/spdy_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/tools/web_transport_test_visitors.h"
#include "quiche/common/platform/api/quiche_file_utils.h"
#include "quiche/common/quiche_text_utils.h"

using quiche::HttpHeaderBlock;
using spdy::kV3LowestPriority;

namespace quic {

QuicMemoryCacheBackend::ResourceFile::ResourceFile(const std::string& file_name)
    : file_name_(file_name) {}

QuicMemoryCacheBackend::ResourceFile::~ResourceFile() = default;

void QuicMemoryCacheBackend::ResourceFile::Read() {
  std::optional<std::string> maybe_file_contents =
      quiche::ReadFileContents(file_name_);
  if (!maybe_file_contents) {
    QUIC_LOG(DFATAL) << "Failed to read file for the memory cache backend: "
                     << file_name_;
    return;
  }
  file_contents_ = *maybe_file_contents;

  // First read the headers.
  for (size_t start = 0; start < file_contents_.length();) {
    size_t pos = file_contents_.find('\n', start);
    if (pos == std::string::npos) {
      QUIC_LOG(DFATAL) << "Headers invalid or empty, ignoring: " << file_name_;
      return;
    }
    size_t len = pos - start;
    // Support both dos and unix line endings for convenience.
    if (file_contents_[pos - 1] == '\r') {
      len -= 1;
    }
    absl::string_view line(file_contents_.data() + start, len);
    start = pos + 1;
    // Headers end with an empty line.
    if (line.empty()) {
      body_ = absl::string_view(file_contents_.data() + start,
                                file_contents_.size() - start);
      break;
    }
    // Extract the status from the HTTP first line.
    if (line.substr(0, 4) == "HTTP") {
      pos = line.find(' ');
      if (pos == std::string::npos) {
        QUIC_LOG(DFATAL) << "Headers invalid or empty, ignoring: "
                         << file_name_;
        return;
      }
      spdy_headers_[":status"] = line.substr(pos + 1, 3);
      continue;
    }
    // Headers are "key: value".
    pos = line.find(": ");
    if (pos == std::string::npos) {
      QUIC_LOG(DFATAL) << "Headers invalid or empty, ignoring: " << file_name_;
      return;
    }
    spdy_headers_.AppendValueOrAddHeader(
        quiche::QuicheTextUtils::ToLower(line.substr(0, pos)),
        line.substr(pos + 2));
  }

  // The connection header is prohibited in HTTP/2.
  spdy_headers_.erase("connection");

  // Override the URL with the X-Original-Url header, if present.
  if (auto it = spdy_headers_.find("x-original-url");
      it != spdy_headers_.end()) {
    x_original_url_ = it->second;
    HandleXOriginalUrl();
  }
}

void QuicMemoryCacheBackend::ResourceFile::SetHostPathFromBase(
    absl::string_view base) {
  QUICHE_DCHECK(base[0] != '/') << base;
  size_t path_start = base.find_first_of('/');
  if (path_start == absl::string_view::npos) {
    host_ = std::string(base);
    path_ = "";
    return;
  }

  host_ = std::string(base.substr(0, path_start));
  size_t query_start = base.find_first_of(',');
  if (query_start > 0) {
    path_ = std::string(base.substr(path_start, query_start - 1));
  } else {
    path_ = std::string(base.substr(path_start));
  }
}

absl::string_view QuicMemoryCacheBackend::ResourceFile::RemoveScheme(
    absl::string_view url) {
  if (absl::StartsWith(url, "https://")) {
    url.remove_prefix(8);
  } else if (absl::StartsWith(url, "http://")) {
    url.remove_prefix(7);
  }
  return url;
}

void QuicMemoryCacheBackend::ResourceFile::HandleXOriginalUrl() {
  absl::string_view url(x_original_url_);
  SetHostPathFromBase(RemoveScheme(url));
}

const QuicBackendResponse* QuicMemoryCacheBackend::GetResponse(
    absl::string_view host, absl::string_view path) const {
  quiche::QuicheWriterMutexLock lock(&response_mutex_);

  auto it = responses_.find(GetKey(host, path));
  if (it == responses_.end()) {
    uint64_t ignored = 0;
    if (generate_bytes_response_) {
      if (absl::SimpleAtoi(absl::string_view(path.data() + 1, path.size() - 1),
                           &ignored)) {
        // The actual parsed length is ignored here and will be recomputed
        // by the caller.
        return generate_bytes_response_.get();
      }
    }
    QUIC_DVLOG(1) << "Get response for resource failed: host " << host
                  << " path " << path;
    if (default_response_) {
      return default_response_.get();
    }
    return nullptr;
  }
  return it->second.get();
}

using SpecialResponseType = QuicBackendResponse::SpecialResponseType;

void QuicMemoryCacheBackend::AddSimpleResponse(absl::string_view host,
                                               absl::string_view path,
                                               int response_code,
                                               absl::string_view body) {
  HttpHeaderBlock response_headers;
  response_headers[":status"] = absl::StrCat(response_code);
  response_headers["content-length"] = absl::StrCat(body.length());
  AddResponse(host, path, std::move(response_headers), body);
}

void QuicMemoryCacheBackend::AddDefaultResponse(QuicBackendResponse* response) {
  quiche::QuicheWriterMutexLock lock(&response_mutex_);
  default_response_.reset(response);
}

void QuicMemoryCacheBackend::AddResponse(absl::string_view host,
                                         absl::string_view path,
                                         HttpHeaderBlock response_headers,
                                         absl::string_view response_body) {
  AddResponseImpl(host, path, QuicBackendResponse::REGULAR_RESPONSE,
                  std::move(response_headers), response_body, HttpHeaderBlock(),
                  std::vector<quiche::HttpHeaderBlock>());
}

void QuicMemoryCacheBackend::AddResponse(absl::string_view host,
                                         absl::string_view path,
                                         HttpHeaderBlock response_headers,
                                         absl::string_view response_body,
                                         HttpHeaderBlock response_trailers) {
  AddResponseImpl(host, path, QuicBackendResponse::REGULAR_RESPONSE,
                  std::move(response_headers), response_body,
                  std::move(response_trailers),
                  std::vector<quiche::HttpHeaderBlock>());
}

bool QuicMemoryCacheBackend::SetResponseDelay(absl::string_view host,
                                              absl::string_view path,
                                              QuicTime::Delta delay) {
  quiche::QuicheWriterMutexLock lock(&response_mutex_);
  auto it = responses_.find(GetKey(host, path));
  if (it == responses_.end()) return false;

  it->second->set_delay(delay);
  return true;
}

void QuicMemoryCacheBackend::AddResponseWithEarlyHints(
    absl::string_view host, absl::string_view path,
    quiche::HttpHeaderBlock response_headers, absl::string_view response_body,
    const std::vector<quiche::HttpHeaderBlock>& early_hints) {
  AddResponseImpl(host, path, QuicBackendResponse::REGULAR_RESPONSE,
                  std::move(response_headers), response_body, HttpHeaderBlock(),
                  early_hints);
}

void QuicMemoryCacheBackend::AddSpecialResponse(
    absl::string_view host, absl::string_view path,
    SpecialResponseType response_type) {
  AddResponseImpl(host, path, response_type, HttpHeaderBlock(), "",
                  HttpHeaderBlock(), std::vector<quiche::HttpHeaderBlock>());
}

void QuicMemoryCacheBackend::AddSpecialResponse(
    absl::string_view host, absl::string_view path,
    quiche::HttpHeaderBlock response_headers, absl::string_view response_body,
    SpecialResponseType response_type) {
  AddResponseImpl(host, path, response_type, std::move(response_headers),
                  response_body, HttpHeaderBlock(),
                  std::vector<quiche::HttpHeaderBlock>());
}

QuicMemoryCacheBackend::QuicMemoryCacheBackend() : cache_initialized_(false) {}

bool QuicMemoryCacheBackend::InitializeBackend(
    const std::string& cache_directory) {
  if (cache_directory.empty()) {
    QUIC_BUG(quic_bug_10932_1) << "cache_directory must not be empty.";
    return false;
  }
  QUIC_LOG(INFO)
      << "Attempting to initialize QuicMemoryCacheBackend from directory: "
      << cache_directory;
  std::vector<std::string> files;
  if (!quiche::EnumerateDirectoryRecursively(cache_directory, files)) {
    QUIC_BUG(QuicMemoryCacheBackend unreadable directory)
        << "Can't read QuicMemoryCacheBackend directory: " << cache_directory;
    return false;
  }
  for (const auto& filename : files) {
    std::unique_ptr<ResourceFile> resource_file(new ResourceFile(filename));

    // Tease apart filename into host and path.
    std::string base(resource_file->file_name());
    // Transform windows path separators to URL path separators.
    for (size_t i = 0; i < base.length(); ++i) {
      if (base[i] == '\\') {
        base[i] = '/';
      }
    }
    base.erase(0, cache_directory.length());
    if (base[0] == '/') {
      base.erase(0, 1);
    }

    resource_file->SetHostPathFromBase(base);
    resource_file->Read();

    AddResponse(resource_file->host(), resource_file->path(),
                resource_file->spdy_headers().Clone(), resource_file->body());
  }

  cache_initialized_ = true;
  return true;
}

void QuicMemoryCacheBackend::GenerateDynamicResponses() {
  quiche::QuicheWriterMutexLock lock(&response_mutex_);
  // Add a generate bytes response.
  quiche::HttpHeaderBlock response_headers;
  response_headers[":status"] = "200";
  generate_bytes_response_ = std::make_unique<QuicBackendResponse>();
  generate_bytes_response_->set_headers(std::move(response_headers));
  generate_bytes_response_->set_response_type(
      QuicBackendResponse::GENERATE_BYTES);
}

void QuicMemoryCacheBackend::EnableWebTransport() {
  enable_webtransport_ = true;
}

bool QuicMemoryCacheBackend::IsBackendInitialized() const {
  return cache_initialized_;
}

void QuicMemoryCacheBackend::FetchResponseFromBackend(
    const HttpHeaderBlock& request_headers, const std::string& request_body,
    QuicSimpleServerBackend::RequestHandler* quic_stream) {
  const QuicBackendResponse* quic_response = nullptr;
  // Find response in cache. If not found, send error response.
  auto authority = request_headers.find(":authority");
  auto path_it = request_headers.find(":path");
  const absl::string_view* path = nullptr;
  if (path_it != request_headers.end()) {
    path = &path_it->second;
  }
  auto method = request_headers.find(":method");
  std::unique_ptr<QuicBackendResponse> echo_response;
  if (path && *path == "/echo" && method != request_headers.end() &&
      method->second == "POST") {
    echo_response = std::make_unique<QuicBackendResponse>();
    quiche::HttpHeaderBlock response_headers;
    response_headers[":status"] = "200";
    echo_response->set_headers(std::move(response_headers));
    echo_response->set_body(request_body);
    quic_response = echo_response.get();
  } else if (authority != request_headers.end() && path) {
    quic_response = GetResponse(authority->second, *path);
  }

  std::string request_url;
  if (authority != request_headers.end()) {
    request_url = std::string(authority->second);
  }
  if (path) {
    request_url += std::string(*path);
  }
  QUIC_DVLOG(1)
      << "Fetching QUIC response from backend in-memory cache for url "
      << request_url;
  quic_stream->OnResponseBackendComplete(quic_response);
}

// The memory cache does not have a per-stream handler
void QuicMemoryCacheBackend::CloseBackendResponseStream(
    QuicSimpleServerBackend::RequestHandler* /*quic_stream*/) {}

QuicMemoryCacheBackend::WebTransportResponse
QuicMemoryCacheBackend::ProcessWebTransportRequest(
    const quiche::HttpHeaderBlock& request_headers,
    WebTransportSession* session) {
  if (!SupportsWebTransport()) {
    return QuicSimpleServerBackend::ProcessWebTransportRequest(request_headers,
                                                               session);
  }

  auto path_it = request_headers.find(":path");
  if (path_it == request_headers.end()) {
    WebTransportResponse response;
    response.response_headers[":status"] = "400";
    return response;
  }
  absl::string_view path = path_it->second;
  if (path == "/echo") {
    WebTransportResponse response;
    response.response_headers[":status"] = "200";
    response.visitor =
        std::make_unique<EchoWebTransportSessionVisitor>(session);
    return response;
  }

  WebTransportResponse response;
  response.response_headers[":status"] = "404";
  return response;
}

QuicMemoryCacheBackend::~QuicMemoryCacheBackend() {
  {
    quiche::QuicheWriterMutexLock lock(&response_mutex_);
    responses_.clear();
  }
}

void QuicMemoryCacheBackend::AddResponseImpl(
    absl::string_view host, absl::string_view path,
    SpecialResponseType response_type, HttpHeaderBlock response_headers,
    absl::string_view response_body, HttpHeaderBlock response_trailers,
    const std::vector<quiche::HttpHeaderBlock>& early_hints) {
  quiche::QuicheWriterMutexLock lock(&response_mutex_);

  QUICHE_DCHECK(!host.empty())
      << "Host must be populated, e.g. \"www.google.com\"";
  std::string key = GetKey(host, path);
  if (responses_.contains(key)) {
    QUIC_BUG(quic_bug_10932_3)
        << "Response for '" << key << "' already exists!";
    return;
  }
  auto new_response = std::make_unique<QuicBackendResponse>();
  new_response->set_response_type(response_type);
  new_response->set_headers(std::move(response_headers));
  new_response->set_body(response_body);
  new_response->set_trailers(std::move(response_trailers));
  for (auto& headers : early_hints) {
    new_response->AddEarlyHints(headers);
  }
  QUIC_DVLOG(1) << "Add response with key " << key;
  responses_[key] = std::move(new_response);
}

std::string QuicMemoryCacheBackend::GetKey(absl::string_view host,
                                           absl::string_view path) const {
  std::string host_string = std::string(host);
  size_t port = host_string.find(':');
  if (port != std::string::npos)
    host_string = std::string(host_string.c_str(), port);
  return host_string + std::string(path);
}

}  // namespace quic
