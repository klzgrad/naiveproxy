// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/stream_socket.h"

#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"

namespace net {

StreamSocket::UseHistory::UseHistory()
    : was_ever_connected_(false),
      was_used_to_convey_data_(false),
      omnibox_speculation_(false),
      subresource_speculation_(false) {
}

StreamSocket::UseHistory::~UseHistory() {
  EmitPreconnectionHistograms();
}

void StreamSocket::UseHistory::Reset() {
  EmitPreconnectionHistograms();
  was_ever_connected_ = false;
  was_used_to_convey_data_ = false;
  // omnibox_speculation_ and subresource_speculation_ values
  // are intentionally preserved.
}

void StreamSocket::UseHistory::set_was_ever_connected() {
  DCHECK(!was_used_to_convey_data_);
  was_ever_connected_ = true;
}

void StreamSocket::UseHistory::set_was_used_to_convey_data() {
  DCHECK(was_ever_connected_);
  was_used_to_convey_data_ = true;
}

void StreamSocket::UseHistory::set_subresource_speculation() {
  DCHECK(was_ever_connected_);
  subresource_speculation_ = true;
}

void StreamSocket::UseHistory::set_omnibox_speculation() {
  DCHECK(was_ever_connected_);
  omnibox_speculation_ = true;
}

bool StreamSocket::UseHistory::was_used_to_convey_data() const {
  DCHECK(!was_used_to_convey_data_ || was_ever_connected_);
  return was_used_to_convey_data_;
}

void StreamSocket::UseHistory::EmitPreconnectionHistograms() const {
  DCHECK(!subresource_speculation_ || !omnibox_speculation_);
  // 0 ==> non-speculative, never connected.
  // 1 ==> non-speculative never used (but connected).
  // 2 ==> non-speculative and used.
  // 3 ==> omnibox_speculative never connected.
  // 4 ==> omnibox_speculative never used (but connected).
  // 5 ==> omnibox_speculative and used.
  // 6 ==> subresource_speculative never connected.
  // 7 ==> subresource_speculative never used (but connected).
  // 8 ==> subresource_speculative and used.
  int result;
  if (was_used_to_convey_data_)
    result = 2;
  else if (was_ever_connected_)
    result = 1;
  else
    result = 0;  // Never used, and not really connected.

  if (omnibox_speculation_)
    result += 3;
  else if (subresource_speculation_)
    result += 6;
  UMA_HISTOGRAM_ENUMERATION("Net.PreconnectUtilization2", result, 9);
}

StreamSocket::SocketMemoryStats::SocketMemoryStats()
    : total_size(0), buffer_size(0), cert_count(0), cert_size(0) {}

StreamSocket::SocketMemoryStats::~SocketMemoryStats() = default;

}  // namespace net
