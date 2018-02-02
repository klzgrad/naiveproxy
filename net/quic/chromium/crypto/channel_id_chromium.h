// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CHROMIUM_CRYPTO_CHANNEL_ID_CHROMIUM_H_
#define NET_QUIC_CHROMIUM_CRYPTO_CHANNEL_ID_CHROMIUM_H_

#include <map>
#include <memory>
#include <string>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/quic/core/crypto/channel_id.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace crypto {
class ECPrivateKey;
}  // namespace crypto

namespace net {

class ChannelIDService;

class NET_EXPORT_PRIVATE ChannelIDKeyChromium : public ChannelIDKey {
 public:
  explicit ChannelIDKeyChromium(
      std::unique_ptr<crypto::ECPrivateKey> ec_private_key);
  ~ChannelIDKeyChromium() override;

  // ChannelIDKey interface
  bool Sign(QuicStringPiece signed_data,
            std::string* out_signature) const override;
  std::string SerializeKey() const override;

 private:
  std::unique_ptr<crypto::ECPrivateKey> ec_private_key_;
};

// ChannelIDSourceChromium implements the QUIC ChannelIDSource interface.
class ChannelIDSourceChromium : public ChannelIDSource {
 public:
  explicit ChannelIDSourceChromium(ChannelIDService* channel_id_service);
  ~ChannelIDSourceChromium() override;

  // ChannelIDSource interface
  QuicAsyncStatus GetChannelIDKey(const std::string& hostname,
                                  std::unique_ptr<ChannelIDKey>* channel_id_key,
                                  ChannelIDSourceCallback* callback) override;

 private:
  class Job;

  void OnJobComplete(Job* job);

  // Set owning pointers to active jobs.
  std::map<Job*, std::unique_ptr<Job>> active_jobs_;

  // The service for retrieving Channel ID keys.
  ChannelIDService* const channel_id_service_;

  DISALLOW_COPY_AND_ASSIGN(ChannelIDSourceChromium);
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_CRYPTO_CHANNEL_ID_CHROMIUM_H_
