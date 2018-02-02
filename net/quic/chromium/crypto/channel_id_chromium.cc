// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/crypto/channel_id_chromium.h"

#include <utility>
#include <vector>

#include "base/strings/string_util.h"
#include "crypto/ec_private_key.h"
#include "crypto/ec_signature_creator.h"
#include "net/base/net_errors.h"
#include "net/cert/asn1_util.h"
#include "net/ssl/channel_id_service.h"

namespace net {

ChannelIDKeyChromium::ChannelIDKeyChromium(
    std::unique_ptr<crypto::ECPrivateKey> ec_private_key)
    : ec_private_key_(std::move(ec_private_key)) {}

ChannelIDKeyChromium::~ChannelIDKeyChromium() {}

bool ChannelIDKeyChromium::Sign(QuicStringPiece signed_data,
                                std::string* out_signature) const {
  std::unique_ptr<crypto::ECSignatureCreator> sig_creator(
      crypto::ECSignatureCreator::Create(ec_private_key_.get()));
  if (!sig_creator) {
    return false;
  }
  const size_t len1 = strlen(ChannelIDVerifier::kContextStr) + 1;
  const size_t len2 = strlen(ChannelIDVerifier::kClientToServerStr) + 1;
  std::vector<uint8_t> data(len1 + len2 + signed_data.size());
  memcpy(&data[0], ChannelIDVerifier::kContextStr, len1);
  memcpy(&data[len1], ChannelIDVerifier::kClientToServerStr, len2);
  memcpy(&data[len1 + len2], signed_data.data(), signed_data.size());
  std::vector<uint8_t> der_signature;
  if (!sig_creator->Sign(&data[0], data.size(), &der_signature)) {
    return false;
  }
  std::vector<uint8_t> raw_signature;
  if (!sig_creator->DecodeSignature(der_signature, &raw_signature)) {
    return false;
  }
  memcpy(base::WriteInto(out_signature, raw_signature.size() + 1),
         &raw_signature[0], raw_signature.size());
  return true;
}

std::string ChannelIDKeyChromium::SerializeKey() const {
  std::string out_key;
  if (!ec_private_key_->ExportRawPublicKey(&out_key)) {
    return std::string();
  }
  return out_key;
}

// A Job handles the lookup of a single channel ID.  It is owned by the
// ChannelIDSource. If the operation can not complete synchronously, it will
// notify the ChannelIDSource upon completion.
class ChannelIDSourceChromium::Job {
 public:
  Job(ChannelIDSourceChromium* channel_id_source,
      ChannelIDService* channel_id_service);

  // Starts the channel ID lookup.  If |QUIC_PENDING| is returned, then
  // |callback| will be invoked asynchronously when the operation completes.
  QuicAsyncStatus GetChannelIDKey(const std::string& hostname,
                                  std::unique_ptr<ChannelIDKey>* channel_id_key,
                                  ChannelIDSourceCallback* callback);

 private:
  enum State {
    STATE_NONE,
    STATE_GET_CHANNEL_ID_KEY,
    STATE_GET_CHANNEL_ID_KEY_COMPLETE,
  };

  int DoLoop(int last_io_result);
  void OnIOComplete(int result);
  int DoGetChannelIDKey(int result);
  int DoGetChannelIDKeyComplete(int result);

  // Channel ID source to notify when this jobs completes.
  ChannelIDSourceChromium* const channel_id_source_;

  ChannelIDService* const channel_id_service_;

  std::unique_ptr<crypto::ECPrivateKey> channel_id_crypto_key_;
  ChannelIDService::Request channel_id_request_;

  // |hostname| specifies the hostname for which we need a channel ID.
  std::string hostname_;

  std::unique_ptr<ChannelIDSourceCallback> callback_;

  std::unique_ptr<ChannelIDKey> channel_id_key_;

  State next_state_;

  DISALLOW_COPY_AND_ASSIGN(Job);
};

ChannelIDSourceChromium::Job::Job(ChannelIDSourceChromium* channel_id_source,
                                  ChannelIDService* channel_id_service)
    : channel_id_source_(channel_id_source),
      channel_id_service_(channel_id_service),
      next_state_(STATE_NONE) {}

QuicAsyncStatus ChannelIDSourceChromium::Job::GetChannelIDKey(
    const std::string& hostname,
    std::unique_ptr<ChannelIDKey>* channel_id_key,
    ChannelIDSourceCallback* callback) {
  DCHECK(channel_id_key);
  DCHECK(callback);

  if (STATE_NONE != next_state_) {
    DLOG(DFATAL) << "GetChannelIDKey has begun";
    return QUIC_FAILURE;
  }

  channel_id_key_.reset();

  hostname_ = hostname;

  next_state_ = STATE_GET_CHANNEL_ID_KEY;
  switch (DoLoop(OK)) {
    case OK:
      *channel_id_key = std::move(channel_id_key_);
      return QUIC_SUCCESS;
    case ERR_IO_PENDING:
      callback_.reset(callback);
      return QUIC_PENDING;
    default:
      channel_id_key->reset();
      return QUIC_FAILURE;
  }
}

int ChannelIDSourceChromium::Job::DoLoop(int last_result) {
  int rv = last_result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_GET_CHANNEL_ID_KEY:
        DCHECK(rv == OK);
        rv = DoGetChannelIDKey(rv);
        break;
      case STATE_GET_CHANNEL_ID_KEY_COMPLETE:
        rv = DoGetChannelIDKeyComplete(rv);
        break;
      case STATE_NONE:
      default:
        rv = ERR_UNEXPECTED;
        LOG(DFATAL) << "unexpected state " << state;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);
  return rv;
}

void ChannelIDSourceChromium::Job::OnIOComplete(int result) {
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    std::unique_ptr<ChannelIDSourceCallback> callback(callback_.release());
    callback->Run(&channel_id_key_);
    // Will delete |this|.
    channel_id_source_->OnJobComplete(this);
  }
}

int ChannelIDSourceChromium::Job::DoGetChannelIDKey(int result) {
  next_state_ = STATE_GET_CHANNEL_ID_KEY_COMPLETE;

  return channel_id_service_->GetOrCreateChannelID(
      hostname_, &channel_id_crypto_key_,
      base::Bind(&ChannelIDSourceChromium::Job::OnIOComplete,
                 base::Unretained(this)),
      &channel_id_request_);
}

int ChannelIDSourceChromium::Job::DoGetChannelIDKeyComplete(int result) {
  DCHECK_EQ(STATE_NONE, next_state_);
  if (result != OK) {
    DLOG(WARNING) << "Failed to look up channel ID: " << ErrorToString(result);
    return result;
  }

  DCHECK(channel_id_crypto_key_);
  channel_id_key_.reset(
      new ChannelIDKeyChromium(std::move(channel_id_crypto_key_)));
  return result;
}

ChannelIDSourceChromium::ChannelIDSourceChromium(
    ChannelIDService* channel_id_service)
    : channel_id_service_(channel_id_service) {}

ChannelIDSourceChromium::~ChannelIDSourceChromium() {
}

QuicAsyncStatus ChannelIDSourceChromium::GetChannelIDKey(
    const std::string& hostname,
    std::unique_ptr<ChannelIDKey>* channel_id_key,
    ChannelIDSourceCallback* callback) {
  std::unique_ptr<Job> job = std::make_unique<Job>(this, channel_id_service_);
  QuicAsyncStatus status =
      job->GetChannelIDKey(hostname, channel_id_key, callback);
  if (status == QUIC_PENDING) {
    Job* job_ptr = job.get();
    active_jobs_[job_ptr] = std::move(job);
  }
  return status;
}

void ChannelIDSourceChromium::OnJobComplete(Job* job) {
  active_jobs_.erase(job);
}

}  // namespace net
