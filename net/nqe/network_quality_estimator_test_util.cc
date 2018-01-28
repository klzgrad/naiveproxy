// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/nqe/network_quality_estimator_test_util.h"

#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "net/base/load_flags.h"
#include "net/log/net_log_with_source.h"
#include "net/log/test_net_log_entry.h"
#include "net/nqe/network_quality_estimator_params.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_test_util.h"

namespace {

const base::FilePath::CharType kTestFilePath[] =
    FILE_PATH_LITERAL("net/data/url_request_unittest");

}  // namespace

namespace net {

TestNetworkQualityEstimator::TestNetworkQualityEstimator()
    : TestNetworkQualityEstimator(std::map<std::string, std::string>()) {}

TestNetworkQualityEstimator::TestNetworkQualityEstimator(
    const std::map<std::string, std::string>& variation_params)
    : TestNetworkQualityEstimator(variation_params,
                                  std::unique_ptr<ExternalEstimateProvider>()) {
}

TestNetworkQualityEstimator::TestNetworkQualityEstimator(
    const std::map<std::string, std::string>& variation_params,
    std::unique_ptr<net::ExternalEstimateProvider> external_estimate_provider)
    : TestNetworkQualityEstimator(std::move(external_estimate_provider),
                                  variation_params,
                                  true,
                                  true,
                                  false,
                                  std::make_unique<BoundTestNetLog>()) {}

TestNetworkQualityEstimator::TestNetworkQualityEstimator(
    std::unique_ptr<net::ExternalEstimateProvider> external_estimate_provider,
    const std::map<std::string, std::string>& variation_params,
    bool allow_local_host_requests_for_tests,
    bool allow_smaller_responses_for_tests,
    bool add_default_platform_observations,
    std::unique_ptr<BoundTestNetLog> net_log)
    : TestNetworkQualityEstimator(std::move(external_estimate_provider),
                                  variation_params,
                                  allow_local_host_requests_for_tests,
                                  allow_smaller_responses_for_tests,
                                  add_default_platform_observations,
                                  false,
                                  std::move(net_log)) {}

TestNetworkQualityEstimator::TestNetworkQualityEstimator(
    std::unique_ptr<net::ExternalEstimateProvider> external_estimate_provider,
    const std::map<std::string, std::string>& variation_params,
    bool allow_local_host_requests_for_tests,
    bool allow_smaller_responses_for_tests,
    bool add_default_platform_observations,
    bool suppress_notifications_for_testing,
    std::unique_ptr<BoundTestNetLog> net_log)
    : NetworkQualityEstimator(
          std::move(external_estimate_provider),
          std::make_unique<NetworkQualityEstimatorParams>(variation_params),
          net_log->bound().net_log()),

      current_network_type_(NetworkChangeNotifier::CONNECTION_UNKNOWN),
      accuracy_recording_intervals_set_(false),
      rand_double_(0.0),
      embedded_test_server_(base::FilePath(kTestFilePath)),
      suppress_notifications_for_testing_(suppress_notifications_for_testing),
      net_log_(std::move(net_log)) {
  SetUseLocalHostRequestsForTesting(allow_local_host_requests_for_tests);
  SetUseSmallResponsesForTesting(allow_smaller_responses_for_tests);
  SetAddDefaultPlatformObservationsForTesting(
      add_default_platform_observations);

  // Set up the embedded test server.
  EXPECT_TRUE(embedded_test_server_.Start());
}

TestNetworkQualityEstimator::TestNetworkQualityEstimator(
    std::unique_ptr<NetworkQualityEstimatorParams> params)
    : TestNetworkQualityEstimator(std::move(params),
                                  std::make_unique<BoundTestNetLog>()) {}

TestNetworkQualityEstimator::TestNetworkQualityEstimator(
    std::unique_ptr<NetworkQualityEstimatorParams> params,
    std::unique_ptr<BoundTestNetLog> net_log)
    : NetworkQualityEstimator(std::unique_ptr<ExternalEstimateProvider>(),
                              std::move(params),
                              net_log->bound().net_log()),
      current_network_type_(NetworkChangeNotifier::CONNECTION_UNKNOWN),
      accuracy_recording_intervals_set_(false),
      rand_double_(0.0),
      embedded_test_server_(base::FilePath(kTestFilePath)),
      suppress_notifications_for_testing_(false),
      net_log_(std::move(net_log)) {
  // Set up the embedded test server.
  EXPECT_TRUE(embedded_test_server_.Start());
}

TestNetworkQualityEstimator::~TestNetworkQualityEstimator() {}

void TestNetworkQualityEstimator::RunOneRequest() {
  TestDelegate test_delegate;
  TestURLRequestContext context(true);
  context.set_network_quality_estimator(this);
  context.Init();
  std::unique_ptr<URLRequest> request(
      context.CreateRequest(GetEchoURL(), DEFAULT_PRIORITY, &test_delegate,
                            TRAFFIC_ANNOTATION_FOR_TESTS));
  request->SetLoadFlags(request->load_flags() | LOAD_MAIN_FRAME_DEPRECATED);
  request->Start();
  base::RunLoop().Run();
}

void TestNetworkQualityEstimator::SimulateNetworkChange(
    NetworkChangeNotifier::ConnectionType new_connection_type,
    const std::string& network_id) {
  current_network_type_ = new_connection_type;
  current_network_id_ = network_id;
  OnConnectionTypeChanged(new_connection_type);
}

const GURL TestNetworkQualityEstimator::GetEchoURL() const {
  return embedded_test_server_.GetURL("/simple.html");
}

const GURL TestNetworkQualityEstimator::GetRedirectURL() const {
  return embedded_test_server_.GetURL("/redirect302-to-https");
}

EffectiveConnectionType
TestNetworkQualityEstimator::GetEffectiveConnectionType() const {
  if (effective_connection_type_)
    return effective_connection_type_.value();
  return NetworkQualityEstimator::GetEffectiveConnectionType();
}

EffectiveConnectionType
TestNetworkQualityEstimator::GetRecentEffectiveConnectionType(
    const base::TimeTicks& start_time) const {
  if (recent_effective_connection_type_)
    return recent_effective_connection_type_.value();
  return NetworkQualityEstimator::GetRecentEffectiveConnectionType(start_time);
}

EffectiveConnectionType
TestNetworkQualityEstimator::GetRecentEffectiveConnectionTypeAndNetworkQuality(
    const base::TimeTicks& start_time,
    base::TimeDelta* http_rtt,
    base::TimeDelta* transport_rtt,
    int32_t* downstream_throughput_kbps) const {
  if (recent_effective_connection_type_) {
    GetRecentHttpRTT(start_time, http_rtt);
    GetRecentTransportRTT(start_time, transport_rtt);
    GetRecentDownlinkThroughputKbps(start_time, downstream_throughput_kbps);
    return recent_effective_connection_type_.value();
  }
  return NetworkQualityEstimator::
      GetRecentEffectiveConnectionTypeAndNetworkQuality(
          start_time, http_rtt, transport_rtt, downstream_throughput_kbps);
}

bool TestNetworkQualityEstimator::GetRecentHttpRTT(
    const base::TimeTicks& start_time,
    base::TimeDelta* rtt) const {
  if (start_time.is_null()) {
    if (start_time_null_http_rtt_) {
      *rtt = start_time_null_http_rtt_.value();
      return true;
    }
    return NetworkQualityEstimator::GetRecentHttpRTT(start_time, rtt);
  }
  if (recent_http_rtt_) {
    *rtt = recent_http_rtt_.value();
    return true;
  }
  return NetworkQualityEstimator::GetRecentHttpRTT(start_time, rtt);
}

bool TestNetworkQualityEstimator::GetRecentTransportRTT(
    const base::TimeTicks& start_time,
    base::TimeDelta* rtt) const {
  if (start_time.is_null()) {
    if (start_time_null_transport_rtt_) {
      *rtt = start_time_null_transport_rtt_.value();
      return true;
    }
    return NetworkQualityEstimator::GetRecentTransportRTT(start_time, rtt);
  }

  if (recent_transport_rtt_) {
    *rtt = recent_transport_rtt_.value();
    return true;
  }
  return NetworkQualityEstimator::GetRecentTransportRTT(start_time, rtt);
}

base::Optional<base::TimeDelta> TestNetworkQualityEstimator::GetTransportRTT()
    const {
  if (start_time_null_transport_rtt_)
    return start_time_null_transport_rtt_;
  return NetworkQualityEstimator::GetTransportRTT();
}

bool TestNetworkQualityEstimator::GetRecentDownlinkThroughputKbps(
    const base::TimeTicks& start_time,
    int32_t* kbps) const {
  if (start_time.is_null()) {
    if (start_time_null_downlink_throughput_kbps_) {
      *kbps = start_time_null_downlink_throughput_kbps_.value();
      return true;
    }
    return NetworkQualityEstimator::GetRecentDownlinkThroughputKbps(start_time,
                                                                    kbps);
  }

  if (recent_downlink_throughput_kbps_) {
    *kbps = recent_downlink_throughput_kbps_.value();
    return true;
  }
  return NetworkQualityEstimator::GetRecentDownlinkThroughputKbps(start_time,
                                                                  kbps);
}

base::TimeDelta TestNetworkQualityEstimator::GetRTTEstimateInternal(
    const std::vector<NetworkQualityObservationSource>&
        disallowed_observation_sources,
    base::TimeTicks start_time,
    const base::Optional<NetworkQualityEstimator::Statistic>& statistic,
    int percentile) const {
  if (rtt_estimate_internal_)
    return rtt_estimate_internal_.value();

  return NetworkQualityEstimator::GetRTTEstimateInternal(
      disallowed_observation_sources, start_time, statistic, percentile);
}

void TestNetworkQualityEstimator::SetAccuracyRecordingIntervals(
    const std::vector<base::TimeDelta>& accuracy_recording_intervals) {
  accuracy_recording_intervals_set_ = true;
  accuracy_recording_intervals_ = accuracy_recording_intervals;
}

const std::vector<base::TimeDelta>&
TestNetworkQualityEstimator::GetAccuracyRecordingIntervals() const {
  if (accuracy_recording_intervals_set_)
    return accuracy_recording_intervals_;

  return NetworkQualityEstimator::GetAccuracyRecordingIntervals();
}

double TestNetworkQualityEstimator::RandDouble() const {
  return rand_double_;
}

base::Optional<int32_t>
TestNetworkQualityEstimator::GetBandwidthDelayProductKbits() const {
  if (bandwidth_delay_product_kbits_.has_value())
    return bandwidth_delay_product_kbits_.value();
  return NetworkQualityEstimator::GetBandwidthDelayProductKbits();
}

int TestNetworkQualityEstimator::GetEntriesCount(NetLogEventType type) const {
  TestNetLogEntry::List entries;
  net_log_->GetEntries(&entries);

  int count = 0;
  for (const auto& entry : entries) {
    if (entry.type == type)
      ++count;
  }
  return count;
}

std::string TestNetworkQualityEstimator::GetNetLogLastStringValue(
    NetLogEventType type,
    const std::string& key) const {
  std::string return_value;
  TestNetLogEntry::List entries;
  net_log_->GetEntries(&entries);

  for (int i = entries.size() - 1; i >= 0; --i) {
    if (entries[i].type == type &&
        entries[i].GetStringValue(key, &return_value)) {
      return return_value;
    }
  }
  return return_value;
}

int TestNetworkQualityEstimator::GetNetLogLastIntegerValue(
    NetLogEventType type,
    const std::string& key) const {
  int return_value = 0;
  TestNetLogEntry::List entries;
  net_log_->GetEntries(&entries);

  for (int i = entries.size() - 1; i >= 0; --i) {
    if (entries[i].type == type &&
        entries[i].GetIntegerValue(key, &return_value)) {
      return return_value;
    }
  }
  return return_value;
}

void TestNetworkQualityEstimator::
    NotifyObserversOfRTTOrThroughputEstimatesComputed(
        const net::nqe::internal::NetworkQuality& network_quality) {
  for (auto& observer : rtt_and_throughput_estimates_observer_list_) {
    observer.OnRTTOrThroughputEstimatesComputed(
        network_quality.http_rtt(), network_quality.transport_rtt(),
        network_quality.downstream_throughput_kbps());
  }
}

void TestNetworkQualityEstimator::NotifyObserversOfEffectiveConnectionType(
    EffectiveConnectionType type) {
  for (auto& observer : effective_connection_type_observer_list_)
    observer.OnEffectiveConnectionTypeChanged(type);
}

nqe::internal::NetworkID TestNetworkQualityEstimator::GetCurrentNetworkID()
    const {
  return nqe::internal::NetworkID(current_network_type_, current_network_id_);
}

TestNetworkQualityEstimator::LocalHttpTestServer::LocalHttpTestServer(
    const base::FilePath& document_root) {
  AddDefaultHandlers(document_root);
}

void TestNetworkQualityEstimator::NotifyObserversOfRTTOrThroughputComputed()
    const {
  if (suppress_notifications_for_testing_)
    return;

  NetworkQualityEstimator::NotifyObserversOfRTTOrThroughputComputed();
}

void TestNetworkQualityEstimator::
    NotifyRTTAndThroughputEstimatesObserverIfPresent(
        RTTAndThroughputEstimatesObserver* observer) const {
  if (suppress_notifications_for_testing_)
    return;

  NetworkQualityEstimator::NotifyRTTAndThroughputEstimatesObserverIfPresent(
      observer);
}

}  // namespace net
