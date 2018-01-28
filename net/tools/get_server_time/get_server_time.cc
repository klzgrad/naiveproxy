// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a small utility that snarfs the server time from the
// response headers of an http/https HEAD request and compares it to
// the local time.
//
// TODO(akalin): Also snarf the server time from the TLS handshake, if
// any (http://crbug.com/146090).

#include <cstdio>
#include <cstdlib>
#include <string>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/format_macros.h"
#include "base/i18n/time_formatting.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/log/net_log.h"
#include "net/log/net_log_entry.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_status.h"
#include "url/gurl.h"

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#elif defined(OS_LINUX)
#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_config_service_fixed.h"
#endif

using base::UTF16ToUTF8;

namespace {

// base::TimeTicks::Now() is documented to have a resolution of
// ~1-15ms.
const int64_t kTicksResolutionMs = 15;

// For the sources that are supported (HTTP date headers, TLS
// handshake), the resolution of the server time is 1 second.
const int64_t kServerTimeResolutionMs = 1000;

// Assume base::Time::Now() has the same resolution as
// base::TimeTicks::Now().
//
// TODO(akalin): Figure out the real resolution.
const int64_t kTimeResolutionMs = kTicksResolutionMs;

// Simply quits the current message loop when finished.  Used to make
// URLFetcher synchronous.
class QuitDelegate : public net::URLFetcherDelegate {
 public:
  QuitDelegate() {}

  ~QuitDelegate() override {}

  // net::URLFetcherDelegate implementation.
  void OnURLFetchComplete(const net::URLFetcher* source) override {
    base::RunLoop::QuitCurrentWhenIdleDeprecated();
  }

  void OnURLFetchDownloadProgress(const net::URLFetcher* source,
                                  int64_t current,
                                  int64_t total,
                                  int64_t current_network_bytes) override {
    NOTREACHED();
  }

  void OnURLFetchUploadProgress(const net::URLFetcher* source,
                                int64_t current,
                                int64_t total) override {
    NOTREACHED();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(QuitDelegate);
};

// NetLog::ThreadSafeObserver implementation that simply prints events
// to the logs.
class PrintingLogObserver : public net::NetLog::ThreadSafeObserver {
 public:
  PrintingLogObserver() {}

  ~PrintingLogObserver() override {
    // This is guaranteed to be safe as this program is single threaded.
    net_log()->RemoveObserver(this);
  }

  // NetLog::ThreadSafeObserver implementation:
  void OnAddEntry(const net::NetLogEntry& entry) override {
    // The log level of the entry is unknown, so just assume it maps
    // to VLOG(1).
    if (!VLOG_IS_ON(1))
      return;

    const char* const source_type =
        net::NetLog::SourceTypeToString(entry.source().type);
    const char* const event_type =
        net::NetLog::EventTypeToString(entry.type());
    const char* const event_phase =
        net::NetLog::EventPhaseToString(entry.phase());
    std::unique_ptr<base::Value> params(entry.ParametersToValue());
    std::string params_str;
    if (params.get()) {
      base::JSONWriter::Write(*params, &params_str);
      params_str.insert(0, ": ");
    }

    VLOG(1) << source_type << "(" << entry.source().id << "): "
            << event_type << ": " << event_phase << params_str;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(PrintingLogObserver);
};

// Builds a URLRequestContext assuming there's only a single loop.
std::unique_ptr<net::URLRequestContext> BuildURLRequestContext(
    net::NetLog* net_log) {
  net::URLRequestContextBuilder builder;
#if defined(OS_LINUX)
  // On Linux, use a fixed ProxyConfigService, since the default one
  // depends on glib.
  //
  // TODO(akalin): Remove this once http://crbug.com/146421 is fixed.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(net::ProxyConfig()));
#endif
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  context->set_net_log(net_log);
  return context;
}

// Assuming that the time |server_time| was received from a server,
// that the request for the server was started on |start_ticks|, and
// that it ended on |end_ticks|, fills |server_now| with an estimate
// of the current time and |server_now_uncertainty| with a
// conservative estimate of the uncertainty.
void EstimateServerTimeNow(base::Time server_time,
                           base::TimeTicks start_ticks,
                           base::TimeTicks end_ticks,
                           base::Time* server_now,
                           base::TimeDelta* server_now_uncertainty) {
  const base::TimeDelta delta_ticks = end_ticks - start_ticks;
  const base::TimeTicks mid_ticks = start_ticks + delta_ticks / 2;
  const base::TimeDelta estimated_elapsed = base::TimeTicks::Now() - mid_ticks;

  *server_now = server_time + estimated_elapsed;

  *server_now_uncertainty =
      base::TimeDelta::FromMilliseconds(kServerTimeResolutionMs) +
      delta_ticks + 3 * base::TimeDelta::FromMilliseconds(kTicksResolutionMs);
}

// Assuming that the time of the server is |server_now| with
// uncertainty |server_now_uncertainty| and that the local time is
// |now|, fills |skew| with the skew of the local clock (i.e., add
// |*skew| to a client time to get a server time) and
// |skew_uncertainty| with a conservative estimate of the uncertainty.
void EstimateSkew(base::Time server_now,
                  base::TimeDelta server_now_uncertainty,
                  base::Time now,
                  base::TimeDelta now_uncertainty,
                  base::TimeDelta* skew,
                  base::TimeDelta* skew_uncertainty) {
  *skew = server_now - now;
  *skew_uncertainty = server_now_uncertainty + now_uncertainty;
}

}  // namespace

int main(int argc, char* argv[]) {
#if defined(OS_MACOSX)
  base::mac::ScopedNSAutoreleasePool pool;
#endif

  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  logging::InitLogging(settings);

  const base::CommandLine& parsed_command_line =
      *base::CommandLine::ForCurrentProcess();
  GURL url(parsed_command_line.GetSwitchValueASCII("url"));
  if (!url.is_valid() ||
      (url.scheme() != "http" && url.scheme() != "https")) {
    std::fprintf(
        stderr,
        "Usage: %s --url=[http|https]://www.example.com [--v=[1|2]]\n",
        argv[0]);
    return EXIT_FAILURE;
  }

  base::MessageLoopForIO main_loop;

  // NOTE: A NetworkChangeNotifier could be instantiated here, but
  // that interferes with the request that will be sent; some
  // implementations always send out an OnIPAddressChanged() message,
  // which causes the DNS resolution to abort.  It's simpler to just
  // not instantiate one, since only a single request is sent anyway.

  // The declaration order for net_log and printing_log_observer is
  // important. The destructor of PrintingLogObserver removes itself
  // from net_log, so net_log must be available for entire lifetime of
  // printing_log_observer.
  net::NetLog net_log;
  PrintingLogObserver printing_log_observer;
  net_log.AddObserver(&printing_log_observer,
                      net::NetLogCaptureMode::IncludeSocketBytes());

  QuitDelegate delegate;
  std::unique_ptr<net::URLFetcher> fetcher =
      net::URLFetcher::Create(url, net::URLFetcher::HEAD, &delegate);
  std::unique_ptr<net::URLRequestContext> url_request_context(
      BuildURLRequestContext(&net_log));
  fetcher->SetRequestContext(
      // Since there's only a single thread, there's no need to worry
      // about when the URLRequestContext gets created.
      // The URLFetcher will take a reference on the object, and hence
      // implicitly take ownership.
      new net::TrivialURLRequestContextGetter(url_request_context.get(),
                                              main_loop.task_runner()));
  const base::Time start_time = base::Time::Now();
  const base::TimeTicks start_ticks = base::TimeTicks::Now();

  fetcher->Start();
  std::printf(
      "Request started at %s (ticks = %" PRId64 ")\n",
      UTF16ToUTF8(base::TimeFormatFriendlyDateAndTime(start_time)).c_str(),
      start_ticks.ToInternalValue());

  // |delegate| quits |main_loop| when the request is done.
  base::RunLoop().Run();

  const base::Time end_time = base::Time::Now();
  const base::TimeTicks end_ticks = base::TimeTicks::Now();

  std::printf(
      "Request ended at %s (ticks = %" PRId64 ")\n",
      UTF16ToUTF8(base::TimeFormatFriendlyDateAndTime(end_time)).c_str(),
      end_ticks.ToInternalValue());

  const int64_t delta_ticks_internal =
      end_ticks.ToInternalValue() - start_ticks.ToInternalValue();
  const base::TimeDelta delta_ticks = end_ticks - start_ticks;

  std::printf(
      "Request took %" PRId64 " ticks (%.2f ms)\n",
      delta_ticks_internal, delta_ticks.InMillisecondsF());

  const net::URLRequestStatus status = fetcher->GetStatus();
  if (status.status() != net::URLRequestStatus::SUCCESS) {
    LOG(ERROR) << "Request failed with error code: "
               << net::ErrorToString(status.error());
    return EXIT_FAILURE;
  }

  const net::HttpResponseHeaders* const headers =
      fetcher->GetResponseHeaders();
  if (!headers) {
    LOG(ERROR) << "Response does not have any headers";
    return EXIT_FAILURE;
  }

  size_t iter = 0;
  std::string date_header;
  while (headers->EnumerateHeader(&iter, "Date", &date_header)) {
    std::printf("Got date header: %s\n", date_header.c_str());
  }

  base::Time server_time;
  if (!headers->GetDateValue(&server_time)) {
    LOG(ERROR) << "Could not parse time from server response headers";
    return EXIT_FAILURE;
  }

  std::printf(
      "Got time %s from server\n",
      UTF16ToUTF8(base::TimeFormatFriendlyDateAndTime(server_time)).c_str());

  base::Time server_now;
  base::TimeDelta server_now_uncertainty;
  EstimateServerTimeNow(server_time, start_ticks, end_ticks,
                        &server_now, &server_now_uncertainty);
  base::Time now = base::Time::Now();

  std::printf(
      "According to the server, it is now %s with uncertainty %.2f ms\n",
      UTF16ToUTF8(base::TimeFormatFriendlyDateAndTime(server_now)).c_str(),
      server_now_uncertainty.InMillisecondsF());

  base::TimeDelta skew;
  base::TimeDelta skew_uncertainty;
  EstimateSkew(server_now, server_now_uncertainty, now,
               base::TimeDelta::FromMilliseconds(kTimeResolutionMs),
               &skew, &skew_uncertainty);

  std::printf(
      "An estimate for the local clock skew is %.2f ms with "
      "uncertainty %.2f ms\n",
      skew.InMillisecondsF(),
      skew_uncertainty.InMillisecondsF());

  return EXIT_SUCCESS;
}
