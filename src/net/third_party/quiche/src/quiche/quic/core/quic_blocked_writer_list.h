// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_BLOCKED_WRITER_LIST_H_
#define QUICHE_QUIC_CORE_QUIC_BLOCKED_WRITER_LIST_H_

#include "quiche/quic/core/quic_blocked_writer_interface.h"
#include "quiche/common/quiche_linked_hash_map.h"

namespace quic {

// Maintains a list of blocked writers which can be resumed when unblocked.
class QUICHE_EXPORT QuicBlockedWriterList {
 public:
  // Adds `blocked_writer` (which must be write blocked) to the list. If
  // `blocked_writer` is already in the list, this method has no effect.
  void Add(QuicBlockedWriterInterface& blocked_writer);

  // Returns false if there are any blocked writers.
  bool Empty() const;

  // Removes `blocked_writer` to the list. Returns true if `blocked_writer`
  // was in the list and false otherwise.
  bool Remove(QuicBlockedWriterInterface& blocked_writer);

  // Calls `OnCanWrite()` on all the writers in the list.
  void OnWriterUnblocked();

 private:
  // Ideally we'd have a linked_hash_set: the boolean is unused.
  using WriteBlockedList =
      quiche::QuicheLinkedHashMap<QuicBlockedWriterInterface*, bool>;

  // The list of writers waiting to write.
  WriteBlockedList write_blocked_list_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_BLOCKED_WRITER_LIST_H_
