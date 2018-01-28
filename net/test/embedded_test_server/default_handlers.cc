// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/test/embedded_test_server/default_handlers.h"

#include <stdlib.h>

#include <ctime>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/format_macros.h"
#include "base/macros.h"
#include "base/md5.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "net/base/escape.h"
#include "net/base/url_util.h"
#include "net/filter/filter_source_stream_test_util.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/embedded_test_server/request_handler_util.h"

namespace net {
namespace test_server {
namespace {

const UnescapeRule::Type kUnescapeAll =
    UnescapeRule::SPACES | UnescapeRule::PATH_SEPARATORS |
    UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS |
    UnescapeRule::SPOOFING_AND_CONTROL_CHARS |
    UnescapeRule::REPLACE_PLUS_WITH_SPACE;

const char kDefaultRealm[] = "testrealm";
const char kDefaultPassword[] = "secret";
const char kEtag[] = "abc";
const char kLogoPath[] = "chrome/test/data/google/logo.gif";

// method: CONNECT
// Responses with a BAD_REQUEST to any CONNECT requests.
std::unique_ptr<HttpResponse> HandleDefaultConnect(const HttpRequest& request) {
  if (request.method != METHOD_CONNECT)
    return nullptr;

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_code(HTTP_BAD_REQUEST);
  http_response->set_content(
      "Your client has issued a malformed or illegal request.");
  http_response->set_content_type("text/html");
  return std::move(http_response);
}

// /cachetime
// Returns a cacheable response.
std::unique_ptr<HttpResponse> HandleCacheTime(const HttpRequest& request) {
  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_content(
      "<html><head><title>Cache: max-age=60</title></head></html>");
  http_response->set_content_type("text/html");
  http_response->AddCustomHeader("Cache-Control", "max-age=60");
  return std::move(http_response);
}

// /echoheader?HEADERS | /echoheadercache?HEADERS
// Responds with the headers echoed in the message body.
// echoheader does not cache the results, while echoheadercache does.
std::unique_ptr<HttpResponse> HandleEchoHeader(const std::string& url,
                                               const std::string& cache_control,
                                               const HttpRequest& request) {
  if (!ShouldHandle(request, url))
    return nullptr;

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);

  GURL request_url = request.GetURL();
  std::string vary;
  std::string content;
  RequestQuery headers = ParseQuery(request_url);
  for (const auto& header : headers) {
    std::string header_name = header.first;
    std::string header_value = "None";
    if (request.headers.find(header_name) != request.headers.end())
      header_value = request.headers.at(header_name);
    if (!vary.empty())
      vary += ",";
    vary += header_name;
    if (!content.empty())
      content += "\n";
    content += header_value;
  }

  http_response->AddCustomHeader("Vary", vary);
  http_response->set_content(content);
  http_response->set_content_type("text/plain");
  http_response->AddCustomHeader("Cache-Control", cache_control);
  return std::move(http_response);
}

// /echo?status=STATUS
// Responds with the request body as the response body and
// a status code of STATUS.
std::unique_ptr<HttpResponse> HandleEcho(const HttpRequest& request) {
  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);

  GURL request_url = request.GetURL();
  if (request_url.has_query()) {
    RequestQuery query = ParseQuery(request_url);
    if (query.find("status") != query.end())
      http_response->set_code(static_cast<HttpStatusCode>(
          std::atoi(query["status"].front().c_str())));
  }

  http_response->set_content_type("text/html");
  if (request.method != METHOD_POST && request.method != METHOD_PUT)
    http_response->set_content("Echo");
  else
    http_response->set_content(request.content);
  return std::move(http_response);
}

// /echotitle
// Responds with the request body as the title.
std::unique_ptr<HttpResponse> HandleEchoTitle(const HttpRequest& request) {
  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_content_type("text/html");
  http_response->set_content("<html><head><title>" + request.content +
                             "</title></head></html>");
  return std::move(http_response);
}

// /echoall?QUERY
// Responds with the list of QUERY and the request headers.
std::unique_ptr<HttpResponse> HandleEchoAll(const HttpRequest& request) {
  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);

  std::string body =
      "<html><head><style>"
      "pre { border: 1px solid black; margin: 5px; padding: 5px }"
      "</style></head><body>"
      "<div style=\"float: right\">"
      "<a href=\"/echo\">back to referring page</a></div>"
      "<h1>Request Body:</h1><pre>";

  if (request.has_content) {
    std::vector<std::string> query_list = base::SplitString(
        request.content, "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    for (const auto& query : query_list)
      body += query + "\n";
  }

  body +=
      "</pre>"
      "<h1>Request Headers:</h1><pre>" +
      request.all_headers +
      "</pre>"
      "</body></html>";

  http_response->set_content_type("text/html");
  http_response->set_content(body);
  return std::move(http_response);
}

// /echo-raw
// Returns the query string as the raw response (no HTTP headers).
std::unique_ptr<HttpResponse> HandleEchoRaw(const HttpRequest& request) {
  return std::make_unique<RawHttpResponse>("", request.GetURL().query());
}

// /set-cookie?COOKIES
// Sets response cookies to be COOKIES.
std::unique_ptr<HttpResponse> HandleSetCookie(const HttpRequest& request) {
  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_content_type("text/html");
  std::string content;
  GURL request_url = request.GetURL();
  if (request_url.has_query()) {
    std::vector<std::string> cookies = base::SplitString(
        request_url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    for (const auto& cookie : cookies) {
      http_response->AddCustomHeader("Set-Cookie", cookie);
      content += cookie;
    }
  }

  http_response->set_content(content);
  return std::move(http_response);
}

// /set-many-cookies?N
// Sets N cookies in the response.
std::unique_ptr<HttpResponse> HandleSetManyCookies(const HttpRequest& request) {
  std::string content;

  GURL request_url = request.GetURL();
  size_t num = 0;
  if (request_url.has_query())
    num = std::atoi(request_url.query().c_str());

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_content_type("text/html");
  for (size_t i = 0; i < num; ++i) {
    http_response->AddCustomHeader("Set-Cookie", "a=");
  }

  http_response->set_content(
      base::StringPrintf("%" PRIuS " cookies were sent", num));
  return std::move(http_response);
}

// /expect-and-set-cookie?expect=EXPECTED&set=SET&data=DATA
// Verifies that the request cookies match EXPECTED and then returns cookies
// that match SET and a content that matches DATA.
std::unique_ptr<HttpResponse> HandleExpectAndSetCookie(
    const HttpRequest& request) {
  std::vector<std::string> received_cookies;
  if (request.headers.find("Cookie") != request.headers.end()) {
    received_cookies =
        base::SplitString(request.headers.at("Cookie"), ";",
                          base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  }

  bool got_all_expected = true;
  GURL request_url = request.GetURL();
  RequestQuery query_list = ParseQuery(request_url);
  if (query_list.find("expect") != query_list.end()) {
    for (const auto& expected_cookie : query_list.at("expect")) {
      bool found = false;
      for (const auto& received_cookie : received_cookies) {
        if (expected_cookie == received_cookie)
          found = true;
      }
      got_all_expected &= found;
    }
  }

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_content_type("text/html");
  if (got_all_expected) {
    for (const auto& cookie : query_list.at("set")) {
      http_response->AddCustomHeader(
          "Set-Cookie", net::UnescapeURLComponent(cookie, kUnescapeAll));
    }
  }

  std::string content;
  if (query_list.find("data") != query_list.end()) {
    for (const auto& item : query_list.at("data"))
      content += item;
  }

  http_response->set_content(content);
  return std::move(http_response);
}

// /set-header?HEADERS
// Returns a response with HEADERS set as the response headers.
std::unique_ptr<HttpResponse> HandleSetHeader(const HttpRequest& request) {
  std::string content;

  GURL request_url = request.GetURL();

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_content_type("text/html");
  if (request_url.has_query()) {
    RequestQuery headers = ParseQuery(request_url);
    for (const auto& header : headers) {
      size_t delimiter = header.first.find(": ");
      if (delimiter == std::string::npos)
        continue;
      std::string key = header.first.substr(0, delimiter);
      std::string value = header.first.substr(delimiter + 2);
      http_response->AddCustomHeader(key, value);
      content += header.first;
    }
  }

  http_response->set_content(content);
  return std::move(http_response);
}

// /nocontent
// Returns a NO_CONTENT response.
std::unique_ptr<HttpResponse> HandleNoContent(const HttpRequest& request) {
  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_code(HTTP_NO_CONTENT);
  return std::move(http_response);
}

// /close-socket
// Immediately closes the connection.
std::unique_ptr<HttpResponse> HandleCloseSocket(const HttpRequest& request) {
  std::unique_ptr<RawHttpResponse> http_response(new RawHttpResponse("", ""));
  return std::move(http_response);
}

// /auth-basic?password=PASS&realm=REALM
// Performs "Basic" HTTP authentication using expected password PASS and
// realm REALM.
std::unique_ptr<HttpResponse> HandleAuthBasic(const HttpRequest& request) {
  GURL request_url = request.GetURL();
  RequestQuery query = ParseQuery(request_url);

  std::string expected_password = kDefaultPassword;
  if (query.find("password") != query.end())
    expected_password = query.at("password").front();
  std::string realm = kDefaultRealm;
  if (query.find("realm") != query.end())
    realm = query.at("realm").front();

  bool authed = false;
  std::string error;
  std::string auth;
  std::string username;
  std::string userpass;
  std::string password;
  std::string b64str;
  if (request.headers.find("Authorization") == request.headers.end()) {
    error = "Missing Authorization Header";
  } else {
    auth = request.headers.at("Authorization");
    if (auth.find("Basic ") == std::string::npos) {
      error = "Invalid Authorization Header";
    } else {
      b64str = auth.substr(std::string("Basic ").size());
      base::Base64Decode(b64str, &userpass);
      size_t delimiter = userpass.find(":");
      if (delimiter != std::string::npos) {
        username = userpass.substr(0, delimiter);
        password = userpass.substr(delimiter + 1);
        if (password == expected_password)
          authed = true;
        else
          error = "Invalid Credentials";
      } else {
        error = "Invalid Credentials";
      }
    }
  }

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  if (!authed) {
    http_response->set_code(HTTP_UNAUTHORIZED);
    http_response->set_content_type("text/html");
    http_response->AddCustomHeader("WWW-Authenticate",
                                   "Basic realm=\"" + realm + "\"");
    if (query.find("set-cookie-if-challenged") != query.end())
      http_response->AddCustomHeader("Set-Cookie", "got_challenged=true");
    http_response->set_content(base::StringPrintf(
        "<html><head><title>Denied: %s</title></head>"
        "<body>auth=%s<p>b64str=%s<p>username: %s<p>userpass: %s<p>"
        "password: %s<p>You sent:<br>%s<p></body></html>",
        error.c_str(), auth.c_str(), b64str.c_str(), username.c_str(),
        userpass.c_str(), password.c_str(), request.all_headers.c_str()));
    return std::move(http_response);
  }

  if (request.headers.find("If-None-Match") != request.headers.end() &&
      request.headers.at("If-None-Match") == kEtag) {
    http_response->set_code(HTTP_NOT_MODIFIED);
    return std::move(http_response);
  }

  base::FilePath file_path =
      base::FilePath().AppendASCII(request.relative_url.substr(1));
  if (file_path.FinalExtension() == FILE_PATH_LITERAL("gif")) {
    base::FilePath server_root;
    PathService::Get(base::DIR_SOURCE_ROOT, &server_root);
    base::FilePath gif_path = server_root.AppendASCII(kLogoPath);
    std::string gif_data;
    base::ReadFileToString(gif_path, &gif_data);
    http_response->set_content_type("image/gif");
    http_response->set_content(gif_data);
  } else {
    http_response->set_content_type("text/html");
    http_response->set_content(
        base::StringPrintf("<html><head><title>%s/%s</title></head>"
                           "<body>auth=%s<p>You sent:<br>%s<p></body></html>",
                           username.c_str(), password.c_str(), auth.c_str(),
                           request.all_headers.c_str()));
  }

  http_response->AddCustomHeader("Cache-Control", "max-age=60000");
  http_response->AddCustomHeader("Etag", kEtag);
  return std::move(http_response);
}

// /auth-digest
// Performs "Digest" HTTP authentication.
std::unique_ptr<HttpResponse> HandleAuthDigest(const HttpRequest& request) {
  std::string nonce = base::MD5String(
      base::StringPrintf("privatekey%s", request.relative_url.c_str()));
  std::string opaque = base::MD5String("opaque");
  std::string password = kDefaultPassword;
  std::string realm = kDefaultRealm;

  bool authed = false;
  std::string error;
  std::string auth;
  std::string digest_str = "Digest";
  std::string username;
  if (request.headers.find("Authorization") == request.headers.end()) {
    error = "no auth";
  } else if (request.headers.at("Authorization").substr(0, digest_str.size()) !=
             digest_str) {
    error = "not digest";
  } else {
    auth = request.headers.at("Authorization").substr(digest_str.size() + 1);

    std::map<std::string, std::string> auth_pairs;
    base::StringPairs auth_vector;
    base::SplitStringIntoKeyValuePairs(auth, '=', ',', &auth_vector);
    for (const auto& auth_pair : auth_vector) {
      std::string key;
      std::string value;
      base::TrimWhitespaceASCII(auth_pair.first, base::TRIM_ALL, &key);
      base::TrimWhitespaceASCII(auth_pair.second, base::TRIM_ALL, &value);
      if (value.size() > 2 && value.at(0) == '"' &&
          value.at(value.size() - 1) == '"') {
        value = value.substr(1, value.size() - 2);
      }
      auth_pairs[key] = value;
    }

    if (auth_pairs["nonce"] != nonce) {
      error = "wrong nonce";
    } else if (auth_pairs["opaque"] != opaque) {
      error = "wrong opaque";
    } else {
      username = auth_pairs["username"];

      std::string hash1 = base::MD5String(
          base::StringPrintf("%s:%s:%s", auth_pairs["username"].c_str(),
                             realm.c_str(), password.c_str()));
      std::string hash2 = base::MD5String(base::StringPrintf(
          "%s:%s", request.method_string.c_str(), auth_pairs["uri"].c_str()));

      std::string response;
      if (auth_pairs.find("qop") != auth_pairs.end() &&
          auth_pairs.find("nc") != auth_pairs.end() &&
          auth_pairs.find("cnonce") != auth_pairs.end()) {
        response = base::MD5String(base::StringPrintf(
            "%s:%s:%s:%s:%s:%s", hash1.c_str(), nonce.c_str(),
            auth_pairs["nc"].c_str(), auth_pairs["cnonce"].c_str(),
            auth_pairs["qop"].c_str(), hash2.c_str()));
      } else {
        response = base::MD5String(base::StringPrintf(
            "%s:%s:%s", hash1.c_str(), nonce.c_str(), hash2.c_str()));
      }

      if (auth_pairs["response"] == response)
        authed = true;
      else
        error = "wrong password";
    }
  }

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  if (!authed) {
    http_response->set_code(HTTP_UNAUTHORIZED);
    http_response->set_content_type("text/html");
    std::string auth_header = base::StringPrintf(
        "Digest realm=\"%s\", "
        "domain=\"/\", qop=\"auth\", algorithm=MD5, nonce=\"%s\", "
        "opaque=\"%s\"",
        realm.c_str(), nonce.c_str(), opaque.c_str());
    http_response->AddCustomHeader("WWW-Authenticate", auth_header);
    http_response->set_content(base::StringPrintf(
        "<html><head><title>Denied: %s</title></head>"
        "<body>auth=%s<p>"
        "You sent:<br>%s<p>We are replying:<br>%s<p></body></html>",
        error.c_str(), auth.c_str(), request.all_headers.c_str(),
        auth_header.c_str()));
    return std::move(http_response);
  }

  http_response->set_content_type("text/html");
  http_response->set_content(
      base::StringPrintf("<html><head><title>%s/%s</title></head>"
                         "<body>auth=%s<p></body></html>",
                         username.c_str(), password.c_str(), auth.c_str()));

  return std::move(http_response);
}

// /server-redirect?URL
// Returns a server-redirect (301) to URL.
std::unique_ptr<HttpResponse> HandleServerRedirect(const HttpRequest& request) {
  GURL request_url = request.GetURL();
  std::string dest =
      net::UnescapeURLComponent(request_url.query(), kUnescapeAll);

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_code(HTTP_MOVED_PERMANENTLY);
  http_response->AddCustomHeader("Location", dest);
  http_response->set_content_type("text/html");
  http_response->set_content(base::StringPrintf(
      "<html><head></head><body>Redirecting to %s</body></html>",
      dest.c_str()));
  return std::move(http_response);
}

// /cross-site?URL
// Returns a cross-site redirect to URL.
std::unique_ptr<HttpResponse> HandleCrossSiteRedirect(
    EmbeddedTestServer* server,
    const HttpRequest& request) {
  if (!ShouldHandle(request, "/cross-site"))
    return nullptr;

  std::string dest_all = net::UnescapeURLComponent(
      request.relative_url.substr(std::string("/cross-site").size() + 1),
      kUnescapeAll);

  std::string dest;
  size_t delimiter = dest_all.find("/");
  if (delimiter != std::string::npos) {
    dest = base::StringPrintf(
        "//%s:%hu/%s", dest_all.substr(0, delimiter).c_str(), server->port(),
        dest_all.substr(delimiter + 1).c_str());
  }

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_code(HTTP_MOVED_PERMANENTLY);
  http_response->AddCustomHeader("Location", dest);
  http_response->set_content_type("text/html");
  http_response->set_content(base::StringPrintf(
      "<html><head></head><body>Redirecting to %s</body></html>",
      dest.c_str()));
  return std::move(http_response);
}

// /client-redirect?URL
// Returns a meta redirect to URL.
std::unique_ptr<HttpResponse> HandleClientRedirect(const HttpRequest& request) {
  GURL request_url = request.GetURL();
  std::string dest =
      net::UnescapeURLComponent(request_url.query(), kUnescapeAll);

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_content_type("text/html");
  http_response->set_content(base::StringPrintf(
      "<html><head><meta http-equiv=\"refresh\" content=\"0;url=%s\"></head>"
      "<body>Redirecting to %s</body></html>",
      dest.c_str(), dest.c_str()));
  return std::move(http_response);
}

// /defaultresponse
// Returns a valid 200 response.
std::unique_ptr<HttpResponse> HandleDefaultResponse(
    const HttpRequest& request) {
  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_content_type("text/html");
  http_response->set_content("Default response given for path: " +
                             request.relative_url);
  return std::move(http_response);
}

// Delays |delay| seconds before sending a response to the client.
class DelayedHttpResponse : public BasicHttpResponse {
 public:
  explicit DelayedHttpResponse(double delay) : delay_(delay) {}

  void SendResponse(const SendBytesCallback& send,
                    const SendCompleteCallback& done) override {
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, base::Bind(send, ToResponseString(), done),
        base::TimeDelta::FromSecondsD(delay_));
  }

 private:
  const double delay_;

  DISALLOW_COPY_AND_ASSIGN(DelayedHttpResponse);
};

// /slow?N
// Returns a response to the server delayed by N seconds.
std::unique_ptr<HttpResponse> HandleSlowServer(const HttpRequest& request) {
  double delay = 1.0f;

  GURL request_url = request.GetURL();
  if (request_url.has_query())
    delay = std::atof(request_url.query().c_str());

  std::unique_ptr<BasicHttpResponse> http_response(
      new DelayedHttpResponse(delay));
  http_response->set_content_type("text/plain");
  http_response->set_content(base::StringPrintf("waited %.1f seconds", delay));
  return std::move(http_response);
}

// Never returns a response.
class HungHttpResponse : public HttpResponse {
 public:
  HungHttpResponse() {}

  void SendResponse(const SendBytesCallback& send,
                    const SendCompleteCallback& done) override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(HungHttpResponse);
};

// /hung
// Never returns a response.
std::unique_ptr<HttpResponse> HandleHungResponse(const HttpRequest& request) {
  return std::make_unique<HungHttpResponse>();
}

// Return headers, then hangs.
class HungAfterHeadersHttpResponse : public HttpResponse {
 public:
  HungAfterHeadersHttpResponse() {}

  void SendResponse(const SendBytesCallback& send,
                    const SendCompleteCallback& done) override {
    send.Run("HTTP/1.1 OK\r\n\r\n", base::Bind(&base::DoNothing));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(HungAfterHeadersHttpResponse);
};

// /hung-after-headers
// Never returns a response.
std::unique_ptr<HttpResponse> HandleHungAfterHeadersResponse(
    const HttpRequest& request) {
  return std::make_unique<HungAfterHeadersHttpResponse>();
}

// /gzip-body?<body>
// Returns a response with a gzipped body of "<body>". Attempts to allocate
// enough memory to contain the body, but DCHECKs if that fails.
std::unique_ptr<HttpResponse> HandleGzipBody(const HttpRequest& request) {
  std::string uncompressed_body = request.GetURL().query();
  // Attempt to pick size that's large enough even in the worst case (deflate
  // block headers should be shorter than 512 bytes, and deflating should never
  // double size of data, modulo headers).
  // TODO(mmenke): This is rather awkward. Worth improving CompressGzip?
  std::vector<char> compressed_body(uncompressed_body.size() * 2 + 512);
  size_t compressed_size = compressed_body.size();
  CompressGzip(uncompressed_body.c_str(), uncompressed_body.size(),
               compressed_body.data(), &compressed_size,
               true /* gzip_framing */);
  // CompressGzip should DCHECK itself if this fails, anyways.
  DCHECK_GE(compressed_body.size(), compressed_size);

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->set_content(
      std::string(compressed_body.data(), compressed_size));
  http_response->AddCustomHeader("Content-Encoding", "gzip");
  return std::move(http_response);
}

}  // anonymous namespace

#define PREFIXED_HANDLER(prefix, handler) \
  base::Bind(&HandlePrefixedRequest, prefix, base::Bind(handler))

void RegisterDefaultHandlers(EmbeddedTestServer* server) {
  server->RegisterDefaultHandler(base::Bind(&HandleDefaultConnect));

  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/cachetime", &HandleCacheTime));
  server->RegisterDefaultHandler(
      base::Bind(&HandleEchoHeader, "/echoheader", "no-cache"));
  server->RegisterDefaultHandler(
      base::Bind(&HandleEchoHeader, "/echoheadercache", "max-age=60000"));
  server->RegisterDefaultHandler(PREFIXED_HANDLER("/echo", &HandleEcho));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/echotitle", &HandleEchoTitle));
  server->RegisterDefaultHandler(PREFIXED_HANDLER("/echoall", &HandleEchoAll));
  server->RegisterDefaultHandler(PREFIXED_HANDLER("/echo-raw", &HandleEchoRaw));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/set-cookie", &HandleSetCookie));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/set-many-cookies", &HandleSetManyCookies));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/expect-and-set-cookie", &HandleExpectAndSetCookie));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/set-header", &HandleSetHeader));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/nocontent", &HandleNoContent));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/close-socket", &HandleCloseSocket));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/auth-basic", &HandleAuthBasic));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/auth-digest", &HandleAuthDigest));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/server-redirect", &HandleServerRedirect));
  server->RegisterDefaultHandler(base::Bind(&HandleCrossSiteRedirect, server));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/client-redirect", &HandleClientRedirect));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/defaultresponse", &HandleDefaultResponse));
  server->RegisterDefaultHandler(PREFIXED_HANDLER("/slow", &HandleSlowServer));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/hung", &HandleHungResponse));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/hung-after-headers", &HandleHungAfterHeadersResponse));
  server->RegisterDefaultHandler(
      PREFIXED_HANDLER("/gzip-body", &HandleGzipBody));

  // TODO(svaldez): HandleDownload
  // TODO(svaldez): HandleDownloadFinish
  // TODO(svaldez): HandleZipFile
  // TODO(svaldez): HandleSSLManySmallRecords
  // TODO(svaldez): HandleChunkedServer
  // TODO(svaldez): HandleGetSSLSessionCache
  // TODO(svaldez): HandleGetChannelID
  // TODO(svaldez): HandleGetClientCert
  // TODO(svaldez): HandleClientCipherList
  // TODO(svaldez): HandleEchoMultipartPost
}

#undef PREFIXED_HANDLER

}  // namespace test_server
}  // namespace net
