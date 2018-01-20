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
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/json/json_file_value_serializer.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/run_loop.h"
#include "base/strings/escape.h"
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
#include "net/cert/cert_verifier.h"
#include "net/cert_net/cert_net_fetcher_url_request.h"
#include "net/dns/host_resolver.h"
#include "net/dns/mapped_host_resolver.h"
#include "net/http/http_auth.h"
#include "net/http/http_auth_cache.h"
#include "net/http/http_network_session.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_transaction_factory.h"
#include "net/log/file_net_log_observer.h"
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_entry.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_util.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/tcp_server_socket.h"
#include "net/socket/udp_server_socket.h"
#include "net/ssl/ssl_key_logger_impl.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_versions.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/tools/naive/naive_proxy.h"
#include "net/tools/naive/naive_proxy_delegate.h"
#include "net/tools/naive/partition_alloc_support.h"
#include "net/tools/naive/redirect_resolver.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"
#include "url/url_util.h"

#if BUILDFLAG(IS_APPLE)
#include "base/mac/scoped_nsautorelease_pool.h"
#endif

namespace {

constexpr int kListenBackLog = 512;
constexpr int kDefaultMaxSocketsPerPool = 256;
constexpr int kDefaultMaxSocketsPerGroup = 255;
constexpr int kExpectedMaxUsers = 8;
constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("naive", "");

struct CommandLine {
  std::string listen;
  std::string proxy;
  std::string concurrency;
  std::string extra_headers;
  std::string host_resolver_rules;
  std::string resolver_range;
  bool no_log;
  base::FilePath log;
  base::FilePath log_net_log;
  base::FilePath ssl_key_log_file;
};

struct Params {
  net::ClientProtocol protocol;
  std::string listen_user;
  std::string listen_pass;
  std::string listen_addr;
  int listen_port;
  int concurrency;
  net::HttpRequestHeaders extra_headers;
  std::string proxy_url;
  std::u16string proxy_user;
  std::u16string proxy_pass;
  std::string host_resolver_rules;
  net::IPAddress resolver_range;
  size_t resolver_prefix;
  logging::LoggingSettings log_settings;
  base::FilePath net_log_path;
  base::FilePath ssl_key_path;
};

std::unique_ptr<base::Value::Dict> GetConstants() {
  base::Value::Dict constants_dict = net::GetNetConstants();
  base::Value::Dict dict;
  std::string os_type = base::StringPrintf(
      "%s: %s (%s)", base::SysInfo::OperatingSystemName().c_str(),
      base::SysInfo::OperatingSystemVersion().c_str(),
      base::SysInfo::OperatingSystemArchitecture().c_str());
  dict.Set("os_type", os_type);
  constants_dict.Set("clientInfo", std::move(dict));
  return std::make_unique<base::Value::Dict>(std::move(constants_dict));
}

void GetCommandLine(const base::CommandLine& proc, CommandLine* cmdline) {
  if (proc.HasSwitch("h") || proc.HasSwitch("help")) {
    std::cout << "Usage: naive { OPTIONS | config.json }\n"
                 "\n"
                 "Options:\n"
                 "-h, --help                 Show this message\n"
                 "--version                  Print version\n"
                 "--listen=<proto>://[addr][:port]\n"
                 "                           proto: socks, http\n"
                 "                                  redir (Linux only)\n"
                 "--proxy=<proto>://[<user>:<pass>@]<hostname>[:<port>]\n"
                 "                           proto: https, quic\n"
                 "--insecure-concurrency=<N> Use N connections, insecure\n"
                 "--extra-headers=...        Extra headers split by CRLF\n"
                 "--host-resolver-rules=...  Resolver rules\n"
                 "--resolver-range=...       Redirect resolver range\n"
                 "--log[=<path>]             Log to stderr, or file\n"
                 "--log-net-log=<path>       Save NetLog\n"
                 "--ssl-key-log-file=<path>  Save SSL keys for Wireshark\n"
              << std::endl;
    exit(EXIT_SUCCESS);
  }

  if (proc.HasSwitch("version")) {
    std::cout << "naive " << version_info::GetVersionNumber() << std::endl;
    exit(EXIT_SUCCESS);
  }

  cmdline->listen = proc.GetSwitchValueASCII("listen");
  cmdline->proxy = proc.GetSwitchValueASCII("proxy");
  cmdline->concurrency = proc.GetSwitchValueASCII("insecure-concurrency");
  cmdline->extra_headers = proc.GetSwitchValueASCII("extra-headers");
  cmdline->host_resolver_rules =
      proc.GetSwitchValueASCII("host-resolver-rules");
  cmdline->resolver_range = proc.GetSwitchValueASCII("resolver-range");
  cmdline->no_log = !proc.HasSwitch("log");
  cmdline->log = proc.GetSwitchValuePath("log");
  cmdline->log_net_log = proc.GetSwitchValuePath("log-net-log");
  cmdline->ssl_key_log_file = proc.GetSwitchValuePath("ssl-key-log-file");
}

void GetCommandLineFromConfig(const base::FilePath& config_path,
                              CommandLine* cmdline) {
  JSONFileValueDeserializer reader(config_path);
  int error_code;
  std::string error_message;
  auto value = reader.Deserialize(&error_code, &error_message);
  if (value == nullptr) {
    std::cerr << "Error reading " << config_path << ": (" << error_code << ") "
              << error_message << std::endl;
    exit(EXIT_FAILURE);
  }
  if (!value->is_dict()) {
    std::cerr << "Invalid config format" << std::endl;
    exit(EXIT_FAILURE);
  }
  const auto* listen = value->FindStringKey("listen");
  if (listen) {
    cmdline->listen = *listen;
  }
  const auto* proxy = value->FindStringKey("proxy");
  if (proxy) {
    cmdline->proxy = *proxy;
  }
  const auto* concurrency = value->FindStringKey("insecure-concurrency");
  if (concurrency) {
    cmdline->concurrency = *concurrency;
  }
  const auto* extra_headers = value->FindStringKey("extra-headers");
  if (extra_headers) {
    cmdline->extra_headers = *extra_headers;
  }
  const auto* host_resolver_rules = value->FindStringKey("host-resolver-rules");
  if (host_resolver_rules) {
    cmdline->host_resolver_rules = *host_resolver_rules;
  }
  const auto* resolver_range = value->FindStringKey("resolver-range");
  if (resolver_range) {
    cmdline->resolver_range = *resolver_range;
  }
  cmdline->no_log = true;
  const auto* log = value->FindStringKey("log");
  if (log) {
    cmdline->no_log = false;
    cmdline->log = base::FilePath::FromUTF8Unsafe(*log);
  }
  const auto* log_net_log = value->FindStringKey("log-net-log");
  if (log_net_log) {
    cmdline->log_net_log = base::FilePath::FromUTF8Unsafe(*log_net_log);
  }
  const auto* ssl_key_log_file = value->FindStringKey("ssl-key-log-file");
  if (ssl_key_log_file) {
    cmdline->ssl_key_log_file =
        base::FilePath::FromUTF8Unsafe(*ssl_key_log_file);
  }
}

bool ParseCommandLine(const CommandLine& cmdline, Params* params) {
  params->protocol = net::ClientProtocol::kSocks5;
  params->listen_addr = "0.0.0.0";
  params->listen_port = 1080;
  url::AddStandardScheme("socks",
                         url::SCHEME_WITH_HOST_PORT_AND_USER_INFORMATION);
  url::AddStandardScheme("redir", url::SCHEME_WITH_HOST_AND_PORT);
  if (!cmdline.listen.empty()) {
    GURL url(cmdline.listen);
    if (url.scheme() == "socks") {
      params->protocol = net::ClientProtocol::kSocks5;
      params->listen_port = 1080;
    } else if (url.scheme() == "http") {
      params->protocol = net::ClientProtocol::kHttp;
      params->listen_port = 8080;
    } else if (url.scheme() == "redir") {
#if BUILDFLAG(IS_LINUX)
      params->protocol = net::ClientProtocol::kRedir;
      params->listen_port = 1080;
#else
      std::cerr << "Redir protocol only supports Linux." << std::endl;
      return false;
#endif
    } else {
      std::cerr << "Invalid scheme in --listen" << std::endl;
      return false;
    }
    if (!url.username().empty()) {
      params->listen_user = base::UnescapeBinaryURLComponent(url.username());
    }
    if (!url.password().empty()) {
      params->listen_pass = base::UnescapeBinaryURLComponent(url.password());
    }
    if (!url.host().empty()) {
      params->listen_addr = url.HostNoBrackets();
    }
    if (!url.port().empty()) {
      if (!base::StringToInt(url.port(), &params->listen_port)) {
        std::cerr << "Invalid port in --listen" << std::endl;
        return false;
      }
      if (params->listen_port <= 0 ||
          params->listen_port > std::numeric_limits<uint16_t>::max()) {
        std::cerr << "Invalid port in --listen" << std::endl;
        return false;
      }
    }
  }

  params->proxy_url = "direct://";
  GURL url(cmdline.proxy);
  GURL::Replacements remove_auth;
  remove_auth.ClearUsername();
  remove_auth.ClearPassword();
  GURL url_no_auth = url.ReplaceComponents(remove_auth);
  if (!cmdline.proxy.empty()) {
    params->proxy_url = url_no_auth.GetWithEmptyPath().spec();
    if (params->proxy_url.empty()) {
      std::cerr << "Invalid proxy URL" << std::endl;
      return false;
    } else if (params->proxy_url.back() == '/') {
      params->proxy_url.pop_back();
    }
    net::GetIdentityFromURL(url, &params->proxy_user, &params->proxy_pass);
  }

  if (!cmdline.concurrency.empty()) {
    if (!base::StringToInt(cmdline.concurrency, &params->concurrency) ||
        params->concurrency < 1) {
      std::cerr << "Invalid concurrency" << std::endl;
      return false;
    }
  } else {
    params->concurrency = 1;
  }

  params->extra_headers.AddHeadersFromString(cmdline.extra_headers);

  params->host_resolver_rules = cmdline.host_resolver_rules;

  if (params->protocol == net::ClientProtocol::kRedir) {
    std::string range = "100.64.0.0/10";
    if (!cmdline.resolver_range.empty())
      range = cmdline.resolver_range;

    if (!net::ParseCIDRBlock(range, &params->resolver_range,
                             &params->resolver_prefix)) {
      std::cerr << "Invalid resolver range" << std::endl;
      return false;
    }
    if (params->resolver_range.IsIPv6()) {
      std::cerr << "IPv6 resolver range not supported" << std::endl;
      return false;
    }
  }

  if (!cmdline.no_log) {
    if (!cmdline.log.empty()) {
      params->log_settings.logging_dest = logging::LOG_TO_FILE;
      params->log_settings.log_file_path = cmdline.log.value().c_str();
    } else {
      params->log_settings.logging_dest = logging::LOG_TO_STDERR;
    }
  } else {
    params->log_settings.logging_dest = logging::LOG_NONE;
  }

  params->net_log_path = cmdline.log_net_log;
  params->ssl_key_path = cmdline.ssl_key_log_file;

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
    const char* event_type = NetLogEventTypeToString(entry.type);
    const char* event_phase = NetLog::EventPhaseToString(entry.phase);
    base::Value params(entry.ToDict());
    std::string params_str;
    base::JSONWriter::Write(params, &params_str);
    params_str.insert(0, ": ");

    VLOG(1) << source_type << "(" << entry.source.id << "): " << event_type
            << ": " << event_phase << params_str;
  }
};
}  // namespace

namespace {
std::unique_ptr<URLRequestContext> BuildCertURLRequestContext(NetLog* net_log) {
  URLRequestContextBuilder builder;

  builder.DisableHttpCache();
  builder.set_net_log(net_log);

  ProxyConfig proxy_config;
  auto proxy_service =
      ConfiguredProxyResolutionService::CreateWithoutProxyResolver(
          std::make_unique<ProxyConfigServiceFixed>(
              ProxyConfigWithAnnotation(proxy_config, kTrafficAnnotation)),
          net_log);
  proxy_service->ForceReloadProxyConfig();
  builder.set_proxy_resolution_service(std::move(proxy_service));

  return builder.Build();
}

// Builds a URLRequestContext assuming there's only a single loop.
std::unique_ptr<URLRequestContext> BuildURLRequestContext(
    const Params& params,
    scoped_refptr<CertNetFetcherURLRequest> cert_net_fetcher,
    NetLog* net_log) {
  URLRequestContextBuilder builder;

  builder.DisableHttpCache();
  builder.set_net_log(net_log);

  ProxyConfig proxy_config;
  proxy_config.proxy_rules().ParseFromString(params.proxy_url);
  LOG(INFO) << "Proxying via " << params.proxy_url;
  auto proxy_service =
      ConfiguredProxyResolutionService::CreateWithoutProxyResolver(
          std::make_unique<ProxyConfigServiceFixed>(
              ProxyConfigWithAnnotation(proxy_config, kTrafficAnnotation)),
          net_log);
  proxy_service->ForceReloadProxyConfig();
  builder.set_proxy_resolution_service(std::move(proxy_service));

  if (!params.host_resolver_rules.empty()) {
    builder.set_host_mapping_rules(params.host_resolver_rules);
  }

  builder.SetCertVerifier(
      CertVerifier::CreateDefault(std::move(cert_net_fetcher)));

  builder.set_proxy_delegate(
      std::make_unique<NaiveProxyDelegate>(params.extra_headers));

  auto context = builder.Build();

  if (!params.proxy_url.empty() && !params.proxy_user.empty() &&
      !params.proxy_pass.empty()) {
    auto* session = context->http_transaction_factory()->GetSession();
    auto* auth_cache = session->http_auth_cache();
    std::string proxy_url = params.proxy_url;
    GURL proxy_gurl(proxy_url);
    if (proxy_url.compare(0, 7, "quic://") == 0) {
      proxy_url.replace(0, 4, "https");
      proxy_gurl = GURL(proxy_url);
      auto* quic = context->quic_context()->params();
      quic->supported_versions = {quic::ParsedQuicVersion::RFCv1()};
      quic->origins_to_force_quic_on.insert(
          net::HostPortPair::FromURL(proxy_gurl));
    }
    url::SchemeHostPort auth_origin(proxy_gurl);
    AuthCredentials credentials(params.proxy_user, params.proxy_pass);
    auth_cache->Add(auth_origin, HttpAuth::AUTH_PROXY,
                    /*realm=*/{}, HttpAuth::AUTH_SCHEME_BASIC, {},
                    /*challenge=*/"Basic", credentials, /*path=*/"/");
  }

  return context;
}
}  // namespace
}  // namespace net

int main(int argc, char* argv[]) {
  naive_partition_alloc_support::ReconfigureEarly();

  url::AddStandardScheme("quic",
                         url::SCHEME_WITH_HOST_PORT_AND_USER_INFORMATION);
  base::FeatureList::InitializeInstance(
      "PartitionConnectionsByNetworkIsolationKey", std::string());
  net::ClientSocketPoolManager::set_max_sockets_per_pool(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerPool * kExpectedMaxUsers);
  net::ClientSocketPoolManager::set_max_sockets_per_proxy_server(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerPool * kExpectedMaxUsers);
  net::ClientSocketPoolManager::set_max_sockets_per_group(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerGroup * kExpectedMaxUsers);

  naive_partition_alloc_support::ReconfigureAfterFeatureListInit();

#if BUILDFLAG(IS_APPLE)
  base::mac::ScopedNSAutoreleasePool pool;
#endif

  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);

  CommandLine cmdline;
  Params params;
  const auto& proc = *base::CommandLine::ForCurrentProcess();
  const auto& args = proc.GetArgs();
  if (args.empty()) {
    if (proc.argv().size() >= 2) {
      GetCommandLine(proc, &cmdline);
    } else {
      auto path = base::FilePath::FromUTF8Unsafe("config.json");
      GetCommandLineFromConfig(path, &cmdline);
    }
  } else {
    base::FilePath path(args[0]);
    GetCommandLineFromConfig(path, &cmdline);
  }
  if (!ParseCommandLine(cmdline, &params)) {
    return EXIT_FAILURE;
  }
  CHECK(logging::InitLogging(params.log_settings));

  base::SingleThreadTaskExecutor io_task_executor(base::MessagePumpType::IO);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("naive");

  naive_partition_alloc_support::ReconfigureAfterTaskRunnerInit();

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

  auto cert_context = net::BuildCertURLRequestContext(net_log);
  scoped_refptr<net::CertNetFetcherURLRequest> cert_net_fetcher;
  // The builtin verifier is supported but not enabled by default on Mac,
  // falling back to CreateSystemVerifyProc() which drops the net fetcher,
  // causing a DCHECK in ~CertNetFetcherURLRequest().
  // See CertVerifier::CreateDefaultWithoutCaching() and
  // CertVerifyProc::CreateSystemVerifyProc() for the build flags.
#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED) || BUILDFLAG(IS_FUCHSIA) || \
    BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  cert_net_fetcher = base::MakeRefCounted<net::CertNetFetcherURLRequest>();
  cert_net_fetcher->SetURLRequestContext(cert_context.get());
#endif
  auto context =
      net::BuildURLRequestContext(params, std::move(cert_net_fetcher), net_log);
  auto* session = context->http_transaction_factory()->GetSession();

  auto listen_socket =
      std::make_unique<net::TCPServerSocket>(net_log, net::NetLogSource());

  int result = listen_socket->ListenWithAddressAndPort(
      params.listen_addr, params.listen_port, kListenBackLog);
  if (result != net::OK) {
    LOG(ERROR) << "Failed to listen: " << result;
    return EXIT_FAILURE;
  }
  LOG(INFO) << "Listening on " << params.listen_addr << ":"
            << params.listen_port;

  std::unique_ptr<net::RedirectResolver> resolver;
  if (params.protocol == net::ClientProtocol::kRedir) {
    auto resolver_socket =
        std::make_unique<net::UDPServerSocket>(net_log, net::NetLogSource());
    resolver_socket->AllowAddressReuse();
    net::IPAddress listen_addr;
    if (!listen_addr.AssignFromIPLiteral(params.listen_addr)) {
      LOG(ERROR) << "Failed to open resolver: " << net::ERR_ADDRESS_INVALID;
      return EXIT_FAILURE;
    }

    result = resolver_socket->Listen(
        net::IPEndPoint(listen_addr, params.listen_port));
    if (result != net::OK) {
      LOG(ERROR) << "Failed to open resolver: " << result;
      return EXIT_FAILURE;
    }

    resolver = std::make_unique<net::RedirectResolver>(
        std::move(resolver_socket), params.resolver_range,
        params.resolver_prefix);
  }

  net::NaiveProxy naive_proxy(std::move(listen_socket), params.protocol,
                              params.listen_user, params.listen_pass,
                              params.concurrency, resolver.get(), session,
                              kTrafficAnnotation);

  base::RunLoop().Run();

  return EXIT_SUCCESS;
}
