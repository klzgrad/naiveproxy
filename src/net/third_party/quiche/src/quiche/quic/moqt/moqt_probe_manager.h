// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_PROBE_MANAGER_H_
#define QUICHE_QUIC_MOQT_MOQT_PROBE_MANAGER_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// ID of a probe.
using ProbeId = uint64_t;

// Potential outcomes of a proge.
enum class ProbeStatus : uint8_t {
  // Probe has finished successfully.
  kSuccess,
  // Probe has timed out.
  kTimeout,
  // Probe has been aborted, via a STOP_SENDING or for some other reason.
  kAborted,
};

// Represents the results of a probe.
struct ProbeResult {
  ProbeId id;
  ProbeStatus status;
  // The number of bytes requested on the probe.
  quic::QuicByteCount probe_size;
  // Time elapsed between the time the probe was requested and now.
  quic::QuicTimeDelta time_elapsed;
};

// Interface used to mock out MoqtProbeManager.
class MoqtProbeManagerInterface {
 public:
  using Callback = quiche::SingleUseCallback<void(const ProbeResult& result)>;

  virtual ~MoqtProbeManagerInterface() = default;

  // Starts the probe.  Returns the ID of the probe, or nullopt if the probe
  // cannot be started.  Will fail if a probe is already pending.
  virtual std::optional<ProbeId> StartProbe(quic::QuicByteCount probe_size,
                                            quic::QuicTimeDelta timeout,
                                            Callback callback) = 0;
  // Cancels the currently pending probe.
  virtual std::optional<ProbeId> StopProbe() = 0;
};

namespace test {
class MoqtProbeManagerPeer;
}

// MoqtProbeManager keeps track of the pending bandwidth probe, including
// ensuring there is only one probe pending, and handling the timeout.
class MoqtProbeManager : public MoqtProbeManagerInterface {
 public:
  explicit MoqtProbeManager(webtransport::Session* session,
                            const quic::QuicClock* clock,
                            quic::QuicAlarmFactory& alarm_factory)
      : session_(session),
        clock_(clock),
        timeout_alarm_(alarm_factory.CreateAlarm(new AlarmDelegate(this))) {}

  // MoqtProbeManagerInterface implementation.
  std::optional<ProbeId> StartProbe(quic::QuicByteCount probe_size,
                                    quic::QuicTimeDelta timeout,
                                    Callback callback) override;
  std::optional<ProbeId> StopProbe() override;

 private:
  friend class ::moqt::test::MoqtProbeManagerPeer;

  struct PendingProbe {
    ProbeId id;
    quic::QuicTime start;
    quic::QuicTime deadline;
    quic::QuicByteCount probe_size;
    webtransport::StreamId stream_id;
    Callback callback;
  };

  class ProbeStreamVisitor : public webtransport::StreamVisitor {
   public:
    ProbeStreamVisitor(MoqtProbeManager* manager, webtransport::Stream* stream,
                       ProbeId probe_id, quic::QuicByteCount probe_size)
        : manager_(manager),
          stream_(stream),
          probe_id_(probe_id),
          data_remaining_(probe_size) {}

    void OnCanRead() override {}
    void OnCanWrite() override;
    void OnResetStreamReceived(webtransport::StreamErrorCode error) override {}
    void OnStopSendingReceived(webtransport::StreamErrorCode error) override;
    void OnWriteSideInDataRecvdState() override;

   private:
    // Ensures the stream is associated with the currently active probe.
    bool ValidateProbe() {
      if (!manager_->probe_.has_value() || manager_->probe_->id != probe_id_) {
        // TODO: figure out the error code.
        stream_->ResetWithUserCode(0);
        return false;
      }
      return true;
    }

    MoqtProbeManager* manager_;
    webtransport::Stream* stream_;
    ProbeId probe_id_;
    bool header_sent_ = false;
    quic::QuicByteCount data_remaining_;
  };

  class AlarmDelegate : public quic::QuicAlarm::DelegateWithoutContext {
   public:
    explicit AlarmDelegate(MoqtProbeManager* manager) : manager_(manager) {}
    void OnAlarm() override { manager_->OnAlarm(); }

   private:
    MoqtProbeManager* manager_;
  };

  void RescheduleAlarm();
  void OnAlarm();
  void ClosePendingProbe(ProbeStatus status);

  std::optional<PendingProbe> probe_;
  webtransport::Session* session_;
  const quic::QuicClock* clock_;
  std::unique_ptr<quic::QuicAlarm> timeout_alarm_;
  ProbeId next_probe_id_ = 0;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_PROBE_MANAGER_H_
