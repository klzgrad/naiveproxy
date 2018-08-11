// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <memory>
#include <string>
#include <utility>

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/cancelable_callback.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_loop_current.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "net/base/address_list.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/dns/dns_client.h"
#include "net/dns/dns_config_service.h"
#include "net/dns/dns_protocol.h"
#include "net/dns/host_cache.h"
#include "net/dns/host_resolver_impl.h"
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_source_type.h"
#include "net/log/net_log_with_source.h"
#include "net/tools/gdig/file_net_log.h"

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#endif

namespace net {

namespace {

bool StringToIPEndPoint(const std::string& ip_address_and_port,
                        IPEndPoint* ip_end_point) {
  DCHECK(ip_end_point);

  std::string ip;
  int port;
  if (!ParseHostAndPort(ip_address_and_port, &ip, &port))
    return false;
  if (port == -1)
    port = dns_protocol::kDefaultPort;

  net::IPAddress ip_address;
  if (!ip_address.AssignFromIPLiteral(ip))
    return false;

  *ip_end_point = net::IPEndPoint(ip_address, static_cast<uint16_t>(port));
  return true;
}

// Convert DnsConfig to human readable text omitting the hosts member.
std::string DnsConfigToString(const DnsConfig& dns_config) {
  std::string output;
  output.append("search ");
  for (size_t i = 0; i < dns_config.search.size(); ++i) {
    output.append(dns_config.search[i] + " ");
  }
  output.append("\n");

  for (size_t i = 0; i < dns_config.nameservers.size(); ++i) {
    output.append("nameserver ");
    output.append(dns_config.nameservers[i].ToString()).append("\n");
  }

  base::StringAppendF(&output, "options ndots:%d\n", dns_config.ndots);
  base::StringAppendF(&output, "options timeout:%d\n",
                      static_cast<int>(dns_config.timeout.InMilliseconds()));
  base::StringAppendF(&output, "options attempts:%d\n", dns_config.attempts);
  if (dns_config.rotate)
    output.append("options rotate\n");
  return output;
}

// Convert DnsConfig hosts member to a human readable text.
std::string DnsHostsToString(const DnsHosts& dns_hosts) {
  std::string output;
  for (DnsHosts::const_iterator i = dns_hosts.begin();
       i != dns_hosts.end();
       ++i) {
    const DnsHostsKey& key = i->first;
    std::string host_name = key.first;
    output.append(IPEndPoint(i->second, 0).ToStringWithoutPort());
    output.append(" ").append(host_name).append("\n");
  }
  return output;
}

struct ReplayLogEntry {
  base::TimeDelta start_time;
  std::string domain_name;
};

typedef std::vector<ReplayLogEntry> ReplayLog;

// Loads and parses a replay log file and fills |replay_log| with a structured
// representation. Returns whether the operation was successful. If not, the
// contents of |replay_log| are undefined.
//
// The replay log is a text file where each line contains
//
//   timestamp_in_milliseconds domain_name
//
// The timestamp_in_milliseconds needs to be an integral delta from start of
// resolution and is in milliseconds. domain_name is the name to be resolved.
//
// The file should be sorted by timestamp in ascending time.
bool LoadReplayLog(const base::FilePath& file_path, ReplayLog* replay_log) {
  std::string original_replay_log_contents;
  if (!base::ReadFileToString(file_path, &original_replay_log_contents)) {
    fprintf(stderr, "Unable to open replay file %s\n",
            file_path.MaybeAsASCII().c_str());
    return false;
  }

  // Strip out \r characters for Windows files. This isn't as efficient as a
  // smarter line splitter, but this particular use does not need to target
  // efficiency.
  std::string replay_log_contents;
  base::RemoveChars(original_replay_log_contents, "\r", &replay_log_contents);

  std::vector<std::string> lines = base::SplitString(
      replay_log_contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  base::TimeDelta previous_delta;
  bool bad_parse = false;
  for (unsigned i = 0; i < lines.size(); ++i) {
    if (lines[i].empty())
      continue;
    std::vector<std::string> time_and_name = base::SplitString(
        lines[i], " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    if (time_and_name.size() != 2) {
      fprintf(
          stderr,
          "[%s %u] replay log should have format 'timestamp domain_name\\n'\n",
          file_path.MaybeAsASCII().c_str(),
          i + 1);
      bad_parse = true;
      continue;
    }

    int64_t delta_in_milliseconds;
    if (!base::StringToInt64(time_and_name[0], &delta_in_milliseconds)) {
      fprintf(
          stderr,
          "[%s %u] replay log should have format 'timestamp domain_name\\n'\n",
          file_path.MaybeAsASCII().c_str(),
          i + 1);
      bad_parse = true;
      continue;
    }

    base::TimeDelta delta =
        base::TimeDelta::FromMilliseconds(delta_in_milliseconds);
    if (delta < previous_delta) {
      fprintf(
          stderr,
          "[%s %u] replay log should be sorted by time\n",
          file_path.MaybeAsASCII().c_str(),
          i + 1);
      bad_parse = true;
      continue;
    }

    previous_delta = delta;
    ReplayLogEntry entry;
    entry.start_time = delta;
    entry.domain_name = time_and_name[1];
    replay_log->push_back(entry);
  }
  return !bad_parse;
}

class GDig {
 public:
  GDig();
  ~GDig();

  enum Result {
    RESULT_NO_RESOLVE = -3,
    RESULT_NO_CONFIG = -2,
    RESULT_WRONG_USAGE = -1,
    RESULT_OK = 0,
    RESULT_PENDING = 1,
  };

  Result Main(int argc, const char* argv[]);

 private:
  bool ParseCommandLine(int argc, const char* argv[]);

  void Start();
  void Finish(Result);

  void OnDnsConfig(const DnsConfig& dns_config_const);
  void OnResolveComplete(unsigned index, AddressList* address_list,
                         base::TimeDelta time_since_start, int val);
  void OnTimeout();
  void ReplayNextEntry();

  base::TimeDelta config_timeout_;
  bool print_config_;
  bool print_hosts_;
  net::IPEndPoint nameserver_;
  base::TimeDelta timeout_;
  int parallellism_;
  ReplayLog replay_log_;
  unsigned replay_log_index_;
  base::Time start_time_;
  int active_resolves_;
  Result result_;

  base::CancelableClosure timeout_closure_;
  std::unique_ptr<DnsConfigService> dns_config_service_;
  std::unique_ptr<FileNetLogObserver> log_observer_;
  std::unique_ptr<NetLog> log_;
  std::unique_ptr<HostResolver> resolver_;
  std::unique_ptr<HostResolver::Request> request_;

#if defined(OS_MACOSX)
  // Without this there will be a mem leak on osx.
  base::mac::ScopedNSAutoreleasePool scoped_pool_;
#endif

  // Need AtExitManager to support AsWeakPtr (in NetLog).
  base::AtExitManager exit_manager_;
};

GDig::GDig()
    : config_timeout_(base::TimeDelta::FromSeconds(5)),
      print_config_(false),
      print_hosts_(false),
      parallellism_(6),
      replay_log_index_(0u),
      active_resolves_(0) {
}

GDig::~GDig() {
  if (log_)
    log_->RemoveObserver(log_observer_.get());
}

GDig::Result GDig::Main(int argc, const char* argv[]) {
  if (!ParseCommandLine(argc, argv)) {
      fprintf(stderr,
              "usage: %s [--net_log[=<basic|no_bytes|all>]]"
              " [--print_config] [--print_hosts]"
              " [--nameserver=<ip_address[:port]>]"
              " [--timeout=<milliseconds>]"
              " [--config_timeout=<seconds>]"
              " [--j=<parallel resolves>]"
              " [--replay_file=<path>]"
              " [domain_name]\n",
              argv[0]);
      return RESULT_WRONG_USAGE;
  }

  base::MessageLoopForIO loop;

  result_ = RESULT_PENDING;
  Start();
  if (result_ == RESULT_PENDING)
    base::RunLoop().Run();

  // Destroy it while MessageLoopForIO is alive.
  dns_config_service_.reset();
  return result_;
}

bool GDig::ParseCommandLine(int argc, const char* argv[]) {
  base::CommandLine::Init(argc, argv);
  const base::CommandLine& parsed_command_line =
      *base::CommandLine::ForCurrentProcess();

  if (parsed_command_line.HasSwitch("config_timeout")) {
    int timeout_seconds = 0;
    bool parsed = base::StringToInt(
        parsed_command_line.GetSwitchValueASCII("config_timeout"),
        &timeout_seconds);
    if (parsed && timeout_seconds > 0) {
      config_timeout_ = base::TimeDelta::FromSeconds(timeout_seconds);
    } else {
      fprintf(stderr, "Invalid config_timeout parameter\n");
      return false;
    }
  }

  if (parsed_command_line.HasSwitch("net_log")) {
    std::string log_param = parsed_command_line.GetSwitchValueASCII("net_log");
    NetLogCaptureMode capture_mode =
        NetLogCaptureMode::IncludeCookiesAndCredentials();

    if (log_param.length() > 0) {
      std::map<std::string, NetLogCaptureMode> capture_modes;
      capture_modes["all"] = NetLogCaptureMode::IncludeSocketBytes();
      capture_modes["no_bytes"] =
          NetLogCaptureMode::IncludeCookiesAndCredentials();

      if (capture_modes.find(log_param) != capture_modes.end()) {
        capture_mode = capture_modes[log_param];
      } else {
        fprintf(stderr, "Invalid net_log parameter\n");
        return false;
      }
    }
    log_.reset(new NetLog);
    log_observer_.reset(new FileNetLogObserver(stderr));
    log_->AddObserver(log_observer_.get(), capture_mode);
  }

  print_config_ = parsed_command_line.HasSwitch("print_config");
  print_hosts_ = parsed_command_line.HasSwitch("print_hosts");

  if (parsed_command_line.HasSwitch("nameserver")) {
    std::string nameserver =
      parsed_command_line.GetSwitchValueASCII("nameserver");
    if (!StringToIPEndPoint(nameserver, &nameserver_)) {
      fprintf(stderr,
              "Cannot parse the nameserver string into an IPEndPoint\n");
      return false;
    }
  }

  if (parsed_command_line.HasSwitch("timeout")) {
    int timeout_millis = 0;
    bool parsed = base::StringToInt(
        parsed_command_line.GetSwitchValueASCII("timeout"),
        &timeout_millis);
    if (parsed && timeout_millis > 0) {
      timeout_ = base::TimeDelta::FromMilliseconds(timeout_millis);
    } else {
      fprintf(stderr, "Invalid timeout parameter\n");
      return false;
    }
  }

  if (parsed_command_line.HasSwitch("replay_file")) {
    base::FilePath replay_path =
        parsed_command_line.GetSwitchValuePath("replay_file");
    if (!LoadReplayLog(replay_path, &replay_log_))
      return false;
  }

  if (parsed_command_line.HasSwitch("j")) {
    int parallellism = 0;
    bool parsed = base::StringToInt(
        parsed_command_line.GetSwitchValueASCII("j"),
        &parallellism);
    if (parsed && parallellism > 0) {
      parallellism_ = parallellism;
    } else {
      fprintf(stderr, "Invalid parallellism parameter\n");
    }
  }

  if (parsed_command_line.GetArgs().size() == 1) {
    ReplayLogEntry entry;
    entry.start_time = base::TimeDelta();
#if defined(OS_WIN)
    entry.domain_name = base::UTF16ToASCII(parsed_command_line.GetArgs()[0]);
#else
    entry.domain_name = parsed_command_line.GetArgs()[0];
#endif
    replay_log_.push_back(entry);
  } else if (parsed_command_line.GetArgs().size() != 0) {
    return false;
  }
  return print_config_ || print_hosts_ || !replay_log_.empty();
}

void GDig::Start() {
  if (nameserver_.address().size() > 0) {
    DnsConfig dns_config;
    dns_config.attempts = 1;
    dns_config.nameservers.push_back(nameserver_);
    OnDnsConfig(dns_config);
  } else {
    dns_config_service_ = DnsConfigService::CreateSystemService();
    dns_config_service_->ReadConfig(base::Bind(&GDig::OnDnsConfig,
                                               base::Unretained(this)));
    timeout_closure_.Reset(base::Bind(&GDig::OnTimeout,
                                      base::Unretained(this)));
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, timeout_closure_.callback(), config_timeout_);
  }
}

void GDig::Finish(Result result) {
  DCHECK_NE(RESULT_PENDING, result);
  result_ = result;
  if (base::MessageLoopCurrent::Get())
    base::RunLoop::QuitCurrentWhenIdleDeprecated();
}

void GDig::OnDnsConfig(const DnsConfig& dns_config_const) {
  timeout_closure_.Cancel();
  DCHECK(dns_config_const.IsValid());
  DnsConfig dns_config = dns_config_const;

  if (timeout_.InMilliseconds() > 0)
    dns_config.timeout = timeout_;
  if (print_config_) {
    printf("# Dns Configuration\n"
           "%s", DnsConfigToString(dns_config).c_str());
  }
  if (print_hosts_) {
    printf("# Host Database\n"
           "%s", DnsHostsToString(dns_config.hosts).c_str());
  }

  if (replay_log_.empty()) {
    Finish(RESULT_OK);
    return;
  }

  std::unique_ptr<DnsClient> dns_client(DnsClient::CreateClient(NULL));
  dns_client->SetConfig(dns_config);
  HostResolver::Options options;
  options.max_concurrent_resolves = parallellism_;
  options.max_retry_attempts = 1u;
  std::unique_ptr<HostResolverImpl> resolver(
      new HostResolverImpl(options, log_.get()));
  resolver->SetDnsClient(std::move(dns_client));
  resolver_ = std::move(resolver);

  start_time_ = base::Time::Now();

  ReplayNextEntry();
}

void GDig::ReplayNextEntry() {
  DCHECK_LT(replay_log_index_, replay_log_.size());

  base::TimeDelta time_since_start = base::Time::Now() - start_time_;
  while (replay_log_index_ < replay_log_.size()) {
    const ReplayLogEntry& entry = replay_log_[replay_log_index_];
    if (time_since_start < entry.start_time) {
      // Delay call to next time and return.
      base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
          FROM_HERE, base::Bind(&GDig::ReplayNextEntry, base::Unretained(this)),
          entry.start_time - time_since_start);
      return;
    }

    HostResolver::RequestInfo info(HostPortPair(entry.domain_name.c_str(), 80));
    AddressList* addrlist = new AddressList();
    unsigned current_index = replay_log_index_;
    CompletionCallback callback = base::Bind(&GDig::OnResolveComplete,
                                             base::Unretained(this),
                                             current_index,
                                             base::Owned(addrlist),
                                             time_since_start);
    ++active_resolves_;
    ++replay_log_index_;
    int ret = resolver_->Resolve(
        info, DEFAULT_PRIORITY, addrlist, callback, &request_,
        NetLogWithSource::Make(log_.get(), net::NetLogSourceType::NONE));
    if (ret != ERR_IO_PENDING)
      callback.Run(ret);
  }
}

void GDig::OnResolveComplete(unsigned entry_index,
                             AddressList* address_list,
                             base::TimeDelta resolve_start_time,
                             int val) {
  DCHECK_GT(active_resolves_, 0);
  DCHECK(address_list);
  DCHECK_LT(entry_index, replay_log_.size());
  --active_resolves_;
  base::TimeDelta resolve_end_time = base::Time::Now() - start_time_;
  base::TimeDelta resolve_time = resolve_end_time - resolve_start_time;
  printf("%u %d %d %s %d ",
         entry_index,
         static_cast<int>(resolve_end_time.InMilliseconds()),
         static_cast<int>(resolve_time.InMilliseconds()),
         replay_log_[entry_index].domain_name.c_str(), val);
  if (val != OK) {
    std::string error_string = ErrorToString(val);
    printf("%s", error_string.c_str());
  } else {
    for (size_t i = 0; i < address_list->size(); ++i) {
      if (i != 0)
        printf(" ");
      printf("%s", (*address_list)[i].ToStringWithoutPort().c_str());
    }
  }
  printf("\n");
  if (active_resolves_ == 0 && replay_log_index_ >= replay_log_.size())
    Finish(RESULT_OK);
}

void GDig::OnTimeout() {
  fprintf(stderr, "Timed out waiting to load the dns config\n");
  Finish(RESULT_NO_CONFIG);
}

}  // empty namespace

}  // namespace net

int main(int argc, const char* argv[]) {
  net::GDig dig;
  return dig.Main(argc, argv);
}
