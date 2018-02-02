// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_CRYPTO_SERVER_CONFIG_PROTOBUF_H_
#define NET_QUIC_CORE_CRYPTO_CRYPTO_SERVER_CONFIG_PROTOBUF_H_

#include <cstddef>
#include <cstdint>

#include <memory>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/macros.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

// QuicServerConfigProtobuf contains QUIC server config block and the private
// keys needed to prove ownership.
// TODO(rch): sync with server more rationally.
class QUIC_EXPORT_PRIVATE QuicServerConfigProtobuf {
 public:
  // PrivateKey contains a QUIC tag of a key exchange algorithm and a
  // serialised private key for that algorithm. The format of the serialised
  // private key is specific to the algorithm in question.
  class QUIC_EXPORT_PRIVATE PrivateKey {
   public:
    QuicTag tag() const { return tag_; }
    void set_tag(QuicTag tag) { tag_ = tag; }
    std::string private_key() const { return private_key_; }
    void set_private_key(const std::string& key) { private_key_ = key; }

   private:
    QuicTag tag_;
    std::string private_key_;
  };

  QuicServerConfigProtobuf();
  ~QuicServerConfigProtobuf();

  size_t key_size() const { return keys_.size(); }

  const PrivateKey& key(size_t i) const {
    DCHECK_GT(keys_.size(), i);
    return *keys_[i].get();
  }

  std::string config() const { return config_; }

  void set_config(QuicStringPiece config) { config.CopyToString(&config_); }

  QuicServerConfigProtobuf::PrivateKey* add_key() {
    keys_.push_back(std::make_unique<PrivateKey>());
    return keys_.back().get();
  }

  void clear_key() { keys_.clear(); }

  bool has_primary_time() const { return primary_time_ > 0; }

  int64_t primary_time() const { return primary_time_; }

  void set_primary_time(int64_t primary_time) { primary_time_ = primary_time; }

  bool has_priority() const { return priority_ > 0; }

  uint64_t priority() const { return priority_; }

  void set_priority(int64_t priority) { priority_ = priority; }

  bool has_source_address_token_secret_override() const {
    return !source_address_token_secret_override_.empty();
  }

  std::string source_address_token_secret_override() const {
    return source_address_token_secret_override_;
  }

  void set_source_address_token_secret_override(
      QuicStringPiece source_address_token_secret_override) {
    source_address_token_secret_override.CopyToString(
        &source_address_token_secret_override_);
  }

 private:
  std::vector<std::unique_ptr<PrivateKey>> keys_;

  // config_ is a serialised config in QUIC wire format.
  std::string config_;

  // primary_time_ contains a UNIX epoch seconds value that indicates when this
  // config should become primary.
  int64_t primary_time_;

  // Relative priority of this config vs other configs with the same
  // primary time.  For use as a secondary sort key when selecting the
  // primary config.
  uint64_t priority_;

  // Optional override to the secret used to box/unbox source address
  // tokens when talking to clients that select this server config.
  // It can be of any length as it is fed into a KDF before use.
  std::string source_address_token_secret_override_;

  DISALLOW_COPY_AND_ASSIGN(QuicServerConfigProtobuf);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_CRYPTO_SERVER_CONFIG_PROTOBUF_H_
