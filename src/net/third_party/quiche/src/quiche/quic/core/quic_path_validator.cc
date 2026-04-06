// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_path_validator.h"

#include <memory>
#include <ostream>
#include <utility>

#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace quic {

class RetryAlarmDelegate : public QuicAlarm::DelegateWithContext {
 public:
  explicit RetryAlarmDelegate(QuicPathValidator* path_validator,
                              QuicConnectionContext* context)
      : QuicAlarm::DelegateWithContext(context),
        path_validator_(path_validator) {}
  RetryAlarmDelegate(const RetryAlarmDelegate&) = delete;
  RetryAlarmDelegate& operator=(const RetryAlarmDelegate&) = delete;

  void OnAlarm() override { path_validator_->OnRetryTimeout(); }

 private:
  QuicPathValidator* path_validator_;
};

std::ostream& operator<<(std::ostream& os,
                         const QuicPathValidationContext& context) {
  return os << " from " << context.self_address_ << " to "
            << context.peer_address_;
}

QuicPathValidator::QuicPathValidator(QuicAlarmFactory* alarm_factory,
                                     QuicConnectionArena* arena,
                                     SendDelegate* send_delegate,
                                     QuicRandom* random, const QuicClock* clock,
                                     QuicConnectionContext* context)
    : send_delegate_(send_delegate),
      random_(random),
      clock_(clock),
      retry_timer_(alarm_factory->CreateAlarm(
          arena->New<RetryAlarmDelegate>(this, context), arena)),
      retry_count_(0u) {}

void QuicPathValidator::OnPathResponse(const QuicPathFrameBuffer& probing_data,
                                       QuicSocketAddress self_address) {
  if (!HasPendingPathValidation()) {
    return;
  }

  QUIC_DVLOG(1) << "Match PATH_RESPONSE received on " << self_address;
  QUIC_BUG_IF(quic_bug_12402_1, !path_context_->self_address().IsInitialized())
      << "Self address should have been known by now";
  if (self_address != path_context_->self_address()) {
    QUIC_DVLOG(1) << "Expect the response to be received on "
                  << path_context_->self_address();
    return;
  }
  // This iterates at most 3 times.
  for (auto it = probing_data_.begin(); it != probing_data_.end(); ++it) {
    if (it->frame_buffer == probing_data) {
      result_delegate_->OnPathValidationSuccess(std::move(path_context_),
                                                it->send_time);
      ResetPathValidation();
      return;
    }
  }
  QUIC_DVLOG(1) << "PATH_RESPONSE with payload " << probing_data.data()
                << " doesn't match the probing data.";
}

void QuicPathValidator::StartPathValidation(
    std::unique_ptr<QuicPathValidationContext> context,
    std::unique_ptr<ResultDelegate> result_delegate,
    PathValidationReason reason) {
  QUICHE_DCHECK(context);
  QUIC_DLOG(INFO) << "Start validating path " << *context
                  << " via writer: " << context->WriterToUse();
  if (path_context_ != nullptr) {
    QUIC_BUG(quic_bug_10876_1)
        << "There is an on-going validation on path " << *path_context_;
    ResetPathValidation();
  }

  reason_ = reason;
  path_context_ = std::move(context);
  result_delegate_ = std::move(result_delegate);
  SendPathChallengeAndSetAlarm();
}

void QuicPathValidator::ResetPathValidation() {
  path_context_ = nullptr;
  result_delegate_ = nullptr;
  retry_timer_->Cancel();
  retry_count_ = 0;
  reason_ = PathValidationReason::kReasonUnknown;
}

void QuicPathValidator::CancelPathValidation() {
  if (path_context_ == nullptr) {
    return;
  }
  QUIC_DVLOG(1) << "Cancel validation on path" << *path_context_;
  result_delegate_->OnPathValidationFailure(std::move(path_context_));
  ResetPathValidation();
}

bool QuicPathValidator::HasPendingPathValidation() const {
  return path_context_ != nullptr;
}

QuicPathValidationContext* QuicPathValidator::GetContext() const {
  return path_context_.get();
}

std::unique_ptr<QuicPathValidationContext> QuicPathValidator::ReleaseContext() {
  auto ret = std::move(path_context_);
  ResetPathValidation();
  return ret;
}

const QuicPathFrameBuffer& QuicPathValidator::GeneratePathChallengePayload() {
  probing_data_.emplace_back(clock_->Now());
  random_->RandBytes(probing_data_.back().frame_buffer.data(),
                     sizeof(QuicPathFrameBuffer));
  return probing_data_.back().frame_buffer;
}

void QuicPathValidator::OnRetryTimeout() {
  ++retry_count_;
  if (retry_count_ > kMaxRetryTimes) {
    CancelPathValidation();
    return;
  }
  QUIC_DVLOG(1) << "Send another PATH_CHALLENGE on path " << *path_context_;
  SendPathChallengeAndSetAlarm();
}

void QuicPathValidator::SendPathChallengeAndSetAlarm() {
  bool should_continue = send_delegate_->SendPathChallenge(
      GeneratePathChallengePayload(), path_context_->self_address(),
      path_context_->peer_address(), path_context_->effective_peer_address(),
      path_context_->WriterToUse());

  if (!should_continue) {
    // The delegate doesn't want to continue the path validation.
    CancelPathValidation();
    return;
  }
  retry_timer_->Set(send_delegate_->GetRetryTimeout(
      path_context_->peer_address(), path_context_->WriterToUse()));
}

bool QuicPathValidator::IsValidatingPeerAddress(
    const QuicSocketAddress& effective_peer_address) {
  return path_context_ != nullptr &&
         path_context_->effective_peer_address() == effective_peer_address;
}

void QuicPathValidator::MaybeWritePacketToAddress(
    const char* buffer, size_t buf_len, const QuicSocketAddress& peer_address) {
  if (!HasPendingPathValidation() ||
      path_context_->peer_address() != peer_address) {
    return;
  }
  QUIC_DVLOG(1) << "Path validator is sending packet of size " << buf_len
                << " from " << path_context_->self_address() << " to "
                << path_context_->peer_address();
  path_context_->WriterToUse()->WritePacket(
      buffer, buf_len, path_context_->self_address().host(),
      path_context_->peer_address(), nullptr, QuicPacketWriterParams());
}

}  // namespace quic
