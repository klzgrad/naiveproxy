// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/nqe/network_quality_estimator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_base.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"
#include "base/task/lazy_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/default_tick_clock.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "net/base/host_port_pair.h"
#include "net/base/load_flags.h"
#include "net/base/load_timing_info.h"
#include "net/base/network_interfaces.h"
#include "net/base/trace_constants.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/http/http_status_code.h"
#include "net/nqe/network_quality_estimator_util.h"
#include "net/nqe/throughput_analyzer.h"
#include "net/nqe/weighted_observation.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_status.h"
#include "url/gurl.h"

#if defined(OS_ANDROID)
#include "net/android/cellular_signal_strength.h"
#include "net/android/network_library.h"
#endif  // OS_ANDROID

namespace net {

class HostResolver;

namespace {

#if defined(OS_CHROMEOS)
// SequencedTaskRunner to get the network id. A SequencedTaskRunner is used
// rather than parallel tasks to avoid having many threads getting the network
// id concurrently.
base::LazySequencedTaskRunner g_get_network_id_task_runner =
    LAZY_SEQUENCED_TASK_RUNNER_INITIALIZER(
        base::TaskTraits(base::MayBlock(),
                         base::TaskPriority::BEST_EFFORT,
                         base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN));
#endif

NetworkQualityObservationSource ProtocolSourceToObservationSource(
    SocketPerformanceWatcherFactory::Protocol protocol) {
  switch (protocol) {
    case SocketPerformanceWatcherFactory::PROTOCOL_TCP:
      return NETWORK_QUALITY_OBSERVATION_SOURCE_TCP;
    case SocketPerformanceWatcherFactory::PROTOCOL_QUIC:
      return NETWORK_QUALITY_OBSERVATION_SOURCE_QUIC;
  }
  NOTREACHED();
  return NETWORK_QUALITY_OBSERVATION_SOURCE_TCP;
}

// Returns true if the scheme of the |request| is either HTTP or HTTPS.
bool RequestSchemeIsHTTPOrHTTPS(const URLRequest& request) {
  return request.url().is_valid() && request.url().SchemeIsHTTPOrHTTPS();
}

// Returns the suffix of the histogram that should be used for recording the
// accuracy when the observed RTT is |observed_rtt|. The width of the intervals
// are in exponentially increasing order.
const char* GetHistogramSuffixObservedRTT(const base::TimeDelta& observed_rtt) {
  const int32_t rtt_milliseconds = observed_rtt.InMilliseconds();
  DCHECK_GE(rtt_milliseconds, 0);

  // The values here should remain synchronized with the suffixes specified in
  // histograms.xml.
  static const char* const kSuffixes[] = {
      "0_20",     "20_60",     "60_140",    "140_300",      "300_620",
      "620_1260", "1260_2540", "2540_5100", "5100_Infinity"};
  for (size_t i = 0; i < arraysize(kSuffixes) - 1; ++i) {
    if (rtt_milliseconds <= (20 * (2 << i) - 20))
      return kSuffixes[i];
  }
  return kSuffixes[arraysize(kSuffixes) - 1];
}

// Returns the suffix of the histogram that should be used for recording the
// accuracy when the observed throughput in kilobits per second is
// |observed_throughput_kbps|. The width of the intervals are in exponentially
// increasing order.
const char* GetHistogramSuffixObservedThroughput(
    const int32_t& observed_throughput_kbps) {
  DCHECK_GE(observed_throughput_kbps, 0);

  // The values here should remain synchronized with the suffixes specified in
  // histograms.xml.
  static const char* const kSuffixes[] = {
      "0_20",     "20_60",     "60_140",    "140_300",      "300_620",
      "620_1260", "1260_2540", "2540_5100", "5100_Infinity"};
  for (size_t i = 0; i < arraysize(kSuffixes) - 1; ++i) {
    if (observed_throughput_kbps <= (20 * (2 << i) - 20))
      return kSuffixes[i];
  }
  return kSuffixes[arraysize(kSuffixes) - 1];
}

void RecordRTTAccuracy(base::StringPiece prefix,
                       int32_t metric,
                       base::TimeDelta measuring_duration,
                       base::TimeDelta observed_rtt) {
  const std::string histogram_name =
      base::StringPrintf("%s.EstimatedObservedDiff.%s.%d.%s", prefix.data(),
                         metric >= 0 ? "Positive" : "Negative",
                         static_cast<int32_t>(measuring_duration.InSeconds()),
                         GetHistogramSuffixObservedRTT(observed_rtt));

  base::HistogramBase* histogram = base::Histogram::FactoryGet(
      histogram_name, 1, 10 * 1000 /* 10 seconds */, 50 /* Number of buckets */,
      base::HistogramBase::kUmaTargetedHistogramFlag);
  histogram->Add(std::abs(metric));
}

void RecordThroughputAccuracy(const char* prefix,
                              int32_t metric,
                              base::TimeDelta measuring_duration,
                              int32_t observed_throughput_kbps) {
  const std::string histogram_name = base::StringPrintf(
      "%s.EstimatedObservedDiff.%s.%d.%s", prefix,
      metric >= 0 ? "Positive" : "Negative",
      static_cast<int32_t>(measuring_duration.InSeconds()),
      GetHistogramSuffixObservedThroughput(observed_throughput_kbps));

  base::HistogramBase* histogram = base::Histogram::FactoryGet(
      histogram_name, 1, 1000 * 1000 /* 1 Gbps */, 50 /* Number of buckets */,
      base::HistogramBase::kUmaTargetedHistogramFlag);
  histogram->Add(std::abs(metric));
}

void RecordEffectiveConnectionTypeAccuracy(
    const char* prefix,
    int32_t metric,
    base::TimeDelta measuring_duration,
    EffectiveConnectionType observed_effective_connection_type) {
  const std::string histogram_name =
      base::StringPrintf("%s.EstimatedObservedDiff.%s.%d.%s", prefix,
                         metric >= 0 ? "Positive" : "Negative",
                         static_cast<int32_t>(measuring_duration.InSeconds()),
                         DeprecatedGetNameForEffectiveConnectionType(
                             observed_effective_connection_type));

  base::HistogramBase* histogram = base::Histogram::FactoryGet(
      histogram_name, 0, EFFECTIVE_CONNECTION_TYPE_LAST,
      EFFECTIVE_CONNECTION_TYPE_LAST /* Number of buckets */,
      base::HistogramBase::kUmaTargetedHistogramFlag);
  histogram->Add(std::abs(metric));
}

nqe::internal::NetworkID DoGetCurrentNetworkID() {
  // It is possible that the connection type changed between when
  // GetConnectionType() was called and when the API to determine the
  // network name was called. Check if that happened and retry until the
  // connection type stabilizes. This is an imperfect solution but should
  // capture majority of cases, and should not significantly affect estimates
  // (that are approximate to begin with).
  while (true) {
    nqe::internal::NetworkID network_id(
        NetworkChangeNotifier::GetConnectionType(), std::string(), INT32_MIN);

    switch (network_id.type) {
      case NetworkChangeNotifier::ConnectionType::CONNECTION_UNKNOWN:
      case NetworkChangeNotifier::ConnectionType::CONNECTION_NONE:
      case NetworkChangeNotifier::ConnectionType::CONNECTION_BLUETOOTH:
      case NetworkChangeNotifier::ConnectionType::CONNECTION_ETHERNET:
        break;
      case NetworkChangeNotifier::ConnectionType::CONNECTION_WIFI:
#if defined(OS_ANDROID) || defined(OS_LINUX) || defined(OS_WIN)
        network_id.id = GetWifiSSID();
#endif
        break;
      case NetworkChangeNotifier::ConnectionType::CONNECTION_2G:
      case NetworkChangeNotifier::ConnectionType::CONNECTION_3G:
      case NetworkChangeNotifier::ConnectionType::CONNECTION_4G:
#if defined(OS_ANDROID)
        network_id.id = android::GetTelephonyNetworkOperator();
#endif
        break;
      default:
        NOTREACHED() << "Unexpected connection type = " << network_id.type;
        break;
    }

    if (network_id.type == NetworkChangeNotifier::GetConnectionType())
      return network_id;
  }
  NOTREACHED();
}

}  // namespace

NetworkQualityEstimator::NetworkQualityEstimator(
    std::unique_ptr<NetworkQualityEstimatorParams> params,
    NetLog* net_log)
    : params_(std::move(params)),
      use_localhost_requests_(false),
      disable_offline_check_(false),
      tick_clock_(base::DefaultTickClock::GetInstance()),
      last_connection_change_(tick_clock_->NowTicks()),
      current_network_id_(nqe::internal::NetworkID(
          NetworkChangeNotifier::ConnectionType::CONNECTION_UNKNOWN,
          std::string(),
          INT32_MIN)),
      http_downstream_throughput_kbps_observations_(
          params_.get(),
          tick_clock_,
          params_->weight_multiplier_per_second(),
          params_->weight_multiplier_per_signal_strength_level()),
      effective_connection_type_at_last_main_frame_(
          EFFECTIVE_CONNECTION_TYPE_UNKNOWN),
      effective_connection_type_recomputation_interval_(
          base::TimeDelta::FromSeconds(10)),
      rtt_observations_size_at_last_ect_computation_(0),
      throughput_observations_size_at_last_ect_computation_(0),
      transport_rtt_observation_count_last_ect_computation_(0),
      end_to_end_rtt_observation_count_at_last_ect_computation_(0),
      new_rtt_observations_since_last_ect_computation_(0),
      new_throughput_observations_since_last_ect_computation_(0),
      increase_in_transport_rtt_updater_posted_(false),
      effective_connection_type_(EFFECTIVE_CONNECTION_TYPE_UNKNOWN),
      cached_estimate_applied_(false),
      net_log_(NetLogWithSource::Make(
          net_log,
          net::NetLogSourceType::NETWORK_QUALITY_ESTIMATOR)),
      event_creator_(net_log_),
      weak_ptr_factory_(this) {
  rtt_ms_observations_.reserve(nqe::internal::OBSERVATION_CATEGORY_COUNT);
  for (int i = 0; i < nqe::internal::OBSERVATION_CATEGORY_COUNT; ++i) {
    rtt_ms_observations_.push_back(ObservationBuffer(
        params_.get(), tick_clock_, params_->weight_multiplier_per_second(),
        params_->weight_multiplier_per_signal_strength_level()));
  }

  network_quality_store_.reset(new nqe::internal::NetworkQualityStore());
  NetworkChangeNotifier::AddConnectionTypeObserver(this);
  throughput_analyzer_.reset(new nqe::internal::ThroughputAnalyzer(
      this, params_.get(), base::ThreadTaskRunnerHandle::Get(),
      base::Bind(&NetworkQualityEstimator::OnNewThroughputObservationAvailable,
                 base::Unretained(this)),
      tick_clock_, net_log_));

  watcher_factory_.reset(new nqe::internal::SocketWatcherFactory(
      base::ThreadTaskRunnerHandle::Get(),
      params_->min_socket_watcher_notification_interval(),
      base::Bind(&NetworkQualityEstimator::OnUpdatedTransportRTTAvailable,
                 base::Unretained(this)),
      base::Bind(&NetworkQualityEstimator::ShouldSocketWatcherNotifyRTT,
                 base::Unretained(this)),
      tick_clock_));

  // Record accuracy after a 15 second interval. The values used here must
  // remain in sync with the suffixes specified in
  // tools/metrics/histograms/histograms.xml.
  accuracy_recording_intervals_.push_back(base::TimeDelta::FromSeconds(15));

  GatherEstimatesForNextConnectionType();
}

void NetworkQualityEstimator::AddDefaultEstimates() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!params_->add_default_platform_observations())
    return;

  if (params_->DefaultObservation(current_network_id_.type).http_rtt() !=
      nqe::internal::InvalidRTT()) {
    Observation rtt_observation(
        params_->DefaultObservation(current_network_id_.type)
            .http_rtt()
            .InMilliseconds(),
        tick_clock_->NowTicks(), INT32_MIN,
        NETWORK_QUALITY_OBSERVATION_SOURCE_DEFAULT_HTTP_FROM_PLATFORM);
    AddAndNotifyObserversOfRTT(rtt_observation);
  }

  if (params_->DefaultObservation(current_network_id_.type).transport_rtt() !=
      nqe::internal::InvalidRTT()) {
    Observation rtt_observation(
        params_->DefaultObservation(current_network_id_.type)
            .transport_rtt()
            .InMilliseconds(),
        tick_clock_->NowTicks(), INT32_MIN,
        NETWORK_QUALITY_OBSERVATION_SOURCE_DEFAULT_TRANSPORT_FROM_PLATFORM);
    AddAndNotifyObserversOfRTT(rtt_observation);
  }

  if (params_->DefaultObservation(current_network_id_.type)
          .downstream_throughput_kbps() !=
      nqe::internal::INVALID_RTT_THROUGHPUT) {
    Observation throughput_observation(
        params_->DefaultObservation(current_network_id_.type)
            .downstream_throughput_kbps(),
        tick_clock_->NowTicks(), INT32_MIN,
        NETWORK_QUALITY_OBSERVATION_SOURCE_DEFAULT_HTTP_FROM_PLATFORM);
    AddAndNotifyObserversOfThroughput(throughput_observation);
  }
}

NetworkQualityEstimator::~NetworkQualityEstimator() {
  DCHECK(thread_checker_.CalledOnValidThread());
  NetworkChangeNotifier::RemoveConnectionTypeObserver(this);
}

const std::vector<base::TimeDelta>&
NetworkQualityEstimator::GetAccuracyRecordingIntervals() const {
  DCHECK(thread_checker_.CalledOnValidThread());
  return accuracy_recording_intervals_;
}

void NetworkQualityEstimator::NotifyStartTransaction(
    const URLRequest& request) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!RequestSchemeIsHTTPOrHTTPS(request))
    return;

  // Update |estimated_quality_at_last_main_frame_| if this is a main frame
  // request.
  // TODO(tbansal): Refactor this to a separate method.
  if (request.load_flags() & LOAD_MAIN_FRAME_DEPRECATED) {
    base::TimeTicks now = tick_clock_->NowTicks();
    last_main_frame_request_ = now;

    ComputeEffectiveConnectionType();
    effective_connection_type_at_last_main_frame_ = effective_connection_type_;
    estimated_quality_at_last_main_frame_ = network_quality_;

    // Post the tasks which will run in the future and record the estimation
    // accuracy based on the observations received between now and the time of
    // task execution. Posting the task at different intervals makes it
    // possible to measure the accuracy by comparing the estimate with the
    // observations received over intervals of varying durations.
    for (const base::TimeDelta& measuring_delay :
         GetAccuracyRecordingIntervals()) {
      base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&NetworkQualityEstimator::RecordAccuracyAfterMainFrame,
                     weak_ptr_factory_.GetWeakPtr(), measuring_delay),
          measuring_delay);
    }
  } else {
    MaybeComputeEffectiveConnectionType();
  }
  throughput_analyzer_->NotifyStartTransaction(request);
}

bool NetworkQualityEstimator::IsHangingRequest(
    base::TimeDelta observed_http_rtt) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (transport_rtt_observation_count_last_ect_computation_ >=
          params_->http_rtt_transport_rtt_min_count() &&
      (params_->hanging_request_http_rtt_upper_bound_transport_rtt_multiplier() <=
           0 ||
       observed_http_rtt <
           params_->hanging_request_http_rtt_upper_bound_transport_rtt_multiplier() *
               GetTransportRTT().value_or(base::TimeDelta::FromSeconds(10)))) {
    // If there are sufficient number of transport RTT samples available, use
    // the transport RTT estimate to determine if the request is hanging.
    UMA_HISTOGRAM_TIMES("NQE.RTT.NotAHangingRequest.TransportRTT",
                        observed_http_rtt);
    return false;
  }

  if (params_->hanging_request_http_rtt_upper_bound_http_rtt_multiplier() <=
          0 ||
      observed_http_rtt <
          params_->hanging_request_http_rtt_upper_bound_http_rtt_multiplier() *
              GetHttpRTT().value_or(base::TimeDelta::FromSeconds(10))) {
    // Use the HTTP RTT estimate to determine if the request is hanging.
    UMA_HISTOGRAM_TIMES("NQE.RTT.NotAHangingRequest.HttpRTT",
                        observed_http_rtt);
    return false;
  }

  if (observed_http_rtt <=
      params_->hanging_request_upper_bound_min_http_rtt()) {
    UMA_HISTOGRAM_TIMES("NQE.RTT.NotAHangingRequest.MinHttpBound",
                        observed_http_rtt);
    return false;
  }
  UMA_HISTOGRAM_TIMES("NQE.RTT.HangingRequest", observed_http_rtt);
  return true;
}

void NetworkQualityEstimator::NotifyHeadersReceived(const URLRequest& request) {
  TRACE_EVENT0(kNetTracingCategory,
               "NetworkQualityEstimator::NotifyHeadersReceived");
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!RequestSchemeIsHTTPOrHTTPS(request) ||
      !RequestProvidesRTTObservation(request)) {
    return;
  }

  if (request.load_flags() & LOAD_MAIN_FRAME_DEPRECATED) {
    ComputeEffectiveConnectionType();
    RecordMetricsOnMainFrameRequest();
  }

  LoadTimingInfo load_timing_info;
  request.GetLoadTimingInfo(&load_timing_info);

  // If the load timing info is unavailable, it probably means that the request
  // did not go over the network.
  if (load_timing_info.send_start.is_null() ||
      load_timing_info.receive_headers_end.is_null()) {
    return;
  }
  DCHECK(!request.response_info().was_cached);

  // Duration between when the resource was requested and when the response
  // headers were received.
  const base::TimeDelta observed_http_rtt =
      load_timing_info.receive_headers_end - load_timing_info.send_start;
  if (observed_http_rtt <= base::TimeDelta())
    return;
  DCHECK_GE(observed_http_rtt, base::TimeDelta());

  if (IsHangingRequest(observed_http_rtt))
    return;

  Observation http_rtt_observation(observed_http_rtt.InMilliseconds(),
                                   tick_clock_->NowTicks(),
                                   current_network_id_.signal_strength,
                                   NETWORK_QUALITY_OBSERVATION_SOURCE_HTTP);
  AddAndNotifyObserversOfRTT(http_rtt_observation);
  throughput_analyzer_->NotifyBytesRead(request);
}

void NetworkQualityEstimator::NotifyBytesRead(const URLRequest& request) {
  DCHECK(thread_checker_.CalledOnValidThread());
  throughput_analyzer_->NotifyBytesRead(request);
}

void NetworkQualityEstimator::RecordAccuracyAfterMainFrame(
    base::TimeDelta measuring_duration) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_EQ(0, measuring_duration.InMilliseconds() % 1000);
  DCHECK(ContainsValue(GetAccuracyRecordingIntervals(), measuring_duration));

  const base::TimeTicks now = tick_clock_->NowTicks();

  // Return if the time since |last_main_frame_request_| is less than
  // |measuring_duration|. This may happen if another main frame request started
  // during last |measuring_duration|. Returning here ensures that we do not
  // take inaccurate readings.
  if (now - last_main_frame_request_ < measuring_duration)
    return;

  // Return if the time since |last_main_frame_request_| is off by a factor of
  // 2. This can happen if the task is executed much later than its scheduled
  // time. Returning here ensures that we do not take inaccurate readings.
  if (now - last_main_frame_request_ > 2 * measuring_duration)
    return;

  // Do not record accuracy if there was a connection change since the last main
  // frame request.
  if (last_main_frame_request_ <= last_connection_change_)
    return;

  base::TimeDelta recent_http_rtt;
  if (!GetRecentRTT(nqe::internal::OBSERVATION_CATEGORY_HTTP,
                    last_main_frame_request_, &recent_http_rtt, nullptr)) {
    recent_http_rtt = nqe::internal::InvalidRTT();
  }

  if (estimated_quality_at_last_main_frame_.http_rtt() !=
          nqe::internal::InvalidRTT() &&
      recent_http_rtt != nqe::internal::InvalidRTT()) {
    const int estimated_observed_diff_milliseconds =
        estimated_quality_at_last_main_frame_.http_rtt().InMilliseconds() -
        recent_http_rtt.InMilliseconds();

    RecordRTTAccuracy("NQE.Accuracy.HttpRTT",
                      estimated_observed_diff_milliseconds, measuring_duration,
                      recent_http_rtt);
  }

  base::TimeDelta recent_transport_rtt;
  if (estimated_quality_at_last_main_frame_.transport_rtt() !=
          nqe::internal::InvalidRTT() &&
      GetRecentRTT(nqe::internal::OBSERVATION_CATEGORY_TRANSPORT,
                   last_main_frame_request_, &recent_transport_rtt, nullptr)) {
    const int estimated_observed_diff_milliseconds =
        estimated_quality_at_last_main_frame_.transport_rtt().InMilliseconds() -
        recent_transport_rtt.InMilliseconds();

    RecordRTTAccuracy("NQE.Accuracy.TransportRTT",
                      estimated_observed_diff_milliseconds, measuring_duration,
                      recent_transport_rtt);
  }

  int32_t recent_downstream_throughput_kbps;
  if (estimated_quality_at_last_main_frame_.downstream_throughput_kbps() !=
          nqe::internal::INVALID_RTT_THROUGHPUT &&
      GetRecentDownlinkThroughputKbps(last_main_frame_request_,
                                      &recent_downstream_throughput_kbps)) {
    const int estimated_observed_diff =
        estimated_quality_at_last_main_frame_.downstream_throughput_kbps() -
        recent_downstream_throughput_kbps;

    RecordThroughputAccuracy("NQE.Accuracy.DownstreamThroughputKbps",
                             estimated_observed_diff, measuring_duration,
                             recent_downstream_throughput_kbps);
  }

  EffectiveConnectionType recent_effective_connection_type =
      GetRecentEffectiveConnectionType(last_main_frame_request_);
  if (effective_connection_type_at_last_main_frame_ !=
          EFFECTIVE_CONNECTION_TYPE_UNKNOWN &&
      recent_effective_connection_type != EFFECTIVE_CONNECTION_TYPE_UNKNOWN) {
    const int estimated_observed_diff =
        static_cast<int>(effective_connection_type_at_last_main_frame_) -
        static_cast<int>(recent_effective_connection_type);

    RecordEffectiveConnectionTypeAccuracy(
        "NQE.Accuracy.EffectiveConnectionType", estimated_observed_diff,
        measuring_duration, recent_effective_connection_type);
  }
}

void NetworkQualityEstimator::NotifyRequestCompleted(const URLRequest& request,
                                                     int net_error) {
  TRACE_EVENT0(kNetTracingCategory,
               "NetworkQualityEstimator::NotifyRequestCompleted");
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!RequestSchemeIsHTTPOrHTTPS(request))
    return;

  throughput_analyzer_->NotifyRequestCompleted(request);
}

void NetworkQualityEstimator::NotifyURLRequestDestroyed(
    const URLRequest& request) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!RequestSchemeIsHTTPOrHTTPS(request))
    return;

  throughput_analyzer_->NotifyRequestCompleted(request);
}

void NetworkQualityEstimator::AddRTTObserver(RTTObserver* rtt_observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  rtt_observer_list_.AddObserver(rtt_observer);
}

void NetworkQualityEstimator::RemoveRTTObserver(RTTObserver* rtt_observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  rtt_observer_list_.RemoveObserver(rtt_observer);
}

void NetworkQualityEstimator::AddThroughputObserver(
    ThroughputObserver* throughput_observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  throughput_observer_list_.AddObserver(throughput_observer);
}

void NetworkQualityEstimator::RemoveThroughputObserver(
    ThroughputObserver* throughput_observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  throughput_observer_list_.RemoveObserver(throughput_observer);
}

SocketPerformanceWatcherFactory*
NetworkQualityEstimator::GetSocketPerformanceWatcherFactory() {
  DCHECK(thread_checker_.CalledOnValidThread());

  return watcher_factory_.get();
}

void NetworkQualityEstimator::SetUseLocalHostRequestsForTesting(
    bool use_localhost_requests) {
  DCHECK(thread_checker_.CalledOnValidThread());
  use_localhost_requests_ = use_localhost_requests;
  watcher_factory_->SetUseLocalHostRequestsForTesting(use_localhost_requests_);
  throughput_analyzer_->SetUseLocalHostRequestsForTesting(
      use_localhost_requests_);
}

void NetworkQualityEstimator::SetUseSmallResponsesForTesting(
    bool use_small_responses) {
  DCHECK(thread_checker_.CalledOnValidThread());
  params_->SetUseSmallResponsesForTesting(use_small_responses);
}

void NetworkQualityEstimator::DisableOfflineCheckForTesting(
    bool disable_offline_check) {
  DCHECK(thread_checker_.CalledOnValidThread());
  disable_offline_check_ = disable_offline_check;
}

void NetworkQualityEstimator::ReportEffectiveConnectionTypeForTesting(
    EffectiveConnectionType effective_connection_type) {
  DCHECK(thread_checker_.CalledOnValidThread());

  event_creator_.MaybeAddNetworkQualityChangedEventToNetLog(
      effective_connection_type_,
      params_->TypicalNetworkQuality(effective_connection_type));

  for (auto& observer : effective_connection_type_observer_list_)
    observer.OnEffectiveConnectionTypeChanged(effective_connection_type);

  network_quality_store_->Add(current_network_id_,
                              nqe::internal::CachedNetworkQuality(
                                  tick_clock_->NowTicks(), network_quality_,
                                  effective_connection_type));
}

void NetworkQualityEstimator::ReportRTTsAndThroughputForTesting(
    base::TimeDelta http_rtt,
    base::TimeDelta transport_rtt,
    int32_t downstream_throughput_kbps) {
  DCHECK(thread_checker_.CalledOnValidThread());

  for (auto& observer : rtt_and_throughput_estimates_observer_list_)
    observer.OnRTTOrThroughputEstimatesComputed(http_rtt, transport_rtt,
                                                downstream_throughput_kbps);
}

bool NetworkQualityEstimator::RequestProvidesRTTObservation(
    const URLRequest& request) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  bool private_network_request = nqe::internal::IsPrivateHost(
      request.context()->host_resolver(), HostPortPair::FromURL(request.url()));

  return (use_localhost_requests_ || !private_network_request) &&
         // Verify that response headers are received, so it can be ensured that
         // response is not cached.
         !request.response_info().response_time.is_null() &&
         !request.was_cached() &&
         request.creation_time() >= last_connection_change_ &&
         request.method() == "GET";
}

void NetworkQualityEstimator::OnConnectionTypeChanged(
    NetworkChangeNotifier::ConnectionType type) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Write the estimates of the previous network to the cache.
  network_quality_store_->Add(
      current_network_id_, nqe::internal::CachedNetworkQuality(
                               last_effective_connection_type_computation_,
                               network_quality_, effective_connection_type_));

  // Clear the local state.
  last_connection_change_ = tick_clock_->NowTicks();
  http_downstream_throughput_kbps_observations_.Clear();
  for (int i = 0; i < nqe::internal::OBSERVATION_CATEGORY_COUNT; ++i)
    rtt_ms_observations_[i].Clear();

#if defined(OS_ANDROID)
  if (params_->weight_multiplier_per_signal_strength_level() < 1.0 &&
      NetworkChangeNotifier::IsConnectionCellular(current_network_id_.type)) {
    bool signal_strength_available =
        min_signal_strength_since_connection_change_ &&
        max_signal_strength_since_connection_change_;
    UMA_HISTOGRAM_BOOLEAN("NQE.CellularSignalStrength.LevelAvailable",
                          signal_strength_available);

    if (signal_strength_available) {
      UMA_HISTOGRAM_COUNTS_100(
          "NQE.CellularSignalStrength.LevelDifference",
          max_signal_strength_since_connection_change_.value() -
              min_signal_strength_since_connection_change_.value());
    }
  }
#endif  // OS_ANDROID
  current_network_id_.signal_strength = INT32_MIN;
  min_signal_strength_since_connection_change_.reset();
  max_signal_strength_since_connection_change_.reset();
  network_quality_ = nqe::internal::NetworkQuality();
  end_to_end_rtt_ = base::nullopt;
  effective_connection_type_ = EFFECTIVE_CONNECTION_TYPE_UNKNOWN;
  effective_connection_type_at_last_main_frame_ =
      EFFECTIVE_CONNECTION_TYPE_UNKNOWN;
  rtt_observations_size_at_last_ect_computation_ = 0;
  throughput_observations_size_at_last_ect_computation_ = 0;
  new_rtt_observations_since_last_ect_computation_ = 0;
  new_throughput_observations_since_last_ect_computation_ = 0;
  transport_rtt_observation_count_last_ect_computation_ = 0;
  end_to_end_rtt_observation_count_at_last_ect_computation_ = 0;
  last_socket_watcher_rtt_notification_ = base::TimeTicks();
  estimated_quality_at_last_main_frame_ = nqe::internal::NetworkQuality();
  cached_estimate_applied_ = false;

  GatherEstimatesForNextConnectionType();
  throughput_analyzer_->OnConnectionTypeChanged();
}

void NetworkQualityEstimator::GatherEstimatesForNextConnectionType() {
  DCHECK(thread_checker_.CalledOnValidThread());

#if defined(OS_CHROMEOS)
  if (get_network_id_asynchronously_) {
    // Doing PostTaskAndReplyWithResult by handle because it requires the result
    // type have a default constructor and nqe::internal::NetworkID does not
    // have that.
    g_get_network_id_task_runner.Get()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](scoped_refptr<base::TaskRunner> reply_task_runner,
               base::OnceCallback<void(const nqe::internal::NetworkID&)>
                   reply_callback) {
              reply_task_runner->PostTask(
                  FROM_HERE, base::BindOnce(std::move(reply_callback),
                                            DoGetCurrentNetworkID()));
            },
            base::ThreadTaskRunnerHandle::Get(),
            base::BindOnce(&NetworkQualityEstimator::
                               ContinueGatherEstimatesForNextConnectionType,
                           weak_ptr_factory_.GetWeakPtr())));
    return;
  }
#endif  // defined(OS_CHROMEOS)

  ContinueGatherEstimatesForNextConnectionType(GetCurrentNetworkID());
}

void NetworkQualityEstimator::ContinueGatherEstimatesForNextConnectionType(
    const nqe::internal::NetworkID& network_id) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // Update the local state as part of preparation for the new connection.
  current_network_id_ = network_id;
  RecordNetworkIDAvailability();

  // Read any cached estimates for the new network. If cached estimates are
  // unavailable, add the default estimates.
  if (!ReadCachedNetworkQualityEstimate())
    AddDefaultEstimates();

  ComputeEffectiveConnectionType();
}

int32_t NetworkQualityEstimator::GetCurrentSignalStrength() const {
  DCHECK(thread_checker_.CalledOnValidThread());

#if defined(OS_ANDROID)
  if (params_->weight_multiplier_per_signal_strength_level() >= 1.0)
    return INT32_MIN;
  if (!NetworkChangeNotifier::IsConnectionCellular(current_network_id_.type))
    return INT32_MIN;
  return android::cellular_signal_strength::GetSignalStrengthLevel().value_or(
      INT32_MIN);
#endif  // OS_ANDROID

  return INT32_MIN;
}

void NetworkQualityEstimator::UpdateSignalStrength() {
  DCHECK(thread_checker_.CalledOnValidThread());

  int32_t past_signal_strength = current_network_id_.signal_strength;
  int32_t new_signal_strength = GetCurrentSignalStrength();

  // Check if there is no change in the signal strength.
  if (past_signal_strength == new_signal_strength)
    return;

  // Check if the signal strength is unavailable.
  if (new_signal_strength == INT32_MIN)
    return;

  // Record the network quality we experienced for the previous signal strength
  // (for when we return to that signal strength).
  network_quality_store_->Add(current_network_id_,
                              nqe::internal::CachedNetworkQuality(
                                  tick_clock_->NowTicks(), network_quality_,
                                  effective_connection_type_));

  current_network_id_.signal_strength = new_signal_strength;
  // Update network quality from cached value for new signal strength.
  ReadCachedNetworkQualityEstimate();

  min_signal_strength_since_connection_change_ =
      std::min(min_signal_strength_since_connection_change_.value_or(INT32_MAX),
               current_network_id_.signal_strength);
  max_signal_strength_since_connection_change_ =
      std::max(max_signal_strength_since_connection_change_.value_or(INT32_MIN),
               current_network_id_.signal_strength);
}

void NetworkQualityEstimator::RecordNetworkIDAvailability() const {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (current_network_id_.type ==
          NetworkChangeNotifier::ConnectionType::CONNECTION_WIFI ||
      NetworkChangeNotifier::IsConnectionCellular(current_network_id_.type)) {
    UMA_HISTOGRAM_BOOLEAN("NQE.NetworkIdAvailable",
                          !current_network_id_.id.empty());
  }
}

void NetworkQualityEstimator::RecordMetricsOnMainFrameRequest() const {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (estimated_quality_at_last_main_frame_.http_rtt() !=
      nqe::internal::InvalidRTT()) {
    // Add the 50th percentile value.
    UMA_HISTOGRAM_TIMES("NQE.MainFrame.RTT.Percentile50",
                        estimated_quality_at_last_main_frame_.http_rtt());
  }
  UMA_HISTOGRAM_BOOLEAN("NQE.EstimateAvailable.MainFrame.RTT",
                        estimated_quality_at_last_main_frame_.http_rtt() !=
                            nqe::internal::InvalidRTT());

  if (estimated_quality_at_last_main_frame_.transport_rtt() !=
      nqe::internal::InvalidRTT()) {
    // Add the 50th percentile value.
    UMA_HISTOGRAM_TIMES("NQE.MainFrame.TransportRTT.Percentile50",
                        estimated_quality_at_last_main_frame_.transport_rtt());
  }
  UMA_HISTOGRAM_BOOLEAN("NQE.EstimateAvailable.MainFrame.TransportRTT",
                        estimated_quality_at_last_main_frame_.transport_rtt() !=
                            nqe::internal::InvalidRTT());

  if (estimated_quality_at_last_main_frame_.downstream_throughput_kbps() !=
      nqe::internal::INVALID_RTT_THROUGHPUT) {
    // Add the 50th percentile value.
    UMA_HISTOGRAM_COUNTS_1M(
        "NQE.MainFrame.Kbps.Percentile50",
        estimated_quality_at_last_main_frame_.downstream_throughput_kbps());
  }
  UMA_HISTOGRAM_BOOLEAN(
      "NQE.EstimateAvailable.MainFrame.Kbps",
      estimated_quality_at_last_main_frame_.downstream_throughput_kbps() !=
          nqe::internal::INVALID_RTT_THROUGHPUT);

  UMA_HISTOGRAM_ENUMERATION("NQE.MainFrame.EffectiveConnectionType",
                            effective_connection_type_at_last_main_frame_,
                            EFFECTIVE_CONNECTION_TYPE_LAST);
}

void NetworkQualityEstimator::ComputeBandwidthDelayProduct() {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Reset the bandwidth delay product to prevent stale values being returned.
  bandwidth_delay_product_kbits_.reset();

  // Record the bandwidth delay product (BDP) from the 80 percentile throughput
  // and the 20 percentile transport RTT. Percentiles are reversed for
  // throughput. The reason for using the 20 percentile transport RTT is to get
  // an estimate of the true RTT sans the queueing delay. The minimum value of
  // transport RTT was not used because it is likely to be noisy. For
  // throughput, the 80 percentile value is considered to get an estimate of the
  // maximum bandwidth when there is no congestion. The maximum value of
  // observed throughput was not used because it is likely to be noisy.
  base::TimeDelta transport_rtt = GetRTTEstimateInternal(
      base::TimeTicks(), nqe::internal::OBSERVATION_CATEGORY_TRANSPORT, 20,
      nullptr);
  if (transport_rtt == nqe::internal::InvalidRTT())
    return;

  int32_t downlink_throughput_kbps =
      GetDownlinkThroughputKbpsEstimateInternal(base::TimeTicks(), 20);
  if (downlink_throughput_kbps == nqe::internal::INVALID_RTT_THROUGHPUT)
    return;

  bandwidth_delay_product_kbits_ =
      (downlink_throughput_kbps * transport_rtt.InMilliseconds()) / 1000;

  // Record UMA histograms.
  UMA_HISTOGRAM_TIMES("NQE.BDPComputationTransportRTT.OnECTComputation",
                      transport_rtt);
  UMA_HISTOGRAM_COUNTS_1M("NQE.BDPComputationKbps.OnECTComputation",
                          downlink_throughput_kbps);
  UMA_HISTOGRAM_COUNTS_1M("NQE.BDPKbits.OnECTComputation",
                          bandwidth_delay_product_kbits_.value());
}

void NetworkQualityEstimator::IncreaseInTransportRTTUpdater() {
  DCHECK(thread_checker_.CalledOnValidThread());

  increase_in_transport_rtt_ = ComputeIncreaseInTransportRTT();

  // Stop the timer if there was no recent data and |increase_in_transport_rtt_|
  // could not be computed. This is fine because |increase_in_transport_rtt| can
  // only be computed if there is recent transport RTT data, and the timer is
  // restarted when there is a new observation.
  if (!increase_in_transport_rtt_) {
    increase_in_transport_rtt_updater_posted_ = false;
    return;
  }

  increase_in_transport_rtt_updater_posted_ = true;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&NetworkQualityEstimator::IncreaseInTransportRTTUpdater,
                 weak_ptr_factory_.GetWeakPtr()),
      params_->increase_in_transport_rtt_logging_interval());
}

base::Optional<int32_t> NetworkQualityEstimator::ComputeIncreaseInTransportRTT()
    const {
  DCHECK(thread_checker_.CalledOnValidThread());

  base::TimeTicks now = tick_clock_->NowTicks();

  // The time after which the observations are considered to be recent enough to
  // be a good proxy for the current level of congestion.
  base::TimeTicks recent_start_time = now - params_->recent_time_threshold();

  // Get the median transport RTT observed over the last 5 seconds for each
  // remote host. This is an estimate of the current RTT which will be compared
  // to the baseline obtained from historical data to detect an increase in RTT.
  std::map<nqe::internal::IPHash, int32_t> recent_median_rtts;
  std::map<nqe::internal::IPHash, size_t> recent_observation_counts;
  rtt_ms_observations_[nqe::internal::OBSERVATION_CATEGORY_TRANSPORT]
      .GetPercentileForEachHostWithCounts(recent_start_time, 50, base::nullopt,
                                          &recent_median_rtts,
                                          &recent_observation_counts);

  if (recent_median_rtts.empty())
    return base::nullopt;

  // The time after which the observations are used to calculate the baseline.
  // This is needed because the general network characteristics could have
  // changed over time.
  base::TimeTicks history_start_time =
      now - params_->historical_time_threshold();

  // Create a set of the remote hosts seen in the recent observations so that
  // the data can be filtered while calculating the percentiles.
  std::set<nqe::internal::IPHash> recent_hosts_set;
  for (const auto& recent_median_rtts_for_host : recent_median_rtts)
    recent_hosts_set.insert(recent_median_rtts_for_host.first);

  // Get the minimum transport RTT observed over 1 minute for each remote host.
  // This is an estimate of the true RTT which will be used as a baseline value
  // to detect an increase in RTT. The minimum value is used here because the
  // observed values cannot be lower than the true RTT. The median is used for
  // the recent data to reduce noise in the calculation.
  std::map<nqe::internal::IPHash, int32_t> historical_min_rtts;
  std::map<nqe::internal::IPHash, size_t> historical_observation_counts;
  rtt_ms_observations_[nqe::internal::OBSERVATION_CATEGORY_TRANSPORT]
      .GetPercentileForEachHostWithCounts(
          history_start_time, 0, recent_hosts_set, &historical_min_rtts,
          &historical_observation_counts);

  // Calculate the total observation counts for the hosts common to the recent
  // data and the historical data.
  size_t total_historical_count = 0;
  size_t total_recent_count = 0;
  for (const auto& recent_median_rtts_for_host : recent_median_rtts) {
    nqe::internal::IPHash host = recent_median_rtts_for_host.first;
    total_historical_count += historical_observation_counts[host];
    total_recent_count += recent_observation_counts[host];
  }

  // Compute the increases in transport RTT for each remote host. Also compute
  // the weight for each remote host based on the number of observations.
  double total_weight = 0.0;
  std::vector<nqe::internal::WeightedObservation> weighted_rtts;
  for (auto& host : recent_hosts_set) {
    // The relative weight signifies the amount of confidence in the data. The
    // weight is higher if there were more observations. A regularization term
    // of |1 / recent_hosts_set.size()| is added so that if one particular
    // remote host has a lot of observations, the results do not get skewed.
    double weight =
        1.0 / recent_hosts_set.size() +
        std::min(static_cast<double>(recent_observation_counts[host]) /
                     total_recent_count,
                 static_cast<double>(historical_observation_counts[host]) /
                     total_historical_count);
    weighted_rtts.push_back(nqe::internal::WeightedObservation(
        recent_median_rtts[host] - historical_min_rtts[host], weight));
    total_weight += weight;
  }

  // Sort the increases in RTT for percentile computation.
  std::sort(weighted_rtts.begin(), weighted_rtts.end());

  // Calculate the weighted 50th percentile increase in transport RTT.
  double desired_weight = 0.5 * total_weight;
  for (nqe::internal::WeightedObservation wo : weighted_rtts) {
    desired_weight -= wo.weight;
    if (desired_weight <= 0)
      return wo.value;
  }

  // Calculation will reach here when the 50th percentile is the last value.
  return weighted_rtts.back().value;
}

void NetworkQualityEstimator::ComputeEffectiveConnectionType() {
  DCHECK(thread_checker_.CalledOnValidThread());

  UpdateSignalStrength();

  const base::TimeTicks now = tick_clock_->NowTicks();

  const EffectiveConnectionType past_type = effective_connection_type_;
  last_effective_connection_type_computation_ = now;

  base::TimeDelta http_rtt = nqe::internal::InvalidRTT();
  base::TimeDelta transport_rtt = nqe::internal::InvalidRTT();
  base::TimeDelta end_to_end_rtt = nqe::internal::InvalidRTT();
  int32_t downstream_throughput_kbps = nqe::internal::INVALID_RTT_THROUGHPUT;

  effective_connection_type_ =
      GetRecentEffectiveConnectionTypeAndNetworkQuality(
          base::TimeTicks(), &http_rtt, &transport_rtt, &end_to_end_rtt,
          &downstream_throughput_kbps,
          &transport_rtt_observation_count_last_ect_computation_,
          &end_to_end_rtt_observation_count_at_last_ect_computation_);

  network_quality_ = nqe::internal::NetworkQuality(http_rtt, transport_rtt,
                                                   downstream_throughput_kbps);
  ComputeBandwidthDelayProduct();

  UMA_HISTOGRAM_ENUMERATION("NQE.EffectiveConnectionType.OnECTComputation",
                            effective_connection_type_,
                            EFFECTIVE_CONNECTION_TYPE_LAST);
  if (network_quality_.http_rtt() != nqe::internal::InvalidRTT()) {
    UMA_HISTOGRAM_TIMES("NQE.RTT.OnECTComputation",
                        network_quality_.http_rtt());
  }

  if (network_quality_.transport_rtt() != nqe::internal::InvalidRTT()) {
    UMA_HISTOGRAM_TIMES("NQE.TransportRTT.OnECTComputation",
                        network_quality_.transport_rtt());
  }

  if (end_to_end_rtt != nqe::internal::InvalidRTT()) {
    UMA_HISTOGRAM_TIMES("NQE.EndToEndRTT.OnECTComputation", end_to_end_rtt);
  }
  end_to_end_rtt_ = base::nullopt;
  if (end_to_end_rtt != nqe::internal::InvalidRTT())
    end_to_end_rtt_ = end_to_end_rtt;

  if (network_quality_.downstream_throughput_kbps() !=
      nqe::internal::INVALID_RTT_THROUGHPUT) {
    UMA_HISTOGRAM_COUNTS_1M("NQE.Kbps.OnECTComputation",
                            network_quality_.downstream_throughput_kbps());
  }

  NotifyObserversOfRTTOrThroughputComputed();

  if (past_type != effective_connection_type_)
    NotifyObserversOfEffectiveConnectionTypeChanged();

  event_creator_.MaybeAddNetworkQualityChangedEventToNetLog(
      effective_connection_type_, network_quality_);

  rtt_observations_size_at_last_ect_computation_ =
      rtt_ms_observations_[nqe::internal::OBSERVATION_CATEGORY_HTTP].Size() +
      rtt_ms_observations_[nqe::internal::OBSERVATION_CATEGORY_TRANSPORT]
          .Size();
  throughput_observations_size_at_last_ect_computation_ =
      http_downstream_throughput_kbps_observations_.Size();
  new_rtt_observations_since_last_ect_computation_ = 0;
  new_throughput_observations_since_last_ect_computation_ = 0;
}

EffectiveConnectionType NetworkQualityEstimator::GetEffectiveConnectionType()
    const {
  DCHECK(thread_checker_.CalledOnValidThread());
  return effective_connection_type_;
}

EffectiveConnectionType
NetworkQualityEstimator::GetRecentEffectiveConnectionType(
    const base::TimeTicks& start_time) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  base::TimeDelta http_rtt = nqe::internal::InvalidRTT();
  base::TimeDelta transport_rtt = nqe::internal::InvalidRTT();
  base::TimeDelta end_to_end_rtt = nqe::internal::InvalidRTT();

  int32_t downstream_throughput_kbps = nqe::internal::INVALID_RTT_THROUGHPUT;

  return GetRecentEffectiveConnectionTypeAndNetworkQuality(
      start_time, &http_rtt, &transport_rtt, &end_to_end_rtt,
      &downstream_throughput_kbps, nullptr, nullptr);
}

EffectiveConnectionType
NetworkQualityEstimator::GetRecentEffectiveConnectionTypeAndNetworkQuality(
    const base::TimeTicks& start_time,
    base::TimeDelta* http_rtt,
    base::TimeDelta* transport_rtt,
    base::TimeDelta* end_to_end_rtt,
    int32_t* downstream_throughput_kbps,
    size_t* transport_rtt_observation_count,
    size_t* end_to_end_rtt_observation_count) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  return GetRecentEffectiveConnectionTypeUsingMetrics(
      start_time,
      NetworkQualityEstimator::MetricUsage::MUST_BE_USED /* http_rtt_metric */,
      NetworkQualityEstimator::MetricUsage::
          DO_NOT_USE /* transport_rtt_metric */,
      NetworkQualityEstimator::MetricUsage::
          USE_IF_AVAILABLE /* downstream_throughput_kbps_metric */,
      http_rtt, transport_rtt, end_to_end_rtt, downstream_throughput_kbps,
      transport_rtt_observation_count, end_to_end_rtt_observation_count);
}

EffectiveConnectionType
NetworkQualityEstimator::GetRecentEffectiveConnectionTypeUsingMetrics(
    const base::TimeTicks& start_time,
    NetworkQualityEstimator::MetricUsage http_rtt_metric,
    NetworkQualityEstimator::MetricUsage transport_rtt_metric,
    NetworkQualityEstimator::MetricUsage downstream_throughput_kbps_metric,
    base::TimeDelta* http_rtt,
    base::TimeDelta* transport_rtt,
    base::TimeDelta* end_to_end_rtt,
    int32_t* downstream_throughput_kbps,
    size_t* transport_rtt_observation_count,
    size_t* end_to_end_rtt_observation_count) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  *http_rtt = nqe::internal::InvalidRTT();
  *transport_rtt = nqe::internal::InvalidRTT();
  *end_to_end_rtt = nqe::internal::InvalidRTT();
  *downstream_throughput_kbps = nqe::internal::INVALID_RTT_THROUGHPUT;

  auto forced_ect =
      params_->GetForcedEffectiveConnectionType(current_network_id_.type);
  if (forced_ect) {
    *http_rtt = params_->TypicalNetworkQuality(forced_ect.value()).http_rtt();
    *transport_rtt =
        params_->TypicalNetworkQuality(forced_ect.value()).transport_rtt();
    *downstream_throughput_kbps =
        params_->TypicalNetworkQuality(forced_ect.value())
            .downstream_throughput_kbps();
    return forced_ect.value();
  }

  // If the device is currently offline, then return
  // EFFECTIVE_CONNECTION_TYPE_OFFLINE.
  if (current_network_id_.type == NetworkChangeNotifier::CONNECTION_NONE &&
      !disable_offline_check_) {
    return EFFECTIVE_CONNECTION_TYPE_OFFLINE;
  }

  if (!GetRecentRTT(nqe::internal::OBSERVATION_CATEGORY_HTTP, start_time,
                    http_rtt, nullptr)) {
    *http_rtt = nqe::internal::InvalidRTT();
  }

  if (!GetRecentRTT(nqe::internal::OBSERVATION_CATEGORY_TRANSPORT, start_time,
                    transport_rtt, transport_rtt_observation_count)) {
    *transport_rtt = nqe::internal::InvalidRTT();
  }

  if (!GetRecentRTT(nqe::internal::OBSERVATION_CATEGORY_END_TO_END, start_time,
                    end_to_end_rtt, end_to_end_rtt_observation_count)) {
    *end_to_end_rtt = nqe::internal::InvalidRTT();
  }

  if (*http_rtt != nqe::internal::InvalidRTT() &&
      *transport_rtt != nqe::internal::InvalidRTT()) {
    // Use transport RTT to clamp the HTTP RTT between lower and upper bounds.
    // To improve accuracy, the transport RTT estimate is used only when the
    // transport RTT estimate was computed using at least
    // |params_->http_rtt_transport_rtt_min_count()| observations.
    if (transport_rtt_observation_count_last_ect_computation_ >=
        params_->http_rtt_transport_rtt_min_count()) {
      if (params_->lower_bound_http_rtt_transport_rtt_multiplier() > 0) {
        *http_rtt = std::max(
            *http_rtt,
            *transport_rtt *
                params_->lower_bound_http_rtt_transport_rtt_multiplier());
      }
    }
  }

  if (!GetRecentDownlinkThroughputKbps(start_time, downstream_throughput_kbps))
    *downstream_throughput_kbps = nqe::internal::INVALID_RTT_THROUGHPUT;

  if (*http_rtt == nqe::internal::InvalidRTT() &&
      http_rtt_metric == NetworkQualityEstimator::MetricUsage::MUST_BE_USED) {
    return EFFECTIVE_CONNECTION_TYPE_UNKNOWN;
  }

  if (*transport_rtt == nqe::internal::InvalidRTT() &&
      transport_rtt_metric ==
          NetworkQualityEstimator::MetricUsage::MUST_BE_USED) {
    return EFFECTIVE_CONNECTION_TYPE_UNKNOWN;
  }

  if (*downstream_throughput_kbps == nqe::internal::INVALID_RTT_THROUGHPUT &&
      downstream_throughput_kbps_metric ==
          NetworkQualityEstimator::MetricUsage::MUST_BE_USED) {
    return EFFECTIVE_CONNECTION_TYPE_UNKNOWN;
  }

  if (*http_rtt == nqe::internal::InvalidRTT() &&
      *transport_rtt == nqe::internal::InvalidRTT() &&
      *downstream_throughput_kbps == nqe::internal::INVALID_RTT_THROUGHPUT) {
    // None of the metrics are available.
    return EFFECTIVE_CONNECTION_TYPE_UNKNOWN;
  }

  // Search from the slowest connection type to the fastest to find the
  // EffectiveConnectionType that best matches the current connection's
  // performance. The match is done by comparing RTT and throughput.
  for (size_t i = 0; i < EFFECTIVE_CONNECTION_TYPE_LAST; ++i) {
    EffectiveConnectionType type = static_cast<EffectiveConnectionType>(i);
    if (i == EFFECTIVE_CONNECTION_TYPE_UNKNOWN)
      continue;

    const bool estimated_http_rtt_is_higher_than_threshold =
        http_rtt_metric != NetworkQualityEstimator::MetricUsage::DO_NOT_USE &&
        *http_rtt != nqe::internal::InvalidRTT() &&
        params_->ConnectionThreshold(type).http_rtt() !=
            nqe::internal::InvalidRTT() &&
        *http_rtt >= params_->ConnectionThreshold(type).http_rtt();

    const bool estimated_transport_rtt_is_higher_than_threshold =
        transport_rtt_metric !=
            NetworkQualityEstimator::MetricUsage::DO_NOT_USE &&
        *transport_rtt != nqe::internal::InvalidRTT() &&
        params_->ConnectionThreshold(type).transport_rtt() !=
            nqe::internal::InvalidRTT() &&
        *transport_rtt >= params_->ConnectionThreshold(type).transport_rtt();

    const bool estimated_throughput_is_lower_than_threshold =
        downstream_throughput_kbps_metric !=
            NetworkQualityEstimator::MetricUsage::DO_NOT_USE &&
        *downstream_throughput_kbps != nqe::internal::INVALID_RTT_THROUGHPUT &&
        params_->ConnectionThreshold(type).downstream_throughput_kbps() !=
            nqe::internal::INVALID_RTT_THROUGHPUT &&
        *downstream_throughput_kbps <=
            params_->ConnectionThreshold(type).downstream_throughput_kbps();

    if (estimated_http_rtt_is_higher_than_threshold ||
        estimated_transport_rtt_is_higher_than_threshold ||
        estimated_throughput_is_lower_than_threshold) {
      return type;
    }
  }
  // Return the fastest connection type.
  return static_cast<EffectiveConnectionType>(EFFECTIVE_CONNECTION_TYPE_LAST -
                                              1);
}

void NetworkQualityEstimator::AddEffectiveConnectionTypeObserver(
    EffectiveConnectionTypeObserver* observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(observer);
  effective_connection_type_observer_list_.AddObserver(observer);

  // Notify the |observer| on the next message pump since |observer| may not
  // be completely set up for receiving the callbacks.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&NetworkQualityEstimator::
                                NotifyEffectiveConnectionTypeObserverIfPresent,
                            weak_ptr_factory_.GetWeakPtr(), observer));
}

void NetworkQualityEstimator::RemoveEffectiveConnectionTypeObserver(
    EffectiveConnectionTypeObserver* observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  effective_connection_type_observer_list_.RemoveObserver(observer);
}

void NetworkQualityEstimator::AddRTTAndThroughputEstimatesObserver(
    RTTAndThroughputEstimatesObserver* observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(observer);
  rtt_and_throughput_estimates_observer_list_.AddObserver(observer);

  // Notify the |observer| on the next message pump since |observer| may not
  // be completely set up for receiving the callbacks.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&NetworkQualityEstimator::
                     NotifyRTTAndThroughputEstimatesObserverIfPresent,
                 weak_ptr_factory_.GetWeakPtr(), observer));
}

void NetworkQualityEstimator::RemoveRTTAndThroughputEstimatesObserver(
    RTTAndThroughputEstimatesObserver* observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  rtt_and_throughput_estimates_observer_list_.RemoveObserver(observer);
}

bool NetworkQualityEstimator::GetRecentRTT(
    nqe::internal::ObservationCategory observation_category,
    const base::TimeTicks& start_time,
    base::TimeDelta* rtt,
    size_t* observations_count) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  *rtt = GetRTTEstimateInternal(start_time, observation_category, 50,
                                observations_count);
  return (*rtt != nqe::internal::InvalidRTT());
}

bool NetworkQualityEstimator::GetRecentDownlinkThroughputKbps(
    const base::TimeTicks& start_time,
    int32_t* kbps) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  *kbps = GetDownlinkThroughputKbpsEstimateInternal(start_time, 50);
  return (*kbps != nqe::internal::INVALID_RTT_THROUGHPUT);
}

base::TimeDelta NetworkQualityEstimator::GetRTTEstimateInternal(
    base::TimeTicks start_time,
    nqe::internal::ObservationCategory observation_category,
    int percentile,
    size_t* observations_count) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  // RTT observations are sorted by duration from shortest to longest, thus
  // a higher percentile RTT will have a longer RTT than a lower percentile.
  switch (observation_category) {
    case nqe::internal::OBSERVATION_CATEGORY_HTTP:
    case nqe::internal::OBSERVATION_CATEGORY_TRANSPORT:
    case nqe::internal::OBSERVATION_CATEGORY_END_TO_END:
      return base::TimeDelta::FromMilliseconds(
          rtt_ms_observations_[observation_category]
              .GetPercentile(start_time, current_network_id_.signal_strength,
                             percentile, observations_count)
              .value_or(nqe::internal::INVALID_RTT_THROUGHPUT));
    case nqe::internal::OBSERVATION_CATEGORY_COUNT:
      NOTREACHED();
      return base::TimeDelta();
  }
}

int32_t NetworkQualityEstimator::GetDownlinkThroughputKbpsEstimateInternal(
    const base::TimeTicks& start_time,
    int percentile) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Throughput observations are sorted by kbps from slowest to fastest,
  // thus a higher percentile throughput will be faster than a lower one.
  return http_downstream_throughput_kbps_observations_
      .GetPercentile(start_time, current_network_id_.signal_strength,
                     100 - percentile, nullptr)
      .value_or(nqe::internal::INVALID_RTT_THROUGHPUT);
}

nqe::internal::NetworkID NetworkQualityEstimator::GetCurrentNetworkID() const {
  DCHECK(thread_checker_.CalledOnValidThread());

  // TODO(tbansal): crbug.com/498068 Add NetworkQualityEstimatorAndroid class
  // that overrides this method on the Android platform.

  return DoGetCurrentNetworkID();
}

bool NetworkQualityEstimator::ReadCachedNetworkQualityEstimate() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!params_->persistent_cache_reading_enabled())
    return false;

  nqe::internal::CachedNetworkQuality cached_network_quality;

  const bool cached_estimate_available = network_quality_store_->GetById(
      current_network_id_, &cached_network_quality);
  UMA_HISTOGRAM_BOOLEAN("NQE.CachedNetworkQualityAvailable",
                        cached_estimate_available);

  if (!cached_estimate_available)
    return false;

  EffectiveConnectionType effective_connection_type =
      cached_network_quality.effective_connection_type();

  if (effective_connection_type == EFFECTIVE_CONNECTION_TYPE_UNKNOWN ||
      effective_connection_type == EFFECTIVE_CONNECTION_TYPE_OFFLINE ||
      effective_connection_type == EFFECTIVE_CONNECTION_TYPE_LAST) {
    return false;
  }

  nqe::internal::NetworkQuality network_quality =
      cached_network_quality.network_quality();

  bool update_network_quality_store = false;

  // Populate |network_quality| with synthetic RTT and throughput observations
  // if they are missing.
  if (network_quality.http_rtt().InMilliseconds() ==
      nqe::internal::INVALID_RTT_THROUGHPUT) {
    network_quality.set_http_rtt(
        params_->TypicalNetworkQuality(effective_connection_type).http_rtt());
    update_network_quality_store = true;
  }

  if (network_quality.transport_rtt().InMilliseconds() ==
      nqe::internal::INVALID_RTT_THROUGHPUT) {
    network_quality.set_transport_rtt(
        params_->TypicalNetworkQuality(effective_connection_type)
            .transport_rtt());
    update_network_quality_store = true;
  }

  if (network_quality.downstream_throughput_kbps() ==
      nqe::internal::INVALID_RTT_THROUGHPUT) {
    network_quality.set_downstream_throughput_kbps(
        params_->TypicalNetworkQuality(effective_connection_type)
            .downstream_throughput_kbps());
    update_network_quality_store = true;
  }

  if (update_network_quality_store) {
    network_quality_store_->Add(current_network_id_,
                                nqe::internal::CachedNetworkQuality(
                                    tick_clock_->NowTicks(), network_quality,
                                    effective_connection_type));
  }

  Observation http_rtt_observation(
      network_quality.http_rtt().InMilliseconds(), tick_clock_->NowTicks(),
      INT32_MIN, NETWORK_QUALITY_OBSERVATION_SOURCE_HTTP_CACHED_ESTIMATE);
  AddAndNotifyObserversOfRTT(http_rtt_observation);

  Observation transport_rtt_observation(
      network_quality.transport_rtt().InMilliseconds(), tick_clock_->NowTicks(),
      INT32_MIN, NETWORK_QUALITY_OBSERVATION_SOURCE_TRANSPORT_CACHED_ESTIMATE);
  AddAndNotifyObserversOfRTT(transport_rtt_observation);

  Observation througphput_observation(
      network_quality.downstream_throughput_kbps(), tick_clock_->NowTicks(),
      INT32_MIN, NETWORK_QUALITY_OBSERVATION_SOURCE_HTTP_CACHED_ESTIMATE);
  AddAndNotifyObserversOfThroughput(througphput_observation);

  ComputeEffectiveConnectionType();
  return true;
}

void NetworkQualityEstimator::SetTickClockForTesting(
    const base::TickClock* tick_clock) {
  DCHECK(thread_checker_.CalledOnValidThread());
  tick_clock_ = tick_clock;
  for (int i = 0; i < nqe::internal::OBSERVATION_CATEGORY_COUNT; ++i)
    rtt_ms_observations_[i].SetTickClockForTesting(tick_clock_);
  http_downstream_throughput_kbps_observations_.SetTickClockForTesting(
      tick_clock_);
  throughput_analyzer_->SetTickClockForTesting(tick_clock_);
  watcher_factory_->SetTickClockForTesting(tick_clock_);
}

void NetworkQualityEstimator::OnUpdatedTransportRTTAvailable(
    SocketPerformanceWatcherFactory::Protocol protocol,
    const base::TimeDelta& rtt,
    const base::Optional<nqe::internal::IPHash>& host) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_LT(nqe::internal::INVALID_RTT_THROUGHPUT, rtt.InMilliseconds());

  Observation observation(rtt.InMilliseconds(), tick_clock_->NowTicks(),
                          current_network_id_.signal_strength,
                          ProtocolSourceToObservationSource(protocol), host);
  AddAndNotifyObserversOfRTT(observation);

  // Post a task to compute and update the increase in RTT if not already
  // posted.
  if (!increase_in_transport_rtt_updater_posted_)
    IncreaseInTransportRTTUpdater();
}

void NetworkQualityEstimator::AddAndNotifyObserversOfRTT(
    const Observation& observation) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_NE(nqe::internal::InvalidRTT(),
            base::TimeDelta::FromMilliseconds(observation.value()));
  DCHECK_GT(NETWORK_QUALITY_OBSERVATION_SOURCE_MAX, observation.source());

  if (!ShouldAddObservation(observation))
    return;

  MaybeUpdateCachedEstimateApplied(
      observation,
      &rtt_ms_observations_[nqe::internal::OBSERVATION_CATEGORY_HTTP]);
  MaybeUpdateCachedEstimateApplied(
      observation,
      &rtt_ms_observations_[nqe::internal::OBSERVATION_CATEGORY_TRANSPORT]);
  ++new_rtt_observations_since_last_ect_computation_;

  std::vector<nqe::internal::ObservationCategory> observation_categories =
      observation.GetObservationCategories();
  for (nqe::internal::ObservationCategory observation_category :
       observation_categories) {
    rtt_ms_observations_[observation_category].AddObservation(observation);
  }

  if (observation.source() == NETWORK_QUALITY_OBSERVATION_SOURCE_TCP ||
      observation.source() == NETWORK_QUALITY_OBSERVATION_SOURCE_QUIC) {
    last_socket_watcher_rtt_notification_ = tick_clock_->NowTicks();
  }

  UMA_HISTOGRAM_ENUMERATION("NQE.RTT.ObservationSource", observation.source(),
                            NETWORK_QUALITY_OBSERVATION_SOURCE_MAX);

  base::HistogramBase* raw_observation_histogram = base::Histogram::FactoryGet(
      std::string("NQE.RTT.RawObservation.") +
          nqe::internal::GetNameForObservationSource(observation.source()),
      1, 10 * 1000, 50, base::HistogramBase::kUmaTargetedHistogramFlag);
  if (raw_observation_histogram)
    raw_observation_histogram->Add(observation.value());

  // Maybe recompute the effective connection type since a new RTT observation
  // is available.
  MaybeComputeEffectiveConnectionType();
  for (auto& observer : rtt_observer_list_) {
    observer.OnRTTObservation(observation.value(), observation.timestamp(),
                              observation.source());
  }
}

void NetworkQualityEstimator::AddAndNotifyObserversOfThroughput(
    const Observation& observation) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_NE(nqe::internal::INVALID_RTT_THROUGHPUT, observation.value());
  DCHECK_GT(NETWORK_QUALITY_OBSERVATION_SOURCE_MAX, observation.source());
  DCHECK_EQ(1u, observation.GetObservationCategories().size());
  DCHECK_EQ(nqe::internal::OBSERVATION_CATEGORY_HTTP,
            observation.GetObservationCategories().front());

  if (!ShouldAddObservation(observation))
    return;

  MaybeUpdateCachedEstimateApplied(
      observation, &http_downstream_throughput_kbps_observations_);
  ++new_throughput_observations_since_last_ect_computation_;
  http_downstream_throughput_kbps_observations_.AddObservation(observation);

  UMA_HISTOGRAM_ENUMERATION("NQE.Kbps.ObservationSource", observation.source(),
                            NETWORK_QUALITY_OBSERVATION_SOURCE_MAX);

  base::HistogramBase* raw_observation_histogram = base::Histogram::FactoryGet(
      std::string("NQE.Kbps.RawObservation.") +
          nqe::internal::GetNameForObservationSource(observation.source()),
      1, 10 * 1000, 50, base::HistogramBase::kUmaTargetedHistogramFlag);
  if (raw_observation_histogram)
    raw_observation_histogram->Add(observation.value());

  // Maybe recompute the effective connection type since a new throughput
  // observation is available.
  MaybeComputeEffectiveConnectionType();
  for (auto& observer : throughput_observer_list_) {
    observer.OnThroughputObservation(
        observation.value(), observation.timestamp(), observation.source());
  }
}

void NetworkQualityEstimator::OnNewThroughputObservationAvailable(
    int32_t downstream_kbps) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (downstream_kbps <= 0)
    return;

  DCHECK_NE(nqe::internal::INVALID_RTT_THROUGHPUT, downstream_kbps);

  Observation throughput_observation(downstream_kbps, tick_clock_->NowTicks(),
                                     current_network_id_.signal_strength,
                                     NETWORK_QUALITY_OBSERVATION_SOURCE_HTTP);
  AddAndNotifyObserversOfThroughput(throughput_observation);
}

void NetworkQualityEstimator::MaybeComputeEffectiveConnectionType() {
  DCHECK(thread_checker_.CalledOnValidThread());

  const base::TimeTicks now = tick_clock_->NowTicks();
  // Recompute effective connection type only if
  // |effective_connection_type_recomputation_interval_| has passed since it was
  // last computed or a connection change event was observed since the last
  // computation. Strict inequalities are used to ensure that effective
  // connection type is recomputed on connection change events even if the clock
  // has not updated.
  if (now - last_effective_connection_type_computation_ <
          effective_connection_type_recomputation_interval_ &&
      last_connection_change_ < last_effective_connection_type_computation_ &&
      // Recompute the effective connection type if the previously computed
      // effective connection type was unknown.
      effective_connection_type_ != EFFECTIVE_CONNECTION_TYPE_UNKNOWN &&
      // Recompute the effective connection type if the number of samples
      // available now are 50% more than the number of samples that were
      // available when the effective connection type was last computed.
      rtt_observations_size_at_last_ect_computation_ * 1.5 >=
          (rtt_ms_observations_[nqe::internal::OBSERVATION_CATEGORY_HTTP]
               .Size() +
           rtt_ms_observations_[nqe::internal::OBSERVATION_CATEGORY_TRANSPORT]
               .Size()) &&
      throughput_observations_size_at_last_ect_computation_ * 1.5 >=
          http_downstream_throughput_kbps_observations_.Size() &&
      (new_rtt_observations_since_last_ect_computation_ +
       new_throughput_observations_since_last_ect_computation_) <
          params_->count_new_observations_received_compute_ect()) {
    return;
  }
  ComputeEffectiveConnectionType();
}

void NetworkQualityEstimator::
    NotifyObserversOfEffectiveConnectionTypeChanged() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_NE(EFFECTIVE_CONNECTION_TYPE_LAST, effective_connection_type_);

  // TODO(tbansal): Add hysteresis in the notification.
  for (auto& observer : effective_connection_type_observer_list_)
    observer.OnEffectiveConnectionTypeChanged(effective_connection_type_);

  // Add the estimates of the current network to the cache store.
  network_quality_store_->Add(current_network_id_,
                              nqe::internal::CachedNetworkQuality(
                                  tick_clock_->NowTicks(), network_quality_,
                                  effective_connection_type_));
}

void NetworkQualityEstimator::NotifyObserversOfRTTOrThroughputComputed() const {
  DCHECK(thread_checker_.CalledOnValidThread());

  // TODO(tbansal): Add hysteresis in the notification.
  for (auto& observer : rtt_and_throughput_estimates_observer_list_) {
    observer.OnRTTOrThroughputEstimatesComputed(
        network_quality_.http_rtt(), network_quality_.transport_rtt(),
        network_quality_.downstream_throughput_kbps());
  }
}

void NetworkQualityEstimator::NotifyEffectiveConnectionTypeObserverIfPresent(
    EffectiveConnectionTypeObserver* observer) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!effective_connection_type_observer_list_.HasObserver(observer))
    return;
  if (effective_connection_type_ == EFFECTIVE_CONNECTION_TYPE_UNKNOWN)
    return;
  observer->OnEffectiveConnectionTypeChanged(effective_connection_type_);
}

void NetworkQualityEstimator::NotifyRTTAndThroughputEstimatesObserverIfPresent(
    RTTAndThroughputEstimatesObserver* observer) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!rtt_and_throughput_estimates_observer_list_.HasObserver(observer))
    return;
  observer->OnRTTOrThroughputEstimatesComputed(
      network_quality_.http_rtt(), network_quality_.transport_rtt(),
      network_quality_.downstream_throughput_kbps());
}

void NetworkQualityEstimator::AddNetworkQualitiesCacheObserver(
    nqe::internal::NetworkQualityStore::NetworkQualitiesCacheObserver*
        observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  network_quality_store_->AddNetworkQualitiesCacheObserver(observer);
}

void NetworkQualityEstimator::RemoveNetworkQualitiesCacheObserver(
    nqe::internal::NetworkQualityStore::NetworkQualitiesCacheObserver*
        observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  network_quality_store_->RemoveNetworkQualitiesCacheObserver(observer);
}

void NetworkQualityEstimator::OnPrefsRead(
    const std::map<nqe::internal::NetworkID,
                   nqe::internal::CachedNetworkQuality> read_prefs) {
  DCHECK(thread_checker_.CalledOnValidThread());

  UMA_HISTOGRAM_COUNTS_1M("NQE.Prefs.ReadSize", read_prefs.size());
  for (auto& it : read_prefs) {
    EffectiveConnectionType effective_connection_type =
        it.second.effective_connection_type();
    if (effective_connection_type == EFFECTIVE_CONNECTION_TYPE_UNKNOWN ||
        effective_connection_type == EFFECTIVE_CONNECTION_TYPE_OFFLINE) {
      continue;
    }

    // RTT and throughput values are not set in the prefs.
    DCHECK_EQ(nqe::internal::InvalidRTT(),
              it.second.network_quality().http_rtt());
    DCHECK_EQ(nqe::internal::InvalidRTT(),
              it.second.network_quality().transport_rtt());
    DCHECK_EQ(nqe::internal::INVALID_RTT_THROUGHPUT,
              it.second.network_quality().downstream_throughput_kbps());

    nqe::internal::CachedNetworkQuality cached_network_quality(
        tick_clock_->NowTicks(),
        params_->TypicalNetworkQuality(effective_connection_type),
        effective_connection_type);

    network_quality_store_->Add(it.first, cached_network_quality);
  }
  ReadCachedNetworkQualityEstimate();
}

#if defined(OS_CHROMEOS)
void NetworkQualityEstimator::EnableGetNetworkIdAsynchronously() {
  get_network_id_asynchronously_ = true;
}
#endif  // defined(OS_CHROMEOS)

base::Optional<base::TimeDelta> NetworkQualityEstimator::GetHttpRTT() const {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (network_quality_.http_rtt() == nqe::internal::InvalidRTT())
    return base::Optional<base::TimeDelta>();
  return network_quality_.http_rtt();
}

base::Optional<base::TimeDelta> NetworkQualityEstimator::GetTransportRTT()
    const {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (network_quality_.transport_rtt() == nqe::internal::InvalidRTT())
    return base::Optional<base::TimeDelta>();
  return network_quality_.transport_rtt();
}

base::Optional<int32_t> NetworkQualityEstimator::GetDownstreamThroughputKbps()
    const {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (network_quality_.downstream_throughput_kbps() ==
      nqe::internal::INVALID_RTT_THROUGHPUT) {
    return base::Optional<int32_t>();
  }
  return network_quality_.downstream_throughput_kbps();
}

base::Optional<int32_t> NetworkQualityEstimator::GetBandwidthDelayProductKbits()
    const {
  DCHECK(thread_checker_.CalledOnValidThread());
  return bandwidth_delay_product_kbits_;
}

void NetworkQualityEstimator::MaybeUpdateCachedEstimateApplied(
    const Observation& observation,
    ObservationBuffer* buffer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (observation.source() !=
          NETWORK_QUALITY_OBSERVATION_SOURCE_HTTP_CACHED_ESTIMATE &&
      observation.source() !=
          NETWORK_QUALITY_OBSERVATION_SOURCE_TRANSPORT_CACHED_ESTIMATE) {
    return;
  }

  cached_estimate_applied_ = true;
  bool deleted_observation_sources[NETWORK_QUALITY_OBSERVATION_SOURCE_MAX] = {
      false};
  deleted_observation_sources
      [NETWORK_QUALITY_OBSERVATION_SOURCE_DEFAULT_HTTP_FROM_PLATFORM] = true;
  deleted_observation_sources
      [NETWORK_QUALITY_OBSERVATION_SOURCE_DEFAULT_TRANSPORT_FROM_PLATFORM] =
          true;

  buffer->RemoveObservationsWithSource(deleted_observation_sources);
}

bool NetworkQualityEstimator::ShouldAddObservation(
    const Observation& observation) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (cached_estimate_applied_ &&
      (observation.source() ==
           NETWORK_QUALITY_OBSERVATION_SOURCE_DEFAULT_HTTP_FROM_PLATFORM ||
       observation.source() ==
           NETWORK_QUALITY_OBSERVATION_SOURCE_DEFAULT_TRANSPORT_FROM_PLATFORM)) {
    return false;
  }
  return true;
}

bool NetworkQualityEstimator::ShouldSocketWatcherNotifyRTT(
    base::TimeTicks now) {
  DCHECK(thread_checker_.CalledOnValidThread());
  return (now - last_socket_watcher_rtt_notification_ >=
          params_->socket_watchers_min_notification_interval());
}

void NetworkQualityEstimator::SimulateNetworkQualityChangeForTesting(
    net::EffectiveConnectionType type) {
  DCHECK(thread_checker_.CalledOnValidThread());
  params_->SetForcedEffectiveConnectionTypeForTesting(type);
  ComputeEffectiveConnectionType();
}

}  // namespace net
