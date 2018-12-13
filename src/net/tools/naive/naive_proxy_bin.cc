// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/system/sys_info.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/version_info/version_info.h"
#include "net/base/auth.h"
#include "net/base/network_isolation_key.h"
#include "net/base/url_util.h"
#include "net/dns/host_resolver.h"
#include "net/dns/mapped_host_resolver.h"
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
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/tcp_server_socket.h"
#include "net/ssl/ssl_key_logger_impl.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/tools/naive/naive_proxy.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"
#include "url/url_util.h"

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#endif

namespace {

constexpr int kListenBackLog = 512;
constexpr int kDefaultMaxSocketsPerPool = 256;
constexpr int kDefaultMaxSocketsPerGroup = 255;
constexpr int kExpectedMaxUsers = 8;
constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("naive", "");

struct Params {
  net::NaiveConnection::Protocol protocol;
  std::string listen_addr;
  int listen_port;
  bool use_padding;
  std::string proxy_url;
  std::u16string proxy_user;
  std::u16string proxy_pass;
  std::string host_resolver_rules;
  logging::LoggingSettings log_settings;
  base::FilePath log_path;
  base::FilePath net_log_path;
  base::FilePath ssl_key_path;
};

std::unique_ptr<base::Value> GetConstants() {
  auto constants_dict = std::make_unique<base::Value>(net::GetNetConstants());
  base::DictionaryValue dict;
  std::string os_type = base::StringPrintf(
      "%s: %s (%s)", base::SysInfo::OperatingSystemName().c_str(),
      base::SysInfo::OperatingSystemVersion().c_str(),
      base::SysInfo::OperatingSystemArchitecture().c_str());
  dict.SetStringPath("os_type", os_type);
  constants_dict->SetKey("clientInfo", std::move(dict));
  return constants_dict;
}

// Builds a URLRequestContext assuming there's only a single loop.
std::unique_ptr<net::URLRequestContext> BuildURLRequestContext(
    const Params& params,
    net::NetLog* net_log) {
  net::URLRequestContextBuilder builder;

  builder.DisableHttpCache();
  builder.set_net_log(net_log);

  net::ProxyConfig proxy_config;
  proxy_config.proxy_rules().ParseFromString(params.proxy_url);
  auto proxy_service =
      net::ConfiguredProxyResolutionService::CreateWithoutProxyResolver(
          std::make_unique<net::ProxyConfigServiceFixed>(
              net::ProxyConfigWithAnnotation(proxy_config, kTrafficAnnotation)),
          net_log);
  proxy_service->ForceReloadProxyConfig();
  builder.set_proxy_resolution_service(std::move(proxy_service));

  if (!params.host_resolver_rules.empty()) {
    builder.set_host_mapping_rules(params.host_resolver_rules);
  }

  auto context = builder.Build();

  if (!params.proxy_url.empty() && !params.proxy_user.empty() &&
      !params.proxy_pass.empty()) {
    auto* session = context->http_transaction_factory()->GetSession();
    auto* auth_cache = session->http_auth_cache();
    std::string proxy_url = params.proxy_url;
    GURL proxy_gurl(proxy_url);
    if (proxy_url.compare(0, 7, "quic://") == 0) {
      proxy_url.replace(0, 4, "https");
      auto* quic = context->quic_context()->params();
      quic->supported_versions = {quic::ParsedQuicVersion::RFCv1()};
      quic->origins_to_force_quic_on.insert(
          net::HostPortPair::FromURL(proxy_gurl));
    }
    url::SchemeHostPort auth_origin(proxy_gurl);
    net::AuthCredentials credentials(params.proxy_user, params.proxy_pass);
    auth_cache->Add(auth_origin, net::HttpAuth::AUTH_PROXY,
                    /*realm=*/std::string(), net::HttpAuth::AUTH_SCHEME_BASIC,
                    net::NetworkIsolationKey(), /*challenge=*/"Basic",
                    credentials, /*path=*/"/");
  }

  return context;
}

bool ParseCommandLineFlags(Params* params) {
  const base::CommandLine& line = *base::CommandLine::ForCurrentProcess();

  if (line.HasSwitch("h") || line.HasSwitch("help")) {
    std::cout << "Usage: naive [options]\n"
                 "\n"
                 "Options:\n"
                 "-h, --help                 Show this message\n"
                 "--version                  Print version\n"
                 "--listen=<proto>://[addr][:port]\n"
                 "                           proto: socks, http\n"
                 "--proxy=<proto>://[<user>:<pass>@]<hostname>[:<port>]\n"
                 "                           proto: https, quic\n"
                 "--padding                  Use padding\n"
                 "--log[=<path>]             Log to stderr, or file\n"
                 "--log-net-log=<path>       Save NetLog\n"
                 "--ssl-key-log-file=<path>  Save SSL keys for Wireshark\n"
              << std::endl;
    exit(EXIT_SUCCESS);
    return false;
  }

  if (line.HasSwitch("version")) {
    std::cout << "Version: " << version_info::GetVersionNumber() << std::endl;
    exit(EXIT_SUCCESS);
    return false;
  }

  params->protocol = net::NaiveConnection::kSocks5;
  params->listen_addr = "0.0.0.0";
  params->listen_port = 1080;
  url::AddStandardScheme("socks", url::SCHEME_WITH_HOST_AND_PORT);
  if (line.HasSwitch("listen")) {
    GURL url(line.GetSwitchValueASCII("listen"));
    if (url.scheme() == "socks") {
      params->protocol = net::NaiveConnection::kSocks5;
      params->listen_port = 1080;
    } else if (url.scheme() == "http") {
      params->protocol = net::NaiveConnection::kHttp;
      params->listen_port = 8080;
    } else {
      LOG(ERROR) << "Invalid scheme in --listen";
      return false;
    }
    if (!url.host().empty()) {
      params->listen_addr = url.host();
    }
    if (!url.port().empty()) {
      if (!base::StringToInt(url.port(), &params->listen_port)) {
        LOG(ERROR) << "Invalid port in --listen";
        return false;
      }
      if (params->listen_port <= 0 ||
          params->listen_port > std::numeric_limits<uint16_t>::max()) {
        LOG(ERROR) << "Invalid port in --listen";
        return false;
      }
    }
  }

  url::AddStandardScheme("quic",
                         url::SCHEME_WITH_HOST_PORT_AND_USER_INFORMATION);
  params->proxy_url = "direct://";
  GURL url(line.GetSwitchValueASCII("proxy"));
  if (line.HasSwitch("proxy")) {
    if (!url.is_valid()) {
      LOG(ERROR) << "Invalid proxy URL";
      return false;
    }
    if (url.scheme() != "https" && url.scheme() != "quic") {
      LOG(ERROR) << "Must be HTTPS or QUIC proxy";
      return false;
    }
    params->proxy_url = url::SchemeHostPort(url).Serialize();
    net::GetIdentityFromURL(url, &params->proxy_user, &params->proxy_pass);
  }

  params->use_padding = false;
  if (line.HasSwitch("padding")) {
    params->use_padding = true;
  }

  if (line.HasSwitch("host-resolver-rules")) {
    params->host_resolver_rules =
        line.GetSwitchValueASCII("host-resolver-rules");
  }

  if (line.HasSwitch("log")) {
    params->log_settings.logging_dest = logging::LOG_DEFAULT;
    params->log_path = line.GetSwitchValuePath("log");
    if (!params->log_path.empty()) {
      params->log_settings.logging_dest = logging::LOG_TO_FILE;
    } else if (params->log_settings.logging_dest == logging::LOG_TO_FILE) {
      params->log_path = base::FilePath::FromUTF8Unsafe("naive.log");
    }
    params->log_settings.log_file_path = params->log_path.value().c_str();
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

namespace net {
namespace {
// NetLog::ThreadSafeObserver implementation that simply prints events
// to the logs.
class PrintingLogObserver : public NetLog::ThreadSafeObserver {
 public:
  PrintingLogObserver() = default;
  PrintingLogObserver(const PrintingLogObserver&) = delete;
  PrintingLogObserver& operator=(const PrintingLogObserver&) = delete;

  ~PrintingLogObserver() override {
    // This is guaranteed to be safe as this program is single threaded.
    net_log()->RemoveObserver(this);
  }

  // NetLog::ThreadSafeObserver implementation:
  void OnAddEntry(const NetLogEntry& entry) override {
    switch (entry.type) {
      case NetLogEventType::SOCKET_POOL_STALLED_MAX_SOCKETS:
      case NetLogEventType::SOCKET_POOL_STALLED_MAX_SOCKETS_PER_GROUP:
      case NetLogEventType::HTTP2_SESSION_STREAM_STALLED_BY_SESSION_SEND_WINDOW:
      case NetLogEventType::HTTP2_SESSION_STREAM_STALLED_BY_STREAM_SEND_WINDOW:
      case NetLogEventType::HTTP2_SESSION_STALLED_MAX_STREAMS:
      case NetLogEventType::HTTP2_STREAM_FLOW_CONTROL_UNSTALLED:
        break;
      default:
        return;
    }
    const char* source_type = NetLog::SourceTypeToString(entry.source.type);
    const char* event_type = NetLog::EventTypeToString(entry.type);
    const char* event_phase = NetLog::EventPhaseToString(entry.phase);
    base::Value params(entry.ToValue());
    std::string params_str;
    base::JSONWriter::Write(params, &params_str);
    params_str.insert(0, ": ");

    VLOG(1) << source_type << "(" << entry.source.id << "): " << event_type
            << ": " << event_phase << params_str;
  }
};

}  // namespace
}  // namespace net

int main(int argc, char* argv[]) {
  base::SingleThreadTaskExecutor io_task_executor(base::MessagePumpType::IO);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("naive");
  base::AtExitManager exit_manager;

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
  net::ClientSocketPoolManager::set_max_sockets_per_proxy_server(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerPool * kExpectedMaxUsers);
  net::ClientSocketPoolManager::set_max_sockets_per_group(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerGroup * kExpectedMaxUsers);

  CHECK(logging::InitLogging(params.log_settings));

  if (!params.ssl_key_path.empty()) {
    net::SSLClientSocket::SetSSLKeyLogger(
        std::make_unique<net::SSLKeyLoggerImpl>(params.ssl_key_path));
  }

  // The declaration order for net_log and printing_log_observer is
  // important. The destructor of PrintingLogObserver removes itself
  // from net_log, so net_log must be available for entire lifetime of
  // printing_log_observer.
  net::NetLog* net_log = net::NetLog::Get();
  std::unique_ptr<net::FileNetLogObserver> observer;
  if (!params.net_log_path.empty()) {
    observer = net::FileNetLogObserver::CreateUnbounded(
        params.net_log_path, net::NetLogCaptureMode::kDefault, GetConstants());
    observer->StartObserving(net_log);
  }

  // Avoids net log overhead if verbose logging is disabled.
  std::unique_ptr<net::PrintingLogObserver> printing_log_observer;
  if (params.log_settings.logging_dest != logging::LOG_NONE && VLOG_IS_ON(1)) {
    printing_log_observer = std::make_unique<net::PrintingLogObserver>();
    net_log->AddObserver(printing_log_observer.get(),
                         net::NetLogCaptureMode::kDefault);
  }

  auto context = BuildURLRequestContext(params, net_log);
  auto* session = context->http_transaction_factory()->GetSession();

  auto listen_socket =
      std::make_unique<net::TCPServerSocket>(net_log, net::NetLogSource());

  int result = listen_socket->ListenWithAddressAndPort(
      params.listen_addr, params.listen_port, kListenBackLog);
  if (result != net::OK) {
    LOG(ERROR) << "Failed to listen: " << result;
    return EXIT_FAILURE;
  }

  net::NaiveProxy naive_proxy(std::move(listen_socket), params.protocol,
                              params.use_padding, session, kTrafficAnnotation);

  base::RunLoop().Run();

  return EXIT_SUCCESS;
}
