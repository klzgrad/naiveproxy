/* Copyright (c) 2019, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#ifndef HEADER_MOCK_QUIC_TRANSPORT
#define HEADER_MOCK_QUIC_TRANSPORT

#include <openssl/base.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>

#include <vector>

class MockQuicTransport {
 public:
  explicit MockQuicTransport(bssl::UniquePtr<BIO> bio, SSL *ssl);

  bool SetSecrets(enum ssl_encryption_level_t level, const uint8_t *read_secret,
                  const uint8_t *write_secret, size_t secret_len);

  bool ReadHandshake();
  bool WriteHandshakeData(enum ssl_encryption_level_t level,
                          const uint8_t *data, size_t len);
  // Returns the number of bytes read.
  int ReadApplicationData(uint8_t *out, size_t max_out);
  bool WriteApplicationData(const uint8_t *in, size_t len);
  bool Flush();

 private:
  bssl::UniquePtr<BIO> bio_;

  std::vector<uint8_t> pending_app_data_;
  size_t app_data_offset_;

  std::vector<std::vector<uint8_t>> read_secrets_;
  std::vector<std::vector<uint8_t>> write_secrets_;

  SSL *ssl_;  // Unowned.
};


#endif  // HEADER_MOCK_QUIC_TRANSPORT
