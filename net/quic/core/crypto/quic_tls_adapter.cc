// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/quic_tls_adapter.h"

#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_text_utils.h"

using std::string;

namespace net {

const BIO_METHOD QuicTlsAdapter::kBIOMethod = {
    0,        // type
    nullptr,  // name
    QuicTlsAdapter::BIOWriteWrapper,
    QuicTlsAdapter::BIOReadWrapper,
    nullptr,  // puts
    nullptr,  // gets
    QuicTlsAdapter::BIOCtrlWrapper,
    nullptr,  // create
    nullptr,  // destroy
    nullptr,  // callback_ctrl
};

// static
QuicTlsAdapter* QuicTlsAdapter::GetAdapter(BIO* bio) {
  DCHECK_EQ(&kBIOMethod, bio->method);
  QuicTlsAdapter* adapter = reinterpret_cast<QuicTlsAdapter*>(bio->ptr);
  if (adapter)
    DCHECK_EQ(bio, adapter->bio());
  return adapter;
}

// static
int QuicTlsAdapter::BIOReadWrapper(BIO* bio, char* out, int len) {
  QuicTlsAdapter* adapter = GetAdapter(bio);
  if (!adapter)
    return -1;
  return adapter->Read(out, len);
}

// static
int QuicTlsAdapter::BIOWriteWrapper(BIO* bio, const char* in, int len) {
  QuicTlsAdapter* adapter = GetAdapter(bio);
  if (!adapter)
    return -1;
  return adapter->Write(in, len);
}

// static
// NOLINTNEXTLINE
long QuicTlsAdapter::BIOCtrlWrapper(BIO* bio, int cmd, long larg, void* parg) {
  QuicTlsAdapter* adapter = GetAdapter(bio);
  if (!adapter)
    return 0;
  switch (cmd) {
    // The only control request sent by the TLS stack is from BIO_flush. Any
    // other values of |cmd| would indicate some sort of programming error.
    case BIO_CTRL_FLUSH:
      adapter->Flush();
      return 1;
  }
  QUIC_NOTREACHED();
  return 0;
}

QuicTlsAdapter::QuicTlsAdapter(Visitor* visitor)
    : visitor_(visitor), bio_(BIO_new(&kBIOMethod)) {
  bio_->ptr = this;
  bio_->init = 1;
}

QuicTlsAdapter::~QuicTlsAdapter() {}

QuicErrorCode QuicTlsAdapter::error() const {
  // QuicTlsAdapter passes messages received from ProcessInput straight through
  // to the TLS stack (via the BIO) and does not parse the messages at all.
  // ProcessInput never fails, so there is never an error to provide.
  return QUIC_NO_ERROR;
}

const string& QuicTlsAdapter::error_detail() const {
  return error_detail_;
}

bool QuicTlsAdapter::ProcessInput(QuicStringPiece input,
                                  Perspective perspective) {
  read_buffer_.append(input.data(), input.length());
  visitor_->OnDataAvailableForBIO();
  return true;
}

size_t QuicTlsAdapter::InputBytesRemaining() const {
  return read_buffer_.length();
}

int QuicTlsAdapter::Read(char* out, int len) {
  if (len < 0) {
    return -1;
  }
  if (read_buffer_.empty()) {
    BIO_set_retry_read(bio());
    return -1;
  }
  if (len >= static_cast<int>(read_buffer_.length())) {
    len = read_buffer_.length();
  }
  memcpy(out, read_buffer_.data(), len);
  read_buffer_.erase(0, len);
  QUIC_LOG(INFO) << "BIO_read: reading " << len << " bytes:\n";
  return len;
}

int QuicTlsAdapter::Write(const char* in, int len) {
  if (len < 0) {
    return -1;
  }
  QUIC_LOG(INFO) << "BIO_write: writing " << len << " bytes:\n";
  write_buffer_.append(in, len);
  return len;
}

void QuicTlsAdapter::Flush() {
  QUIC_LOG(INFO) << "BIO_flush: flushing " << write_buffer_.size() << " bytes";
  visitor_->OnDataReceivedFromBIO(write_buffer_);
  write_buffer_.clear();
}

}  // namespace net
