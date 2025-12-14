// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_SESSION_NAMESPACE_TREE_H_
#define QUICHE_QUIC_MOQT_SESSION_NAMESPACE_TREE_H_

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "quiche/quic/moqt/moqt_messages.h"

namespace moqt {

// Publishers MUST respond with an error if a SUBSCRIBE_NAMESPACE arrives that
// in any way intersects with an existing SUBSCRIBE_NAMESPACE. This requires a
// fairly complex data structure where each part of the tuple is a node. If a
// node has no children, it indicates a complete namespace, and there can be no
// other complete namespaces as direct ancestors or descendants.
// For example, if a/b/c and a/b/d are in the tree, then a/b/e is allowed, but
// a/b and a/b/c/d would not be.
class SessionNamespaceTree {
 public:
  SessionNamespaceTree() = default;
  ~SessionNamespaceTree() {}

  // Returns false if the namespace was not subscribed.
  bool SubscribeNamespace(const TrackNamespace& track_namespace) {
    if (prohibited_namespaces_.contains(track_namespace)) {
      return false;
    }
    TrackNamespace higher_namespace = track_namespace;
    do {
      if (subscribed_namespaces_.contains(higher_namespace)) {
        return false;
      }
    } while (higher_namespace.PopElement());
    subscribed_namespaces_.insert(track_namespace);
    // Add a reference to every higher namespace to block future subscriptions.
    higher_namespace = track_namespace;
    while (higher_namespace.PopElement()) {
      ++prohibited_namespaces_[higher_namespace];
    }
    return true;
  }

  void UnsubscribeNamespace(const TrackNamespace& track_namespace) {
    if (subscribed_namespaces_.erase(track_namespace) == 0) {
      return;
    }
    // Delete one ref from prohibited_namespaces_.
    TrackNamespace higher_namespace = track_namespace;
    while (higher_namespace.PopElement()) {
      auto it2 = prohibited_namespaces_.find(higher_namespace);
      if (it2 == prohibited_namespaces_.end()) {
        continue;
      }
      if (it2->second == 1) {
        prohibited_namespaces_.erase(it2);
      } else {
        --it2->second;
      }
    }
  }

  // Used only when the SessionNamespaceTree is being destroyed.
  const absl::flat_hash_set<TrackNamespace>& GetSubscribedNamespaces() const {
    return subscribed_namespaces_;
  }

 protected:
  uint64_t NumSubscriptions() const { return subscribed_namespaces_.size(); }

 private:
  absl::flat_hash_set<TrackNamespace> subscribed_namespaces_;
  // Namespaces that cannot be subscribed to because they intersect with an
  // existing subscription. The value is a ref count.
  absl::flat_hash_map<TrackNamespace, int> prohibited_namespaces_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_SESSION_NAMESPACE_TREE_H_
