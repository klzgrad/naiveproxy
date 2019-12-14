// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_STREAM_BODY_MANAGER_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_STREAM_BODY_MANAGER_H_

#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_iovec.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_macros.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

namespace quic {

// All data that a request stream receives falls into one of two categories:
//  * "body", that is, DATA frame payload, which the QuicStreamSequencer must
//    buffer until it is read;
//  * everything else, which QuicSpdyStream immediately processes and thus could
//    be marked as consumed with QuicStreamSequencer, unless there is some piece
//    of body received prior that still needs to be buffered.
// QuicSpdyStreamBodyManager does two things: it keeps references to body
// fragments (owned by QuicStreamSequencer) and offers methods to read them; and
// it calculates the total number of bytes (including non-body bytes) the caller
// needs to mark consumed (with QuicStreamSequencer) when non-body bytes are
// received or when body is consumed.
class QUIC_EXPORT_PRIVATE QuicSpdyStreamBodyManager {
 public:
  QuicSpdyStreamBodyManager();
  ~QuicSpdyStreamBodyManager() = default;

  // One of the following two methods must be called every time data is received
  // on the request stream.

  // Called when data that could immediately be marked consumed with the
  // sequencer (provided that all previous body fragments are consumed) is
  // received.  |length| must be positive.  Returns number of bytes the caller
  // must mark consumed, which might be zero.
  QUIC_MUST_USE_RESULT size_t OnNonBody(QuicByteCount length);

  // Called when body is received.  |body| is added to |fragments_|.  The data
  // pointed to by |body| must be kept alive until an OnBodyConsumed() or
  // ReadBody() call consumes it.  |body| must not be empty.
  void OnBody(QuicStringPiece body);

  // Internally marks |num_bytes| of body consumed.  |num_bytes| might be zero.
  // Returns the number of bytes that the caller should mark consumed with the
  // sequencer, which is the sum of |num_bytes| for body, and the number of any
  // interleaving or immediately trailing non-body bytes.
  QUIC_MUST_USE_RESULT size_t OnBodyConsumed(size_t num_bytes);

  // Set up to |iov_len| elements of iov[] to point to available bodies: each
  // iov[i].iov_base will point to a body fragment, and iov[i].iov_len will be
  // set to its length.  No data is copied, no data is consumed.  Returns the
  // number of iov set.
  int PeekBody(iovec* iov, size_t iov_len) const;

  // Copies data from available bodies into at most |iov_len| elements of iov[].
  // Internally consumes copied body bytes as well as all interleaving and
  // immediately trailing non-body bytes.  |iov.iov_base| and |iov.iov_len| are
  // preassigned and will not be changed.  Returns the total number of bytes the
  // caller shall mark consumed.  Sets |*total_bytes_read| to the total number
  // of body bytes read.
  QUIC_MUST_USE_RESULT size_t ReadBody(const struct iovec* iov,
                                       size_t iov_len,
                                       size_t* total_bytes_read);

  bool HasBytesToRead() const { return !fragments_.empty(); }

  uint64_t total_body_bytes_received() const {
    return total_body_bytes_received_;
  }

 private:
  // A Fragment instance represents a body fragment with a count of bytes
  // received afterwards but before the next body fragment that can be marked
  // consumed as soon as all of the body fragment is read.
  struct Fragment {
    // |body| must not be empty.
    QuicStringPiece body;
    // Might be zero.
    QuicByteCount trailing_non_body_byte_count;
  };
  // Queue of body fragments and trailing non-body byte counts.
  QuicDeque<Fragment> fragments_;
  // Total body bytes received.
  QuicByteCount total_body_bytes_received_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_STREAM_BODY_MANAGER_H_
