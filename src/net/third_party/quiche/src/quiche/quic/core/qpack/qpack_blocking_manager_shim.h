// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_BLOCKING_MANAGER_SHIM_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_BLOCKING_MANAGER_SHIM_H_

#include <cstdint>
#include <utility>

#include "absl/types/variant.h"
#include "quiche/quic/core/qpack/new_qpack_blocking_manager.h"
#include "quiche/quic/core/qpack/qpack_blocking_manager.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

class QUICHE_EXPORT QpackBlockingManagerShim {
 public:
  struct IndexSet : absl::variant<QpackBlockingManager::IndexSet,
                                  NewQpackBlockingManager::IndexSet> {
    IndexSet() {
      if (use_new_qpack_blocking_manager()) {
        emplace<NewQpackBlockingManager::IndexSet>();
      } else {
        emplace<QpackBlockingManager::IndexSet>();
      }
    }

    QpackBlockingManager::IndexSet& old_variant() {
      QUICHE_DCHECK(!use_new_qpack_blocking_manager());
      return absl::get<QpackBlockingManager::IndexSet>(*this);
    }

    const QpackBlockingManager::IndexSet& old_variant() const {
      QUICHE_DCHECK(!use_new_qpack_blocking_manager());
      return absl::get<QpackBlockingManager::IndexSet>(*this);
    }

    NewQpackBlockingManager::IndexSet& new_variant() {
      QUICHE_DCHECK(use_new_qpack_blocking_manager());
      return absl::get<NewQpackBlockingManager::IndexSet>(*this);
    }

    const NewQpackBlockingManager::IndexSet& new_variant() const {
      QUICHE_DCHECK(use_new_qpack_blocking_manager());
      return absl::get<NewQpackBlockingManager::IndexSet>(*this);
    }

    void insert(uint64_t index) {
      if (use_new_qpack_blocking_manager()) {
        new_variant().insert(index);
      } else {
        old_variant().insert(index);
      }
    }

    bool empty() const {
      if (use_new_qpack_blocking_manager()) {
        return new_variant().empty();
      }
      return old_variant().empty();
    }
  };

  QpackBlockingManagerShim() {
    if (use_new_qpack_blocking_manager()) {
      manager_.emplace<NewQpackBlockingManager>();
    } else {
      manager_.emplace<QpackBlockingManager>();
    }
  }

  bool OnHeaderAcknowledgement(QuicStreamId stream_id) {
    if (use_new_qpack_blocking_manager()) {
      return new_manager().OnHeaderAcknowledgement(stream_id);
    }
    return old_manager().OnHeaderAcknowledgement(stream_id);
  }

  void OnStreamCancellation(QuicStreamId stream_id) {
    if (use_new_qpack_blocking_manager()) {
      new_manager().OnStreamCancellation(stream_id);
    } else {
      old_manager().OnStreamCancellation(stream_id);
    }
  }

  bool OnInsertCountIncrement(uint64_t increment) {
    if (use_new_qpack_blocking_manager()) {
      return new_manager().OnInsertCountIncrement(increment);
    }
    return old_manager().OnInsertCountIncrement(increment);
  }

  void OnHeaderBlockSent(QuicStreamId stream_id, IndexSet indices,
                         uint64_t required_insert_count) {
    if (use_new_qpack_blocking_manager()) {
      new_manager().OnHeaderBlockSent(
          stream_id, std::move(indices.new_variant()), required_insert_count);
    } else {
      old_manager().OnHeaderBlockSent(
          stream_id, std::move(indices.old_variant()), required_insert_count);
    }
  }

  bool blocking_allowed_on_stream(QuicStreamId stream_id,
                                  uint64_t maximum_blocked_streams) const {
    if (use_new_qpack_blocking_manager()) {
      return new_manager().blocking_allowed_on_stream(stream_id,
                                                      maximum_blocked_streams);
    }
    return old_manager().blocking_allowed_on_stream(stream_id,
                                                    maximum_blocked_streams);
  }

  uint64_t smallest_blocking_index() const {
    if (use_new_qpack_blocking_manager()) {
      return new_manager().smallest_blocking_index();
    }
    return old_manager().smallest_blocking_index();
  }

  uint64_t known_received_count() const {
    if (use_new_qpack_blocking_manager()) {
      return new_manager().known_received_count();
    }
    return old_manager().known_received_count();
  }

  static uint64_t RequiredInsertCount(const IndexSet& indices) {
    if (use_new_qpack_blocking_manager()) {
      return NewQpackBlockingManager::RequiredInsertCount(
          indices.new_variant());
    }
    return QpackBlockingManager::RequiredInsertCount(indices.old_variant());
  }

 private:
  static bool use_new_qpack_blocking_manager() {
    static bool use_new = []() {
      bool value = GetQuicRestartFlag(quic_use_new_qpack_blocking_manager);
      if (value) {
        QUIC_RESTART_FLAG_COUNT(quic_use_new_qpack_blocking_manager);
      }
      return value;
    }();
    return use_new;
  }

  QpackBlockingManager& old_manager() {
    return absl::get<QpackBlockingManager>(manager_);
  }
  const QpackBlockingManager& old_manager() const {
    return absl::get<QpackBlockingManager>(manager_);
  }

  NewQpackBlockingManager& new_manager() {
    return absl::get<NewQpackBlockingManager>(manager_);
  }
  const NewQpackBlockingManager& new_manager() const {
    return absl::get<NewQpackBlockingManager>(manager_);
  }

  absl::variant<QpackBlockingManager, NewQpackBlockingManager> manager_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_BLOCKING_MANAGER_SHIM_H_
