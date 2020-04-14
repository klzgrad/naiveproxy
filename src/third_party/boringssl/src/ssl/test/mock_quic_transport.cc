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

#include "mock_quic_transport.h"

#include <openssl/span.h>

#include <cstring>
#include <limits>

namespace {

const uint8_t kTagHandshake = 'H';
const uint8_t kTagApplication = 'A';

bool write_header(BIO *bio, uint8_t tag, size_t len) {
  uint8_t header[5];
  header[0] = tag;
  header[1] = (len >> 24) & 0xff;
  header[2] = (len >> 16) & 0xff;
  header[3] = (len >> 8) & 0xff;
  header[4] = len & 0xff;
  return BIO_write_all(bio, header, sizeof(header));
}

}  // namespace

MockQuicTransport::MockQuicTransport(bssl::UniquePtr<BIO> bio, SSL *ssl)
    : bio_(std::move(bio)),
      read_secrets_(ssl_encryption_application + 1),
      write_secrets_(ssl_encryption_application + 1),
      ssl_(ssl) {}

bool MockQuicTransport::SetSecrets(enum ssl_encryption_level_t level,
                                   const uint8_t *read_secret,
                                   const uint8_t *write_secret,
                                   size_t secret_len) {
  if (read_secret) {
    read_secrets_[level].resize(secret_len);
    memcpy(read_secrets_[level].data(), read_secret, secret_len);
  }
  if (write_secret) {
    write_secrets_[level].resize(secret_len);
    memcpy(write_secrets_[level].data(), write_secret, secret_len);
  }
  return true;
}

namespace {

bool ReadAll(BIO *bio, bssl::Span<uint8_t> out) {
  size_t len = out.size();
  uint8_t *buf = out.data();
  while (len > 0) {
    int chunk_len = std::numeric_limits<int>::max();
    if (len <= static_cast<unsigned int>(std::numeric_limits<int>::max())) {
      chunk_len = len;
    }
    int ret = BIO_read(bio, buf, chunk_len);
    if (ret <= 0) {
      return false;
    }
    buf += ret;
    len -= ret;
  }
  return true;
}

bool ReadHeader(BIO *bio, uint8_t *out_tag, size_t *out_len) {
  uint8_t header[5];
  if (!ReadAll(bio, header)) {
    return false;
  }

  *out_len = header[1] << 24 | header[2] << 16 | header[3] << 8 | header[4];
  *out_tag = header[0];
  return true;
}

}  // namespace

bool MockQuicTransport::ReadHandshake() {
  enum ssl_encryption_level_t level = SSL_quic_read_level(ssl_);
  uint8_t tag;
  size_t len;
  if (!ReadHeader(bio_.get(), &tag, &len)) {
    return false;
  }
  if (tag != kTagHandshake) {
    return false;
  }

  const std::vector<uint8_t> &secret = read_secrets_[level];
  std::vector<uint8_t> read_secret(secret.size());
  if (!ReadAll(bio_.get(), bssl::MakeSpan(read_secret))) {
    return false;
  }
  if (read_secret != secret) {
    return false;
  }

  std::vector<uint8_t> buf(len);
  if (!ReadAll(bio_.get(), bssl::MakeSpan(buf))) {
    return false;
  }
  return SSL_provide_quic_data(ssl_, SSL_quic_read_level(ssl_), buf.data(),
                               buf.size());
}

int MockQuicTransport::ReadApplicationData(uint8_t *out, size_t max_out) {
  if (pending_app_data_.size() > 0) {
    size_t len = pending_app_data_.size() - app_data_offset_;
    if (len > max_out) {
      len = max_out;
    }
    memcpy(out, pending_app_data_.data() + app_data_offset_, len);
    app_data_offset_ += len;
    if (app_data_offset_ == pending_app_data_.size()) {
      pending_app_data_.clear();
      app_data_offset_ = 0;
    }
    return len;
  }

  uint8_t tag = 0;
  size_t len;
  while (true) {
    if (!ReadHeader(bio_.get(), &tag, &len)) {
      // Assume that a failure to read the header means there's no more to read,
      // not an error reading.
      return 0;
    }
    if (tag != kTagHandshake && tag != kTagApplication) {
      return -1;
    }
    const std::vector<uint8_t> &secret =
        read_secrets_[ssl_encryption_application];
    std::vector<uint8_t> read_secret(secret.size());
    if (!ReadAll(bio_.get(), bssl::MakeSpan(read_secret))) {
      return -1;
    }
    if (read_secret != secret) {
      return -1;
    }
    if (tag == kTagApplication) {
      break;
    }

    std::vector<uint8_t> buf(len);
    if (!ReadAll(bio_.get(), bssl::MakeSpan(buf))) {
      return -1;
    }
    if (SSL_provide_quic_data(ssl_, SSL_quic_read_level(ssl_), buf.data(),
                              buf.size()) != 1 ||
        SSL_process_quic_post_handshake(ssl_) != 1) {
      return -1;
    }
  }

  uint8_t *buf = out;
  if (len > max_out) {
    pending_app_data_.resize(len);
    buf = pending_app_data_.data();
  }
  app_data_offset_ = 0;
  if (!ReadAll(bio_.get(), bssl::MakeSpan(buf, len))) {
    return -1;
  }
  if (len > max_out) {
    memcpy(out, buf, max_out);
    app_data_offset_ = max_out;
    return max_out;
  }
  return len;
}

bool MockQuicTransport::WriteHandshakeData(enum ssl_encryption_level_t level,
                                           const uint8_t *data, size_t len) {
  const std::vector<uint8_t> &secret = write_secrets_[level];
  if (!write_header(bio_.get(), kTagHandshake, len) ||
      BIO_write_all(bio_.get(), secret.data(), secret.size()) != 1 ||
      BIO_write_all(bio_.get(), data, len) != 1) {
    return false;
  }
  return true;
}

bool MockQuicTransport::WriteApplicationData(const uint8_t *in, size_t len) {
  const std::vector<uint8_t> &secret =
      write_secrets_[ssl_encryption_application];
  if (!write_header(bio_.get(), kTagApplication, len) ||
      BIO_write_all(bio_.get(), secret.data(), secret.size()) != 1 ||
      BIO_write_all(bio_.get(), in, len) != 1) {
    return false;
  }
  return true;
}

bool MockQuicTransport::Flush() { return BIO_flush(bio_.get()); }
