// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_http_response_cache.h"

#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "net/http/http_util.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_map_util.h"
#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/quic/platform/api/quic_text_utils.h"
#include "net/spdy/chromium/spdy_http_utils.h"

using base::FilePath;
using base::IntToString;
using std::string;

namespace net {

QuicHttpResponseCache::ServerPushInfo::ServerPushInfo(QuicUrl request_url,
                                                      SpdyHeaderBlock headers,
                                                      SpdyPriority priority,
                                                      string body)
    : request_url(request_url),
      headers(std::move(headers)),
      priority(priority),
      body(body) {}

QuicHttpResponseCache::ServerPushInfo::ServerPushInfo(
    const ServerPushInfo& other)
    : request_url(other.request_url),
      headers(other.headers.Clone()),
      priority(other.priority),
      body(other.body) {}

QuicHttpResponseCache::Response::Response()
    : response_type_(REGULAR_RESPONSE) {}

QuicHttpResponseCache::Response::~Response() {}

QuicHttpResponseCache::ResourceFile::ResourceFile(
    const base::FilePath& file_name)
    : file_name_(file_name), file_name_string_(file_name.AsUTF8Unsafe()) {}

QuicHttpResponseCache::ResourceFile::~ResourceFile() {}

void QuicHttpResponseCache::ResourceFile::Read() {
  base::ReadFileToString(FilePath(file_name_), &file_contents_);

  // First read the headers.
  size_t start = 0;
  while (start < file_contents_.length()) {
    size_t pos = file_contents_.find("\n", start);
    if (pos == string::npos) {
      QUIC_LOG(DFATAL) << "Headers invalid or empty, ignoring: "
                       << file_name_.value();
      return;
    }
    size_t len = pos - start;
    // Support both dos and unix line endings for convenience.
    if (file_contents_[pos - 1] == '\r') {
      len -= 1;
    }
    QuicStringPiece line(file_contents_.data() + start, len);
    start = pos + 1;
    // Headers end with an empty line.
    if (line.empty()) {
      break;
    }
    // Extract the status from the HTTP first line.
    if (line.substr(0, 4) == "HTTP") {
      pos = line.find(" ");
      if (pos == string::npos) {
        QUIC_LOG(DFATAL) << "Headers invalid or empty, ignoring: "
                         << file_name_.value();
        return;
      }
      spdy_headers_[":status"] = line.substr(pos + 1, 3);
      continue;
    }
    // Headers are "key: value".
    pos = line.find(": ");
    if (pos == string::npos) {
      QUIC_LOG(DFATAL) << "Headers invalid or empty, ignoring: "
                       << file_name_.value();
      return;
    }
    spdy_headers_.AppendValueOrAddHeader(
        QuicTextUtils::ToLower(line.substr(0, pos)), line.substr(pos + 2));
  }

  // The connection header is prohibited in HTTP/2.
  spdy_headers_.erase("connection");

  // Override the URL with the X-Original-Url header, if present.
  auto it = spdy_headers_.find("x-original-url");
  if (it != spdy_headers_.end()) {
    x_original_url_ = it->second;
    HandleXOriginalUrl();
  }

  // X-Push-URL header is a relatively quick way to support sever push
  // in the toy server.  A production server should use link=preload
  // stuff as described in https://w3c.github.io/preload/.
  it = spdy_headers_.find("x-push-url");
  if (it != spdy_headers_.end()) {
    QuicStringPiece push_urls = it->second;
    size_t start = 0;
    while (start < push_urls.length()) {
      size_t pos = push_urls.find('\0', start);
      if (pos == string::npos) {
        push_urls_.push_back(QuicStringPiece(push_urls.data() + start,
                                             push_urls.length() - start));
        break;
      }
      push_urls_.push_back(QuicStringPiece(push_urls.data() + start, pos));
      start += pos + 1;
    }
  }

  body_ = QuicStringPiece(file_contents_.data() + start,
                          file_contents_.size() - start);
}

void QuicHttpResponseCache::ResourceFile::SetHostPathFromBase(
    QuicStringPiece base) {
  size_t path_start = base.find_first_of('/');
  DCHECK_LT(0UL, path_start);
  host_ = base.substr(0, path_start);
  size_t query_start = base.find_first_of(',');
  if (query_start > 0) {
    path_ = base.substr(path_start, query_start - 1);
  } else {
    path_ = base.substr(path_start);
  }
}

QuicStringPiece QuicHttpResponseCache::ResourceFile::RemoveScheme(
    QuicStringPiece url) {
  if (QuicTextUtils::StartsWith(url, "https://")) {
    url.remove_prefix(8);
  } else if (QuicTextUtils::StartsWith(url, "http://")) {
    url.remove_prefix(7);
  }
  return url;
}

void QuicHttpResponseCache::ResourceFile::HandleXOriginalUrl() {
  QuicStringPiece url(x_original_url_);
  // Remove the protocol so we can add it below.
  url = RemoveScheme(url);
  SetHostPathFromBase(url);
}

const QuicHttpResponseCache::Response* QuicHttpResponseCache::GetResponse(
    QuicStringPiece host,
    QuicStringPiece path) const {
  QuicWriterMutexLock lock(&response_mutex_);

  auto it = responses_.find(GetKey(host, path));
  if (it == responses_.end()) {
    DVLOG(1) << "Get response for resource failed: host " << host << " path "
             << path;
    if (default_response_) {
      return default_response_.get();
    }
    return nullptr;
  }
  return it->second.get();
}

typedef QuicHttpResponseCache::ServerPushInfo ServerPushInfo;

void QuicHttpResponseCache::AddSimpleResponse(QuicStringPiece host,
                                              QuicStringPiece path,
                                              int response_code,
                                              QuicStringPiece body) {
  SpdyHeaderBlock response_headers;
  response_headers[":status"] = QuicTextUtils::Uint64ToString(response_code);
  response_headers["content-length"] =
      QuicTextUtils::Uint64ToString(body.length());
  AddResponse(host, path, std::move(response_headers), body);
}

void QuicHttpResponseCache::AddSimpleResponseWithServerPushResources(
    QuicStringPiece host,
    QuicStringPiece path,
    int response_code,
    QuicStringPiece body,
    std::list<ServerPushInfo> push_resources) {
  AddSimpleResponse(host, path, response_code, body);
  MaybeAddServerPushResources(host, path, push_resources);
}

void QuicHttpResponseCache::AddDefaultResponse(Response* response) {
  QuicWriterMutexLock lock(&response_mutex_);
  default_response_.reset(response);
}

void QuicHttpResponseCache::AddResponse(QuicStringPiece host,
                                        QuicStringPiece path,
                                        SpdyHeaderBlock response_headers,
                                        QuicStringPiece response_body) {
  AddResponseImpl(host, path, REGULAR_RESPONSE, std::move(response_headers),
                  response_body, SpdyHeaderBlock());
}

void QuicHttpResponseCache::AddResponse(QuicStringPiece host,
                                        QuicStringPiece path,
                                        SpdyHeaderBlock response_headers,
                                        QuicStringPiece response_body,
                                        SpdyHeaderBlock response_trailers) {
  AddResponseImpl(host, path, REGULAR_RESPONSE, std::move(response_headers),
                  response_body, std::move(response_trailers));
}

void QuicHttpResponseCache::AddSpecialResponse(
    QuicStringPiece host,
    QuicStringPiece path,
    SpecialResponseType response_type) {
  AddResponseImpl(host, path, response_type, SpdyHeaderBlock(), "",
                  SpdyHeaderBlock());
}

QuicHttpResponseCache::QuicHttpResponseCache() {}

void QuicHttpResponseCache::InitializeFromDirectory(
    const string& cache_directory) {
  if (cache_directory.empty()) {
    QUIC_BUG << "cache_directory must not be empty.";
    return;
  }
  QUIC_LOG(INFO)
      << "Attempting to initialize QuicHttpResponseCache from directory: "
      << cache_directory;
  FilePath directory(FilePath::FromUTF8Unsafe(cache_directory));
  base::FileEnumerator file_list(directory, true, base::FileEnumerator::FILES);
  std::list<std::unique_ptr<ResourceFile>> resource_files;
  for (FilePath file_iter = file_list.Next(); !file_iter.empty();
       file_iter = file_list.Next()) {
    // Need to skip files in .svn directories
    if (file_iter.value().find(FILE_PATH_LITERAL("/.svn/")) != string::npos) {
      continue;
    }

    std::unique_ptr<ResourceFile> resource_file(new ResourceFile(file_iter));

    // Tease apart filename into host and path.
    QuicStringPiece base(resource_file->file_name());
    base.remove_prefix(cache_directory.length());
    if (base[0] == '/') {
      base.remove_prefix(1);
    }

    resource_file->SetHostPathFromBase(base);
    resource_file->Read();

    AddResponse(resource_file->host(), resource_file->path(),
                resource_file->spdy_headers().Clone(), resource_file->body());

    resource_files.push_back(std::move(resource_file));
  }

  for (const auto& resource_file : resource_files) {
    std::list<ServerPushInfo> push_resources;
    for (const auto& push_url : resource_file->push_urls()) {
      QuicUrl url(push_url);
      const Response* response = GetResponse(url.host(), url.path());
      if (!response) {
        QUIC_BUG << "Push URL '" << push_url << "' not found.";
        return;
      }
      push_resources.push_back(ServerPushInfo(url, response->headers().Clone(),
                                              kV3LowestPriority,
                                              response->body().as_string()));
    }
    MaybeAddServerPushResources(resource_file->host(), resource_file->path(),
                                push_resources);
  }
}

std::list<ServerPushInfo> QuicHttpResponseCache::GetServerPushResources(
    string request_url) {
  QuicWriterMutexLock lock(&response_mutex_);

  std::list<ServerPushInfo> resources;
  auto resource_range = server_push_resources_.equal_range(request_url);
  for (auto it = resource_range.first; it != resource_range.second; ++it) {
    resources.push_back(it->second);
  }
  QUIC_DVLOG(1) << "Found " << resources.size() << " push resources for "
                << request_url;
  return resources;
}

QuicHttpResponseCache::~QuicHttpResponseCache() {
  {
    QuicWriterMutexLock lock(&response_mutex_);
    responses_.clear();
  }
}

void QuicHttpResponseCache::AddResponseImpl(QuicStringPiece host,
                                            QuicStringPiece path,
                                            SpecialResponseType response_type,
                                            SpdyHeaderBlock response_headers,
                                            QuicStringPiece response_body,
                                            SpdyHeaderBlock response_trailers) {
  QuicWriterMutexLock lock(&response_mutex_);

  DCHECK(!host.empty()) << "Host must be populated, e.g. \"www.google.com\"";
  string key = GetKey(host, path);
  if (QuicContainsKey(responses_, key)) {
    QUIC_BUG << "Response for '" << key << "' already exists!";
    return;
  }
  auto new_response = QuicMakeUnique<Response>();
  new_response->set_response_type(response_type);
  new_response->set_headers(std::move(response_headers));
  new_response->set_body(response_body);
  new_response->set_trailers(std::move(response_trailers));
  QUIC_DVLOG(1) << "Add response with key " << key;
  responses_[key] = std::move(new_response);
}

string QuicHttpResponseCache::GetKey(QuicStringPiece host,
                                     QuicStringPiece path) const {
  return host.as_string() + path.as_string();
}

void QuicHttpResponseCache::MaybeAddServerPushResources(
    QuicStringPiece request_host,
    QuicStringPiece request_path,
    std::list<ServerPushInfo> push_resources) {
  string request_url = GetKey(request_host, request_path);

  for (const auto& push_resource : push_resources) {
    if (PushResourceExistsInCache(request_url, push_resource)) {
      continue;
    }

    QUIC_DVLOG(1) << "Add request-resource association: request url "
                  << request_url << " push url "
                  << push_resource.request_url.ToString()
                  << " response headers "
                  << push_resource.headers.DebugString();
    {
      QuicWriterMutexLock lock(&response_mutex_);
      server_push_resources_.insert(std::make_pair(request_url, push_resource));
    }
    string host = push_resource.request_url.host();
    if (host.empty()) {
      host = request_host.as_string();
    }
    string path = push_resource.request_url.path();
    bool found_existing_response = false;
    {
      QuicWriterMutexLock lock(&response_mutex_);
      found_existing_response = QuicContainsKey(responses_, GetKey(host, path));
    }
    if (!found_existing_response) {
      // Add a server push response to responses map, if it is not in the map.
      QuicStringPiece body = push_resource.body;
      QUIC_DVLOG(1) << "Add response for push resource: host " << host
                    << " path " << path;
      AddResponse(host, path, push_resource.headers.Clone(), body);
    }
  }
}

bool QuicHttpResponseCache::PushResourceExistsInCache(
    string original_request_url,
    ServerPushInfo resource) {
  QuicWriterMutexLock lock(&response_mutex_);
  auto resource_range =
      server_push_resources_.equal_range(original_request_url);
  for (auto it = resource_range.first; it != resource_range.second; ++it) {
    ServerPushInfo push_resource = it->second;
    if (push_resource.request_url.ToString() ==
        resource.request_url.ToString()) {
      return true;
    }
  }
  return false;
}

}  // namespace net
