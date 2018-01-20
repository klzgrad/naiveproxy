// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/json/json_file_value_serializer.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/process/memory.h"
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
#include "net/ssl/ssl_config_service.h"
#include "net/ssl/ssl_key_logger_impl.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_versions.h"
#include "net/tools/naive/naive_command_line.h"
#include "net/tools/naive/naive_config.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/tools/naive/naive_proxy.h"
#include "net/tools/naive/naive_proxy_delegate.h"
#include "net/tools/naive/redirect_resolver.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"
#include "url/url_util.h"

#if BUILDFLAG(IS_APPLE)
#include "base/allocator/early_zone_registration_apple.h"
#include "base/apple/scoped_nsautorelease_pool.h"
#endif

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
#include "base/allocator/allocator_check.h"
#include "base/allocator/partition_alloc_support.h"
#include "base/allocator/partition_allocator/src/partition_alloc/shim/allocator_shim.h"
#endif

namespace {

constexpr int kListenBackLog = 512;
constexpr int kDefaultMaxSocketsPerPool = 256;
constexpr int kDefaultMaxSocketsPerGroup = 255;
constexpr int kExpectedMaxUsers = 8;
constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("naive", "");

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
    const NaiveConfig& config,
    scoped_refptr<CertNetFetcherURLRequest> cert_net_fetcher,
    NetLog* net_log) {
  URLRequestContextBuilder builder;

  builder.DisableHttpCache();

  // Overrides HTTP/2 initial window size default values to accommodate
  // high BDP links.
  // See net/http/http_network_session.cc for the default values.
  // Alternative implementations than fixed large windows:
  // (1) Dynamic window scaling, see
  //     https://github.com/dotnet/runtime/pull/54755
  //     and https://grpc.io/blog/grpc-go-perf-improvements/
  //     This approach estimates throughput and RTT in userspace
  //     and incurs big architectural complexity.
  // (2) Obtains TCP receive windows from Linux-specific TCP_INFO.
  //     This approach is not portable.
  // Security impact:
  // This use of non-default settings creates a fingerprinting feature
  // that is visible to proxy servers, though this is only exploitable
  // if the proxy servers can be MITM'd.

  constexpr int kMaxBandwidthMBps = 125;
  constexpr double kTypicalRttSecond = 0.256;
  constexpr int kMaxBdpMB = kMaxBandwidthMBps * kTypicalRttSecond;

  // The windows size should be twice the BDP because WINDOW_UPDATEs
  // are sent after half the window is unacknowledged.
  constexpr int kTypicalWindow = kMaxBdpMB * 2 * 1024 * 1024;
  HttpNetworkSessionParams http_network_session_params;
  http_network_session_params.spdy_session_max_recv_window_size =
      kTypicalWindow * 2;
  http_network_session_params
      .http2_settings[spdy::SETTINGS_INITIAL_WINDOW_SIZE] = kTypicalWindow;
  builder.set_http_network_session_params(http_network_session_params);

  builder.set_net_log(net_log);

  ProxyConfig proxy_config;
  proxy_config.proxy_rules().type =
      net::ProxyConfig::ProxyRules::Type::PROXY_LIST;
  proxy_config.proxy_rules().single_proxies.SetSingleProxyChain(
      config.proxy_chain);
  LOG(INFO) << "Proxying via "
            << proxy_config.proxy_rules().single_proxies.ToDebugString();
  auto proxy_service =
      ConfiguredProxyResolutionService::CreateWithoutProxyResolver(
          std::make_unique<ProxyConfigServiceFixed>(
              ProxyConfigWithAnnotation(proxy_config, kTrafficAnnotation)),
          net_log);
  proxy_service->ForceReloadProxyConfig();
  builder.set_proxy_resolution_service(std::move(proxy_service));

  if (!config.host_resolver_rules.empty()) {
    builder.set_host_mapping_rules(config.host_resolver_rules);
  }

  builder.SetCertVerifier(
      CertVerifier::CreateDefault(std::move(cert_net_fetcher)));

  builder.set_proxy_delegate(std::make_unique<NaiveProxyDelegate>(
      config.extra_headers,
      std::vector<PaddingType>{PaddingType::kVariant1, PaddingType::kNone}));

  if (config.no_post_quantum == true) {
    struct NoPostQuantum : public SSLConfigService {
      SSLContextConfig GetSSLContextConfig() override {
        SSLContextConfig config;
        config.post_quantum_override = false;
        return config;
      }

      bool CanShareConnectionWithClientCerts(std::string_view) const override {
        return false;
      }
    };
    builder.set_ssl_config_service(std::make_unique<NoPostQuantum>());
  }

  auto context = builder.Build();

  if (!config.origins_to_force_quic_on.empty()) {
    auto* quic = context->quic_context()->params();
    quic->supported_versions = {quic::ParsedQuicVersion::RFCv1()};
    quic->origins_to_force_quic_on.insert(
        config.origins_to_force_quic_on.begin(),
        config.origins_to_force_quic_on.end());
  }

  for (const auto& [k, v] : config.auth_store) {
    auto* session = context->http_transaction_factory()->GetSession();
    auto* auth_cache = session->http_auth_cache();
    auth_cache->Add(k, HttpAuth::AUTH_PROXY,
                    /*realm=*/{}, HttpAuth::AUTH_SCHEME_BASIC, {},
                    /*challenge=*/"Basic", v, /*path=*/"/");
  }

  return context;
}
}  // namespace
}  // namespace net

int main(int argc, char* argv[]) {
  // chrome/app/chrome_exe_main_mac.cc: main()
#if BUILDFLAG(IS_APPLE)
  partition_alloc::EarlyMallocZoneRegistration();
#endif

  // content/app/content_main.cc: RunContentProcess()
#if BUILDFLAG(IS_APPLE)
  base::apple::ScopedNSAutoreleasePool pool;
#endif

  // content/app/content_main.cc: RunContentProcess()
#if BUILDFLAG(IS_APPLE) && PA_BUILDFLAG(USE_ALLOCATOR_SHIM)
  // The static initializer function for initializing PartitionAlloc
  // InitializeDefaultMallocZoneWithPartitionAlloc() would be removed by the
  // linker if allocator_shim.o is not referenced by the following call,
  // resulting in undefined behavior of accessing uninitialized TLS
  // data in PurgeCurrentThread() when PA is enabled.
  allocator_shim::InitializeAllocatorShim();
#endif

  // content/app/content_main.cc: RunContentProcess()
  base::EnableTerminationOnOutOfMemory();

  DuplicateSwitchCollector::InitInstance();

  // content/app/content_main.cc: RunContentProcess()
  base::CommandLine::Init(argc, argv);

  // content/app/content_main.cc: RunContentProcess()
  base::EnableTerminationOnHeapCorruption();

  // content/app/content_main.cc: RunContentProcess()
  //   content/app/content_main_runner_impl.cc: Initialize()
  base::AtExitManager exit_manager;

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
  std::string process_type = "";
  base::allocator::PartitionAllocSupport::Get()->ReconfigureEarlyish(
      process_type);
#endif

  // content/app/content_main.cc: RunContentProcess()
  //   content/app/content_main_runner_impl.cc: Initialize()
  // If we are on a platform where the default allocator is overridden (e.g.
  // with PartitionAlloc on most platforms) smoke-tests that the overriding
  // logic is working correctly. If not causes a hard crash, as its unexpected
  // absence has security implications.
#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
  CHECK(base::allocator::IsAllocatorInitialized());
#endif

  // content/app/content_main.cc: RunContentProcess()
  //   content/app/content_main_runner_impl.cc: Run()
  base::FeatureList::InitInstance("PartitionConnectionsByNetworkIsolationKey",
                                  std::string());

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
  base::allocator::PartitionAllocSupport::Get()
      ->ReconfigureAfterFeatureListInit(/*process_type=*/"");
#endif

  base::SingleThreadTaskExecutor io_task_executor(base::MessagePumpType::IO);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("naive");

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
  base::allocator::PartitionAllocSupport::Get()->ReconfigureAfterTaskRunnerInit(
      process_type);
#endif

  url::AddStandardScheme("quic",
                         url::SCHEME_WITH_HOST_PORT_AND_USER_INFORMATION);
  url::AddStandardScheme("socks",
                         url::SCHEME_WITH_HOST_PORT_AND_USER_INFORMATION);
  url::AddStandardScheme("redir", url::SCHEME_WITH_HOST_AND_PORT);
  net::ClientSocketPoolManager::set_max_sockets_per_pool(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerPool * kExpectedMaxUsers);
  net::ClientSocketPoolManager::set_max_sockets_per_proxy_chain(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerPool * kExpectedMaxUsers);
  net::ClientSocketPoolManager::set_max_sockets_per_group(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      kDefaultMaxSocketsPerGroup * kExpectedMaxUsers);

  const auto& proc = *base::CommandLine::ForCurrentProcess();
  const auto& args = proc.GetArgs();
  base::Value::Dict config_dict;
  if (args.empty() && proc.argv().size() >= 2) {
    config_dict = GetSwitchesAsValue(proc);
  } else {
    base::FilePath config_file;
    if (!args.empty()) {
      config_file = base::FilePath(args[0]);
    } else {
      config_file = base::FilePath::FromUTF8Unsafe("config.json");
    }
    JSONFileValueDeserializer reader(config_file);
    int error_code;
    std::string error_message;
    std::unique_ptr<base::Value> value =
        reader.Deserialize(&error_code, &error_message);
    if (value == nullptr) {
      std::cerr << "Error reading " << config_file << ": (" << error_code
                << ") " << error_message << std::endl;
      return EXIT_FAILURE;
    }
    if (const base::Value::Dict* dict = value->GetIfDict()) {
      config_dict = dict->Clone();
    }
  }

  if (config_dict.contains("h") || config_dict.contains("help")) {
    std::cout << "Usage: naive { OPTIONS | config.json }\n"
                 "\n"
                 "Options:\n"
                 "-h, --help                 Show this message\n"
                 "--version                  Print version\n"
                 "--listen=<proto>://[addr][:port] [--listen=...]\n"
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
                 "--no-post-quantum          No post-quantum key agreement\n"
              << std::endl;
    exit(EXIT_SUCCESS);
  }

  if (config_dict.contains("version")) {
    std::cout << "naive " << version_info::GetVersionNumber() << std::endl;
    exit(EXIT_SUCCESS);
  }

  net::NaiveConfig config;
  if (!config.Parse(config_dict)) {
    return EXIT_FAILURE;
  }
  CHECK(logging::InitLogging(config.log));

  if (!config.ssl_key_log_file.empty()) {
    net::SSLClientSocket::SetSSLKeyLogger(
        std::make_unique<net::SSLKeyLoggerImpl>(config.ssl_key_log_file));
  }

  // The declaration order for net_log and printing_log_observer is
  // important. The destructor of PrintingLogObserver removes itself
  // from net_log, so net_log must be available for entire lifetime of
  // printing_log_observer.
  net::NetLog* net_log = net::NetLog::Get();
  std::unique_ptr<net::FileNetLogObserver> observer;
  if (!config.log_net_log.empty()) {
    observer = net::FileNetLogObserver::CreateUnbounded(
        config.log_net_log, net::NetLogCaptureMode::kDefault, GetConstants());
    observer->StartObserving(net_log);
  }

  // Avoids net log overhead if verbose logging is disabled.
  std::unique_ptr<net::PrintingLogObserver> printing_log_observer;
  if (config.log.logging_dest != logging::LOG_NONE && VLOG_IS_ON(1)) {
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
      net::BuildURLRequestContext(config, std::move(cert_net_fetcher), net_log);
  auto* session = context->http_transaction_factory()->GetSession();

  std::vector<std::unique_ptr<net::NaiveProxy>> naive_proxies;
  std::unique_ptr<net::RedirectResolver> resolver;

  for (const net::NaiveListenConfig& listen_config : config.listen) {
    auto listen_socket =
        std::make_unique<net::TCPServerSocket>(net_log, net::NetLogSource());

    int result = listen_socket->ListenWithAddressAndPort(
        listen_config.addr, listen_config.port, kListenBackLog);
    if (result != net::OK) {
      LOG(ERROR) << "Failed to listen on "
                 << net::ToString(listen_config.protocol) << "://"
                 << listen_config.addr << " " << listen_config.port << ": "
                 << net::ErrorToShortString(result);
      return EXIT_FAILURE;
    }
    LOG(INFO) << "Listening on " << net::ToString(listen_config.protocol)
              << "://" << listen_config.addr << ":" << listen_config.port;

    if (resolver == nullptr &&
        listen_config.protocol == net::ClientProtocol::kRedir) {
      auto resolver_socket =
          std::make_unique<net::UDPServerSocket>(net_log, net::NetLogSource());
      resolver_socket->AllowAddressReuse();
      net::IPAddress listen_addr;
      if (!listen_addr.AssignFromIPLiteral(listen_config.addr)) {
        LOG(ERROR) << "Failed to open resolver: " << listen_config.addr;
        return EXIT_FAILURE;
      }

      result = resolver_socket->Listen(
          net::IPEndPoint(listen_addr, listen_config.port));
      if (result != net::OK) {
        LOG(ERROR) << "Failed to open resolver: "
                   << net::ErrorToShortString(result);
        return EXIT_FAILURE;
      }

      resolver = std::make_unique<net::RedirectResolver>(
          std::move(resolver_socket), config.resolver_range,
          config.resolver_prefix);
    }

    auto naive_proxy = std::make_unique<net::NaiveProxy>(
        std::move(listen_socket), listen_config.protocol, listen_config.user,
        listen_config.pass, config.insecure_concurrency, resolver.get(),
        session, kTrafficAnnotation,
        std::vector<net::PaddingType>{net::PaddingType::kVariant1,
                                      net::PaddingType::kNone});
    naive_proxies.push_back(std::move(naive_proxy));
  }

  base::RunLoop().Run();

  return EXIT_SUCCESS;
}
