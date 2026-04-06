// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_TEST_TICKET_CRYPTER_H_
#define QUICHE_QUIC_TEST_TOOLS_TEST_TICKET_CRYPTER_H_

#include "quiche/quic/core/crypto/proof_source.h"

namespace quic {
namespace test {

// Provides a simple implementation of ProofSource::TicketCrypter for testing.
// THIS IMPLEMENTATION IS NOT SECURE. It is only intended for testing purposes.
class TestTicketCrypter : public ProofSource::TicketCrypter {
 public:
  TestTicketCrypter();
  ~TestTicketCrypter() override = default;

  // TicketCrypter interface
  size_t MaxOverhead() override;
  std::vector<uint8_t> Encrypt(absl::string_view in,
                               absl::string_view encryption_key) override;
  void Decrypt(absl::string_view in,
               std::shared_ptr<ProofSource::DecryptCallback> callback) override;

  void SetRunCallbacksAsync(bool run_async);
  size_t NumPendingCallbacks();
  void RunPendingCallback(size_t n);

  // Allows configuring this TestTicketCrypter to fail decryption.
  void set_fail_decrypt(bool fail_decrypt) { fail_decrypt_ = fail_decrypt; }
  void set_fail_encrypt(bool fail_encrypt) { fail_encrypt_ = fail_encrypt; }

 private:
  // Performs the Decrypt operation synchronously.
  std::vector<uint8_t> Decrypt(absl::string_view in);

  struct PendingCallback {
    std::shared_ptr<ProofSource::DecryptCallback> callback;
    std::vector<uint8_t> decrypted_ticket;
  };

  bool fail_decrypt_ = false;
  bool fail_encrypt_ = false;
  bool run_async_ = false;
  std::vector<PendingCallback> pending_callbacks_;
  std::vector<uint8_t> ticket_prefix_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_TEST_TICKET_CRYPTER_H_
