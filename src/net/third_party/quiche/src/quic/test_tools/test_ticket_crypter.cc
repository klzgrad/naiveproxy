// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/test_ticket_crypter.h"

#include <cstring>

#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

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

size_t TestTicketCrypter::MaxOverhead() {
  return QUICHE_ARRAYSIZE(kTicketPrefix);
}

std::vector<uint8_t> TestTicketCrypter::Encrypt(quiche::QuicheStringPiece in) {
  size_t prefix_len = QUICHE_ARRAYSIZE(kTicketPrefix);
  std::vector<uint8_t> out(prefix_len + in.size());
  memcpy(out.data(), kTicketPrefix, prefix_len);
  memcpy(out.data() + prefix_len, in.data(), in.size());
  return out;
}

std::vector<uint8_t> TestTicketCrypter::Decrypt(quiche::QuicheStringPiece in) {
  size_t prefix_len = QUICHE_ARRAYSIZE(kTicketPrefix);
  if (fail_decrypt_ || in.size() < prefix_len ||
      memcmp(kTicketPrefix, in.data(), prefix_len) != 0) {
    return std::vector<uint8_t>();
  }
  return std::vector<uint8_t>(in.begin() + prefix_len, in.end());
}

void TestTicketCrypter::Decrypt(
    quiche::QuicheStringPiece in,
    std::unique_ptr<ProofSource::DecryptCallback> callback) {
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
