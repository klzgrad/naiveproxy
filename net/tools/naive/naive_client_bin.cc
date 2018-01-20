// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <limits>
#include <memory>
#include <string>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/sys_info.h"
#include "base/task_scheduler/task_scheduler.h"
#include "base/values.h"
#include "build/build_config.h"
#include "net/base/auth.h"
#include "net/http/http_auth.h"
#include "net/http/http_auth_cache.h"
#include "net/http/http_network_session.h"
#include "net/http/http_transaction_factory.h"
#include "net/log/file_net_log_observer.h"
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_entry.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_util.h"
#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_config_service_fixed.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/tcp_server_socket.h"
#include "net/tools/naive/naive_client.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#endif

namespace {

constexpr int kListenBackLog = 512;
constexpr int kDefaultMaxSocketsPerPool = 256;
constexpr int kDefaultMaxSocketsPerGroup = 255;
constexpr int kExpectedMaxUsers = 8;

std::unique_ptr<base::Value> GetConstants(
    const base::CommandLine::StringType& command_line_string) {
  auto constants_dict = net::GetNetConstants();
  DCHECK(constants_dict);

  // Add a dictionary with the version of the client and its command line
  // arguments.
  auto dict = std::make_unique<base::DictionaryValue>();

  // We have everything we need to send the right values.
  std::string os_type = base::StringPrintf(
      "%s: %s (%s)", base::SysInfo::OperatingSystemName().c_str(),
      base::SysInfo::OperatingSystemVersion().c_str(),
      base::SysInfo::OperatingSystemArchitecture().c_str());
  dict->SetString("os_type", os_type);
  dict->SetString("command_line", command_line_string);

  constants_dict->Set("clientInfo", std::move(dict));

  return std::move(constants_dict);
}

// Builds a URLRequestContext assuming there's only a single loop.
std::unique_ptr<net::URLRequestContext> BuildURLRequestContext(
    const std::string& proxy_url,
    const std::string& proxy_user,
    const std::string& proxy_pass,
    net::NetLog* net_log) {
  net::URLRequestContextBuilder builder;

  net::ProxyConfig proxy_config;
  proxy_config.proxy_rules().ParseFromString(proxy_url);
  auto proxy_service = net::ProxyService::CreateWithoutProxyResolver(
      std::make_unique<net::ProxyConfigServiceFixed>(proxy_config), net_log);
  proxy_service->ForceReloadProxyConfig();

  builder.set_proxy_service(std::move(proxy_service));
  builder.DisableHttpCache();
  builder.set_net_log(net_log);

  auto context = builder.Build();

  net::HttpNetworkSession* session =
      context->http_transaction_factory()->GetSession();
  net::HttpAuthCache* auth_cache = session->http_auth_cache();
  GURL auth_origin(proxy_url);
  net::AuthCredentials credentials(base::ASCIIToUTF16(proxy_user),
                                   base::ASCIIToUTF16(proxy_pass));
  auth_cache->Add(auth_origin, /*realm=*/std::string(),
                  net::HttpAuth::AUTH_SCHEME_BASIC, /*challenge=*/"Basic",
                  credentials, /*path=*/"/");

  return context;
}

struct Params {
  std::string listen_addr;
  int listen_port;
  std::string proxy_url;
  std::string proxy_user;
  std::string proxy_pass;
  logging::LoggingSettings log_settings;
  base::FilePath net_log_path;
  base::FilePath ssl_key_path;
};

bool ParseCommandLineFlags(Params* params) {
  const base::CommandLine& line = *base::CommandLine::ForCurrentProcess();

  if (line.HasSwitch("h") || line.HasSwitch("help")) {
    LOG(INFO) << "Usage: naive_client [options]\n"
                 "\n"
                 "Options:\n"
                 "-h, --help                  Show this help message and exit\n"
                 "--addr=<address>            Address to listen on\n"
                 "--port=<port>               Port to listen on\n"
                 "--proxy=https://<user>:<pass>@<domain>[:port]\n"
                 "                            Proxy specification\n"
                 "--log                       Log to stderr, otherwise no log\n"
                 "--log-net-log=<path>        Save NetLog\n"
                 "--ssl-key-log-file=<path>   Save SSL keys for Wireshark\n";
    exit(EXIT_SUCCESS);
    return false;
  }

  if (!line.HasSwitch("addr")) {
    LOG(ERROR) << "Missing --addr";
    return false;
  }
  params->listen_addr = line.GetSwitchValueASCII("addr");
  if (params->listen_addr.empty()) {
    LOG(ERROR) << "Invalid --port";
    return false;
  }

  if (!line.HasSwitch("port")) {
    LOG(ERROR) << "Missing --port";
    return false;
  }
  if (!base::StringToInt(line.GetSwitchValueASCII("port"),
                         &params->listen_port)) {
    LOG(ERROR) << "Invalid --port";
    return false;
  }
  if (params->listen_port <= 0 ||
      params->listen_port > std::numeric_limits<uint16_t>::max()) {
    LOG(ERROR) << "Invalid --port";
    return false;
  }

  if (!line.HasSwitch("proxy")) {
    LOG(ERROR) << "Missing --proxy";
    return false;
  }
  GURL url(line.GetSwitchValueASCII("proxy"));
  if (!url.is_valid()) {
    LOG(ERROR) << "Invalid proxy URL";
    return false;
  }
  if (url.scheme() != "https") {
    LOG(ERROR) << "Must be HTTPS proxy";
    return false;
  }
  if (url.username().empty() || url.password().empty()) {
    LOG(ERROR) << "Missing user or pass";
    return false;
  }
  params->proxy_url = url::SchemeHostPort(url).Serialize();
  params->proxy_user = url.username();
  params->proxy_pass = url.password();

  if (line.HasSwitch("log")) {
    params->log_settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  } else {
    params->log_settings.logging_dest = logging::LOG_NONE;
  }

  if (line.HasSwitch("log-net-log")) {
    params->net_log_path = line.GetSwitchValuePath("log-net-log");
  }

  if (line.HasSwitch("ssl-key-log-file")) {
    params->ssl_key_path = line.GetSwitchValuePath("ssl-key-log-file");
  }

  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  base::TaskScheduler::CreateAndStartWithDefaultParams("");
  base::AtExitManager exit_manager;
  base::MessageLoopForIO main_loop;

#if defined(OS_MACOSX)
  base::mac::ScopedNSAutoreleasePool pool;
#endif

  base::CommandLine::Init(argc, argv);

  Params params;
  if (!ParseCommandLineFlags(&params)) {
    return EXIT_FAILURE;
  }

  net::ClientSocketPoolManager::set_max_sockets_per_pool(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerPool * kExpectedMaxUsers);
  net::ClientSocketPoolManager::set_max_sockets_per_group(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerGroup * kExpectedMaxUsers);
  net::ClientSocketPoolManager::set_max_sockets_per_proxy_server(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerPool * kExpectedMaxUsers);

  CHECK(logging::InitLogging(params.log_settings));

  if (!params.ssl_key_path.empty()) {
    net::SSLClientSocket::SetSSLKeyLogFile(params.ssl_key_path);
  }

  // The declaration order for net_log and printing_log_observer is
  // important. The destructor of PrintingLogObserver removes itself
  // from net_log, so net_log must be available for entire lifetime of
  // printing_log_observer.
  net::NetLog net_log;
  std::unique_ptr<net::FileNetLogObserver> observer;
  if (!params.net_log_path.empty()) {
    const auto& cmdline =
        base::CommandLine::ForCurrentProcess()->GetCommandLineString();
    observer = net::FileNetLogObserver::CreateUnbounded(params.net_log_path,
                                                        GetConstants(cmdline));
    observer->StartObserving(&net_log, net::NetLogCaptureMode::Default());
  }

  auto context = BuildURLRequestContext(params.proxy_url, params.proxy_user,
                                        params.proxy_pass, &net_log);

  auto server_socket =
      std::make_unique<net::TCPServerSocket>(&net_log, net::NetLogSource());

  int result = server_socket->ListenWithAddressAndPort(
      params.listen_addr, params.listen_port, kListenBackLog);
  if (result != net::OK) {
    LOG(ERROR) << "Failed to listen: " << result;
    return EXIT_FAILURE;
  }

  net::NaiveClient naive_client(
      std::move(server_socket),
      context->http_transaction_factory()->GetSession());

  base::RunLoop().Run();

  return EXIT_SUCCESS;
}
