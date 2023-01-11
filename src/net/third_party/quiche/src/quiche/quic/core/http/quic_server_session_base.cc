// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_server_session_base.h"

#include <string>

#include "quiche/quic/core/proto/cached_network_parameters_proto.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_tag.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

QuicServerSessionBase::QuicServerSessionBase(
    const QuicConfig& config, const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection, Visitor* visitor,
    QuicCryptoServerStreamBase::Helper* helper,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache)
    : QuicSpdySession(connection, visitor, config, supported_versions),
      crypto_config_(crypto_config),
      compressed_certs_cache_(compressed_certs_cache),
      helper_(helper),
      bandwidth_resumption_enabled_(false),
      bandwidth_estimate_sent_to_client_(QuicBandwidth::Zero()),
      last_scup_time_(QuicTime::Zero()) {}

QuicServerSessionBase::~QuicServerSessionBase() {}

void QuicServerSessionBase::Initialize() {
  crypto_stream_ =
      CreateQuicCryptoServerStream(crypto_config_, compressed_certs_cache_);
  QuicSpdySession::Initialize();
  SendSettingsToCryptoStream();
}

void QuicServerSessionBase::OnConfigNegotiated() {
  QuicSpdySession::OnConfigNegotiated();

  const CachedNetworkParameters* cached_network_params =
      crypto_stream_->PreviousCachedNetworkParams();

  // Set the initial rtt from cached_network_params.min_rtt_ms, which comes from
  // a validated address token. This will override the initial rtt that may have
  // been set by the transport parameters.
  if (version().UsesTls() && cached_network_params != nullptr) {
    if (cached_network_params->serving_region() == serving_region_) {
      QUIC_CODE_COUNT(quic_server_received_network_params_at_same_region);
      if (config()->HasReceivedConnectionOptions() &&
          ContainsQuicTag(config()->ReceivedConnectionOptions(), kTRTT)) {
        QUIC_DLOG(INFO)
            << "Server: Setting initial rtt to "
            << cached_network_params->min_rtt_ms()
            << "ms which is received from a validated address token";
        connection()->sent_packet_manager().SetInitialRtt(
            QuicTime::Delta::FromMilliseconds(
                cached_network_params->min_rtt_ms()),
            /*trusted=*/true);
      }
    } else {
      QUIC_CODE_COUNT(quic_server_received_network_params_at_different_region);
    }
  }

  if (!config()->HasReceivedConnectionOptions()) {
    return;
  }

  if (GetQuicReloadableFlag(quic_enable_disable_resumption) &&
      version().UsesTls() &&
      ContainsQuicTag(config()->ReceivedConnectionOptions(), kNRES) &&
      crypto_stream_->ResumptionAttempted()) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_enable_disable_resumption);
    const bool disabled = crypto_stream_->DisableResumption();
    QUIC_BUG_IF(quic_failed_to_disable_resumption, !disabled)
        << "Failed to disable resumption";
  }

  enable_sending_bandwidth_estimate_when_network_idle_ =
      GetQuicRestartFlag(
          quic_enable_sending_bandwidth_estimate_when_network_idle_v2) &&
      version().HasIetfQuicFrames() &&
      ContainsQuicTag(config()->ReceivedConnectionOptions(), kBWID);

  // Enable bandwidth resumption if peer sent correct connection options.
  const bool last_bandwidth_resumption =
      ContainsQuicTag(config()->ReceivedConnectionOptions(), kBWRE);
  const bool max_bandwidth_resumption =
      ContainsQuicTag(config()->ReceivedConnectionOptions(), kBWMX);
  bandwidth_resumption_enabled_ =
      last_bandwidth_resumption || max_bandwidth_resumption;

  // If the client has provided a bandwidth estimate from the same serving
  // region as this server, then decide whether to use the data for bandwidth
  // resumption.
  if (cached_network_params != nullptr &&
      cached_network_params->serving_region() == serving_region_) {
    if (!version().UsesTls()) {
      // Log the received connection parameters, regardless of how they
      // get used for bandwidth resumption.
      connection()->OnReceiveConnectionState(*cached_network_params);
    }

    if (bandwidth_resumption_enabled_) {
      // Only do bandwidth resumption if estimate is recent enough.
      const uint64_t seconds_since_estimate =
          connection()->clock()->WallNow().ToUNIXSeconds() -
          cached_network_params->timestamp();
      if (seconds_since_estimate <= kNumSecondsPerHour) {
        connection()->ResumeConnectionState(*cached_network_params,
                                            max_bandwidth_resumption);
      }
    }
  }
}

void QuicServerSessionBase::OnConnectionClosed(
    const QuicConnectionCloseFrame& frame, ConnectionCloseSource source) {
  QuicSession::OnConnectionClosed(frame, source);
  // In the unlikely event we get a connection close while doing an asynchronous
  // crypto event, make sure we cancel the callback.
  if (crypto_stream_ != nullptr) {
    crypto_stream_->CancelOutstandingCallbacks();
  }
}

void QuicServerSessionBase::OnBandwidthUpdateTimeout() {
  if (!enable_sending_bandwidth_estimate_when_network_idle_) {
    return;
  }
  QUIC_DVLOG(1) << "Bandwidth update timed out.";
  const SendAlgorithmInterface* send_algorithm =
      connection()->sent_packet_manager().GetSendAlgorithm();
  if (send_algorithm != nullptr &&
      send_algorithm->HasGoodBandwidthEstimateForResumption()) {
    const bool success = MaybeSendAddressToken();
    QUIC_BUG_IF(QUIC_BUG_25522, !success) << "Failed to send address token.";
    QUIC_RESTART_FLAG_COUNT_N(
        quic_enable_sending_bandwidth_estimate_when_network_idle_v2, 2, 3);
  }
}

void QuicServerSessionBase::OnCongestionWindowChange(QuicTime now) {
  // Sending bandwidth is no longer conditioned on if session does bandwidth
  // resumption.
  if (GetQuicRestartFlag(
          quic_enable_sending_bandwidth_estimate_when_network_idle_v2)) {
    QUIC_RESTART_FLAG_COUNT_N(
        quic_enable_sending_bandwidth_estimate_when_network_idle_v2, 3, 3);
    return;
  }
  if (!bandwidth_resumption_enabled_) {
    return;
  }
  // Only send updates when the application has no data to write.
  if (HasDataToWrite()) {
    return;
  }

  // If not enough time has passed since the last time we sent an update to the
  // client, or not enough packets have been sent, then return early.
  const QuicSentPacketManager& sent_packet_manager =
      connection()->sent_packet_manager();
  int64_t srtt_ms =
      sent_packet_manager.GetRttStats()->smoothed_rtt().ToMilliseconds();
  int64_t now_ms = (now - last_scup_time_).ToMilliseconds();
  int64_t packets_since_last_scup = 0;
  const QuicPacketNumber largest_sent_packet =
      connection()->sent_packet_manager().GetLargestSentPacket();
  if (largest_sent_packet.IsInitialized()) {
    packets_since_last_scup =
        last_scup_packet_number_.IsInitialized()
            ? largest_sent_packet - last_scup_packet_number_
            : largest_sent_packet.ToUint64();
  }
  if (now_ms < (kMinIntervalBetweenServerConfigUpdatesRTTs * srtt_ms) ||
      now_ms < kMinIntervalBetweenServerConfigUpdatesMs ||
      packets_since_last_scup < kMinPacketsBetweenServerConfigUpdates) {
    return;
  }

  // If the bandwidth recorder does not have a valid estimate, return early.
  const QuicSustainedBandwidthRecorder* bandwidth_recorder =
      sent_packet_manager.SustainedBandwidthRecorder();
  if (bandwidth_recorder == nullptr || !bandwidth_recorder->HasEstimate()) {
    return;
  }

  // The bandwidth recorder has recorded at least one sustained bandwidth
  // estimate. Check that it's substantially different from the last one that
  // we sent to the client, and if so, send the new one.
  QuicBandwidth new_bandwidth_estimate =
      bandwidth_recorder->BandwidthEstimate();

  int64_t bandwidth_delta =
      std::abs(new_bandwidth_estimate.ToBitsPerSecond() -
               bandwidth_estimate_sent_to_client_.ToBitsPerSecond());

  // Define "substantial" difference as a 50% increase or decrease from the
  // last estimate.
  bool substantial_difference =
      bandwidth_delta >
      0.5 * bandwidth_estimate_sent_to_client_.ToBitsPerSecond();
  if (!substantial_difference) {
    return;
  }

  if (version().UsesTls()) {
    if (version().HasIetfQuicFrames() && MaybeSendAddressToken()) {
      bandwidth_estimate_sent_to_client_ = new_bandwidth_estimate;
    }
  } else {
    absl::optional<CachedNetworkParameters> cached_network_params =
        GenerateCachedNetworkParameters();

    if (cached_network_params.has_value()) {
      bandwidth_estimate_sent_to_client_ = new_bandwidth_estimate;
      QUIC_DVLOG(1) << "Server: sending new bandwidth estimate (KBytes/s): "
                    << bandwidth_estimate_sent_to_client_.ToKBytesPerSecond();

      QUICHE_DCHECK_EQ(
          BandwidthToCachedParameterBytesPerSecond(
              bandwidth_estimate_sent_to_client_),
          cached_network_params->bandwidth_estimate_bytes_per_second());

      crypto_stream_->SendServerConfigUpdate(&cached_network_params.value());

      connection()->OnSendConnectionState(*cached_network_params);
    }
  }

  last_scup_time_ = now;
  last_scup_packet_number_ =
      connection()->sent_packet_manager().GetLargestSentPacket();
}

bool QuicServerSessionBase::ShouldCreateIncomingStream(QuicStreamId id) {
  if (!connection()->connected()) {
    QUIC_BUG(quic_bug_10393_2)
        << "ShouldCreateIncomingStream called when disconnected";
    return false;
  }

  if (QuicUtils::IsServerInitiatedStreamId(transport_version(), id)) {
    QUIC_BUG(quic_bug_10393_3)
        << "ShouldCreateIncomingStream called with server initiated "
           "stream ID.";
    return false;
  }

  if (QuicUtils::IsServerInitiatedStreamId(transport_version(), id)) {
    QUIC_DLOG(INFO) << "Invalid incoming even stream_id:" << id;
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Client created even numbered stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  return true;
}

bool QuicServerSessionBase::ShouldCreateOutgoingBidirectionalStream() {
  if (!connection()->connected()) {
    QUIC_BUG(quic_bug_12513_2)
        << "ShouldCreateOutgoingBidirectionalStream called when disconnected";
    return false;
  }
  if (!crypto_stream_->encryption_established()) {
    QUIC_BUG(quic_bug_10393_4)
        << "Encryption not established so no outgoing stream created.";
    return false;
  }

  return CanOpenNextOutgoingBidirectionalStream();
}

bool QuicServerSessionBase::ShouldCreateOutgoingUnidirectionalStream() {
  if (!connection()->connected()) {
    QUIC_BUG(quic_bug_12513_3)
        << "ShouldCreateOutgoingUnidirectionalStream called when disconnected";
    return false;
  }
  if (!crypto_stream_->encryption_established()) {
    QUIC_BUG(quic_bug_10393_5)
        << "Encryption not established so no outgoing stream created.";
    return false;
  }

  return CanOpenNextOutgoingUnidirectionalStream();
}

QuicCryptoServerStreamBase* QuicServerSessionBase::GetMutableCryptoStream() {
  return crypto_stream_.get();
}

const QuicCryptoServerStreamBase* QuicServerSessionBase::GetCryptoStream()
    const {
  return crypto_stream_.get();
}

int32_t QuicServerSessionBase::BandwidthToCachedParameterBytesPerSecond(
    const QuicBandwidth& bandwidth) const {
  return static_cast<int32_t>(std::min<int64_t>(
      bandwidth.ToBytesPerSecond(), std::numeric_limits<int32_t>::max()));
}

void QuicServerSessionBase::SendSettingsToCryptoStream() {
  if (!version().UsesTls()) {
    return;
  }
  std::string settings_frame = HttpEncoder::SerializeSettingsFrame(settings());

  std::unique_ptr<ApplicationState> serialized_settings =
      std::make_unique<ApplicationState>(
          settings_frame.data(),
          settings_frame.data() + settings_frame.length());
  GetMutableCryptoStream()->SetServerApplicationStateForResumption(
      std::move(serialized_settings));
}

QuicSSLConfig QuicServerSessionBase::GetSSLConfig() const {
  QUICHE_DCHECK(crypto_config_ && crypto_config_->proof_source());

  QuicSSLConfig ssl_config = QuicSpdySession::GetSSLConfig();

  ssl_config.disable_ticket_support =
      GetQuicFlag(quic_disable_server_tls_resumption);

  if (!crypto_config_ || !crypto_config_->proof_source()) {
    return ssl_config;
  }

  absl::InlinedVector<uint16_t, 8> signature_algorithms =
      crypto_config_->proof_source()->SupportedTlsSignatureAlgorithms();
  if (!signature_algorithms.empty()) {
    ssl_config.signing_algorithm_prefs = std::move(signature_algorithms);
  }

  return ssl_config;
}

absl::optional<CachedNetworkParameters>
QuicServerSessionBase::GenerateCachedNetworkParameters() const {
  const QuicSentPacketManager& sent_packet_manager =
      connection()->sent_packet_manager();
  const QuicSustainedBandwidthRecorder* bandwidth_recorder =
      sent_packet_manager.SustainedBandwidthRecorder();

  CachedNetworkParameters cached_network_params;
  cached_network_params.set_timestamp(
      connection()->clock()->WallNow().ToUNIXSeconds());

  if (!sent_packet_manager.GetRttStats()->min_rtt().IsZero()) {
    cached_network_params.set_min_rtt_ms(
        sent_packet_manager.GetRttStats()->min_rtt().ToMilliseconds());
  }

  if (enable_sending_bandwidth_estimate_when_network_idle_) {
    const SendAlgorithmInterface* send_algorithm =
        sent_packet_manager.GetSendAlgorithm();
    if (send_algorithm != nullptr &&
        send_algorithm->HasGoodBandwidthEstimateForResumption()) {
      cached_network_params.set_bandwidth_estimate_bytes_per_second(
          BandwidthToCachedParameterBytesPerSecond(
              send_algorithm->BandwidthEstimate()));
      QUIC_CODE_COUNT(quic_send_measured_bandwidth_in_token);
    } else {
      const quic::CachedNetworkParameters* previous_cached_network_params =
          crypto_stream()->PreviousCachedNetworkParams();
      if (previous_cached_network_params != nullptr &&
          previous_cached_network_params
                  ->bandwidth_estimate_bytes_per_second() > 0) {
        cached_network_params.set_bandwidth_estimate_bytes_per_second(
            previous_cached_network_params
                ->bandwidth_estimate_bytes_per_second());
        QUIC_CODE_COUNT(quic_send_previous_bandwidth_in_token);
      } else {
        QUIC_CODE_COUNT(quic_not_send_bandwidth_in_token);
      }
    }
  } else {
    // Populate bandwidth estimates if any.
    if (bandwidth_recorder != nullptr && bandwidth_recorder->HasEstimate()) {
      const int32_t bw_estimate_bytes_per_second =
          BandwidthToCachedParameterBytesPerSecond(
              bandwidth_recorder->BandwidthEstimate());
      const int32_t max_bw_estimate_bytes_per_second =
          BandwidthToCachedParameterBytesPerSecond(
              bandwidth_recorder->MaxBandwidthEstimate());
      QUIC_BUG_IF(quic_bug_12513_1, max_bw_estimate_bytes_per_second < 0)
          << max_bw_estimate_bytes_per_second;
      QUIC_BUG_IF(quic_bug_10393_1, bw_estimate_bytes_per_second < 0)
          << bw_estimate_bytes_per_second;

      cached_network_params.set_bandwidth_estimate_bytes_per_second(
          bw_estimate_bytes_per_second);
      cached_network_params.set_max_bandwidth_estimate_bytes_per_second(
          max_bw_estimate_bytes_per_second);
      cached_network_params.set_max_bandwidth_timestamp_seconds(
          bandwidth_recorder->MaxBandwidthTimestamp());

      cached_network_params.set_previous_connection_state(
          bandwidth_recorder->EstimateRecordedDuringSlowStart()
              ? CachedNetworkParameters::SLOW_START
              : CachedNetworkParameters::CONGESTION_AVOIDANCE);
    }
  }

  if (!serving_region_.empty()) {
    cached_network_params.set_serving_region(serving_region_);
  }

  return cached_network_params;
}

}  // namespace quic
