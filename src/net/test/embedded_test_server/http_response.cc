// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/test/embedded_test_server/http_response.h"

#include <utility>

#include "base/bind.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "net/http/http_status_code.h"

namespace net {
namespace test_server {

HttpResponse::~HttpResponse() = default;

RawHttpResponse::RawHttpResponse(const std::string& headers,
                                 const std::string& contents)
    : headers_(headers), contents_(contents) {}

RawHttpResponse::~RawHttpResponse() = default;

void RawHttpResponse::SendResponse(const SendBytesCallback& send,
                                   SendCompleteCallback done) {
  std::string response;
  if (!headers_.empty()) {
    response = headers_;
    // LocateEndOfHeadersHelper() searches for the first "\n\n" and "\n\r\n" as
    // the end of the header.
    std::size_t index = response.find_last_not_of("\r\n");
    if (index != std::string::npos)
      response.erase(index + 1);
    response += "\n\n";
    response += contents_;
  } else {
    response = contents_;
  }
  send.Run(response, std::move(done));
}

void RawHttpResponse::AddHeader(const std::string& key_value_pair) {
  headers_.append(base::StringPrintf("%s\r\n", key_value_pair.c_str()));
}

BasicHttpResponse::BasicHttpResponse() : code_(HTTP_OK) {
}

BasicHttpResponse::~BasicHttpResponse() = default;

std::string BasicHttpResponse::ToResponseString() const {
  // Response line with headers.
  std::string response_builder;

  std::string http_reason_phrase(GetHttpReasonPhrase(code_));

  // TODO(mtomasz): For http/1.0 requests, send http/1.0.
  base::StringAppendF(&response_builder,
                      "HTTP/1.1 %d %s\r\n",
                      code_,
                      http_reason_phrase.c_str());
  base::StringAppendF(&response_builder, "Connection: close\r\n");

  base::StringAppendF(&response_builder, "Content-Length: %" PRIuS "\r\n",
                      content_.size());
  base::StringAppendF(&response_builder, "Content-Type: %s\r\n",
                      content_type_.c_str());
  for (size_t i = 0; i < custom_headers_.size(); ++i) {
    const std::string& header_name = custom_headers_[i].first;
    const std::string& header_value = custom_headers_[i].second;
    DCHECK(header_value.find_first_of("\n\r") == std::string::npos) <<
        "Malformed header value.";
    base::StringAppendF(&response_builder,
                        "%s: %s\r\n",
                        header_name.c_str(),
                        header_value.c_str());
  }
  base::StringAppendF(&response_builder, "\r\n");

  return response_builder + content_;
}

void BasicHttpResponse::SendResponse(const SendBytesCallback& send,
                                     SendCompleteCallback done) {
  send.Run(ToResponseString(), std::move(done));
}

DelayedHttpResponse::DelayedHttpResponse(const base::TimeDelta delay)
    : delay_(delay) {}

DelayedHttpResponse::~DelayedHttpResponse() = default;

void DelayedHttpResponse::SendResponse(const SendBytesCallback& send,
                                       SendCompleteCallback done) {
  base::SequencedTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::BindOnce(send, ToResponseString(), std::move(done)),
      delay_);
}

void HungResponse::SendResponse(const SendBytesCallback& send,
                                SendCompleteCallback done) {}

}  // namespace test_server
}  // namespace net
