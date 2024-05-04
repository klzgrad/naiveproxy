// Copyright 2024 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_TOOLS_NAIVE_NAIVE_CONFIG_H_
#define NET_TOOLS_NAIVE_NAIVE_CONFIG_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/values.h"
#include "net/base/ip_address.h"
#include "net/http/http_request_headers.h"
#include "net/tools/naive/naive_protocol.h"

namespace net {

struct NaiveListenConfig {
  ClientProtocol protocol = ClientProtocol::kSocks5;
  std::string user;
  std::string pass;
  std::string addr = "0.0.0.0";
  int port = 1080;

  NaiveListenConfig();
  NaiveListenConfig(const NaiveListenConfig&);
  ~NaiveListenConfig();
  bool Parse(const std::string& str);
};

struct NaiveConfig {
  std::vector<NaiveListenConfig> listen = {NaiveListenConfig()};

  int insecure_concurrency = 1;

  HttpRequestHeaders extra_headers;

  std::string proxy_url = "direct://";

  std::u16string proxy_user;
  std::u16string proxy_pass;

  std::string host_resolver_rules;

  IPAddress resolver_range = {100, 64, 0, 0};
  size_t resolver_prefix = 10;

  logging::LoggingSettings log = {.logging_dest = logging::LOG_NONE};
  base::FilePath log_file;

  base::FilePath log_net_log;

  base::FilePath ssl_key_log_file;

  NaiveConfig();
  NaiveConfig(const NaiveConfig&);
  ~NaiveConfig();
  bool Parse(const base::Value::Dict& value);
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_CONFIG_H_
