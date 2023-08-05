// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/test_ticket_crypter.h"

#include <cstring>

#include "absl/base/macros.h"
#include "quiche/quic/core/crypto/quic_random.h"

namespace quic {
namespace test {

namespace {

// A TicketCrypter implementation is supposed to encrypt and decrypt session
// tickets. However, the only requirement that is needed of a test
// implementation is that calling Decrypt(Encrypt(input), callback) results in
// callback being called with input. (The output of Encrypt must also not exceed
// the overhead specified by MaxOverhead.) This test implementation encrypts
// tickets by prepending kTicketPrefix to generate the ciphertext. The decrypt
// function checks that the prefix is present and strips it; otherwise it
// returns an empty vector to signal failure.
constexpr char kTicketPrefix[] = "TEST TICKET";

}  // namespace

TestTicketCrypter::TestTicketCrypter()
    : ticket_prefix_(ABSL_ARRAYSIZE(kTicketPrefix) + 16) {
  memcpy(ticket_prefix_.data(), kTicketPrefix, ABSL_ARRAYSIZE(kTicketPrefix));
  QuicRandom::GetInstance()->RandBytes(
      ticket_prefix_.data() + ABSL_ARRAYSIZE(kTicketPrefix), 16);
}

size_t TestTicketCrypter::MaxOverhead() { return ticket_prefix_.size(); }

std::vector<uint8_t> TestTicketCrypter::Encrypt(
    absl::string_view in, absl::string_view /* encryption_key */) {
  if (fail_encrypt_) {
    return {};
  }
  size_t prefix_len = ticket_prefix_.size();
  std::vector<uint8_t> out(prefix_len + in.size());
  memcpy(out.data(), ticket_prefix_.data(), prefix_len);
  memcpy(out.data() + prefix_len, in.data(), in.size());
  return out;
}

std::vector<uint8_t> TestTicketCrypter::Decrypt(absl::string_view in) {
  size_t prefix_len = ticket_prefix_.size();
  if (fail_decrypt_ || in.size() < prefix_len ||
      memcmp(ticket_prefix_.data(), in.data(), prefix_len) != 0) {
    return std::vector<uint8_t>();
  }
  return std::vector<uint8_t>(in.begin() + prefix_len, in.end());
}

void TestTicketCrypter::Decrypt(
    absl::string_view in,
    std::shared_ptr<ProofSource::DecryptCallback> callback) {
  auto decrypted_ticket = Decrypt(in);
  if (run_async_) {
    pending_callbacks_.push_back({std::move(callback), decrypted_ticket});
  } else {
    callback->Run(decrypted_ticket);
  }
}

void TestTicketCrypter::SetRunCallbacksAsync(bool run_async) {
  run_async_ = run_async;
}

size_t TestTicketCrypter::NumPendingCallbacks() {
  return pending_callbacks_.size();
}

void TestTicketCrypter::RunPendingCallback(size_t n) {
  const PendingCallback& callback = pending_callbacks_[n];
  callback.callback->Run(callback.decrypted_ticket);
}

}  // namespace test
}  // namespace quic
