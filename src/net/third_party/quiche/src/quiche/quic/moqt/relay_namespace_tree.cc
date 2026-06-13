// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/relay_namespace_tree.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

RelayNamespaceTree::RelayNamespaceListener::~RelayNamespaceListener() {
  tree_.RemoveSubscriber(prefix_, this);
}

void RelayNamespaceTree::RelayNamespaceListener::SetObjectsAvailableCallback(
    ObjectsAvailableCallback absl_nullable callback) {
  bool first_callback = callback_ == nullptr;
  callback_ = std::move(callback);
  if (first_callback) {
    // If this is the first callback, we need to notify the listener of all
    // published namespaces and tracks in this namespace. Even if |callback| is
    // nullptr, run this to notify of published tracks.
    // A track that published after the task was created and before this
    // function is called will be actually cause two Publish() calls, but the
    // session will ignore the second one.
    TrackNamespace suffix;
    tree_.NotifyOfAllChildren(tree_.FindNode(prefix_), suffix, this);
  }
}

void RelayNamespaceTree::RelayNamespaceListener::Update(
    const MessageParameters&, MoqtResponseCallback response_callback) {
  // Don't do anything!
  std::move(response_callback)(std::nullopt);
}

GetNextResult RelayNamespaceTree::RelayNamespaceListener::GetNextSuffix(
    TrackNamespace& suffix, TransactionType& type) {
  if (eof_) {
    return kEof;
  }
  if (pending_suffixes_.empty()) {
    if (error_.has_value()) {
      return kError;
    }
    return kPending;
  }
  suffix = pending_suffixes_.front().suffix;
  type = pending_suffixes_.front().type;
  pending_suffixes_.pop_front();
  return kSuccess;
}

void RelayNamespaceTree::RelayNamespaceListener::AddPendingSuffix(
    TrackNamespace suffix, TransactionType type) {
  if (callback_ == nullptr) {
    return;  // Not interested in namespaces.
  }
  if (eof_) {
    return;
  }
  if (pending_suffixes_.size() == kMaxPendingSuffixes) {
    error_ = kResetCodeTooFarBehind;
    return;
  }
  pending_suffixes_.push_back({std::move(suffix), type});
  callback_();
}

void RelayNamespaceTree::RelayNamespaceListener::Publish(TrackNamespace,
                                                         absl::string_view) {
  if (session_ == nullptr) {
    return;  // Not interested in tracks.
  }
  // TODO(martinduke): Build a full track name from prefix_, suffix, and name,
  // then call session_->Publish().
}

void RelayNamespaceTree::RelayNamespaceListener::DeclareEof() {
  if (eof_ || error_.has_value()) {
    return;
  }
  eof_ = true;
  callback_();
}

void RelayNamespaceTree::AddPublisher(
    TrackNamespace prefix, MoqtSessionInterface* absl_nonnull session) {
  Node* node = FindOrCreateNode(prefix);
  if (node->publishers.empty()) {
    NotifyAllParents(prefix, TransactionType::kAdd);
  }
  node->publishers[session] = session->GetWeakPtr();
}

void RelayNamespaceTree::RemovePublisher(
    const TrackNamespace& prefix, MoqtSessionInterface* absl_nonnull session) {
  Node* node = FindNode(prefix);
  if (node == nullptr) {
    return;
  }
  node->publishers.erase(session);
  // Tell all the namespace listeners.
  if (node->publishers.empty()) {
    TrackNamespace mutable_namespace = prefix;
    NotifyAllParents(prefix, TransactionType::kDelete);
    MaybePrune(node, mutable_namespace);
  }
}

std::unique_ptr<MoqtNamespaceTask> RelayNamespaceTree::AddSubscriber(
    const TrackNamespace& prefix,
    MoqtSessionInterface* absl_nullable track_listener) {
  Node* node = FindOrCreateNode(prefix);
  auto task =
      std::make_unique<RelayNamespaceListener>(*this, prefix, track_listener);
  node->listeners[task.get()] = task->GetWeakPtr();
  return std::move(task);
}

void RelayNamespaceTree::RemoveSubscriber(
    TrackNamespace prefix, MoqtNamespaceTask* absl_nonnull listener) {
  Node* node = FindNode(prefix);
  if (node == nullptr) {
    return;
  }
  node->listeners.erase(listener);
  MaybePrune(node, prefix);
}

MoqtSessionInterface* absl_nullable RelayNamespaceTree::GetValidPublisher(
    TrackNamespace track_namespace) {
  Node* node;
  do {
    node = FindNode(track_namespace);
    // Remove invalid publishers.
    while (node != nullptr && !node->publishers.empty() &&
           !node->publishers.begin()->second.IsValid()) {
      node->publishers.erase(node->publishers.begin());
    }
    if (node != nullptr && !node->publishers.empty()) {
      return node->publishers.begin()->second.GetIfAvailable();
    }
    MaybePrune(node, track_namespace);
  } while (track_namespace.PopElement());
  return nullptr;
}

bool RelayNamespaceTree::Node::CanPrune() const {
  return children.empty() && publishers.empty() && published_tracks.empty() &&
         listeners.empty();
}

RelayNamespaceTree::Node* RelayNamespaceTree::FindNode(
    const TrackNamespace& track_namespace) const {
  auto it = nodes_.find(track_namespace);
  if (it == nodes_.end()) {
    return nullptr;
  }
  return it->second.get();
}

RelayNamespaceTree::Node* RelayNamespaceTree::FindOrCreateNode(
    TrackNamespace track_namespace) {
  if (track_namespace.number_of_elements() == 0) {  // Root node.
    auto [it, inserted] =
        nodes_.emplace(track_namespace, std::make_unique<Node>());
    return it->second.get();
  }
  auto [it, inserted] = nodes_.emplace(
      track_namespace, std::make_unique<Node>(track_namespace.tuple().back()));
  if (!inserted) {
    return it->second.get();
  }
  Node* node = it->second.get();  // store it in case it moves.
  if (track_namespace.PopElement()) {
    Node* parent = FindOrCreateNode(track_namespace);
    parent->children.insert(node);
  }
  return node;
}

void RelayNamespaceTree::NotifyOfAllChildren(
    Node* node, TrackNamespace& suffix,
    RelayNamespaceListener* absl_nonnull listener) {
  if (!node->publishers.empty()) {
    listener->AddPendingSuffix(suffix, TransactionType::kAdd);
  }
  for (const std::string& track : node->published_tracks) {
    listener->Publish(suffix, track);
  }
  for (auto child = node->children.begin(); child != node->children.end();
       ++child) {
    if (std::optional<absl::string_view> element = (*child)->element) {
      bool success = suffix.AddElement(*element);
      QUICHE_DCHECK(success);
      NotifyOfAllChildren(*child, suffix, listener);
      suffix.PopElement();
    }
  }
}

void RelayNamespaceTree::NotifyAllParents(const TrackNamespace& prefix,
                                          TransactionType type) {
  TrackNamespace mutable_namespace = prefix;
  do {
    Node* node = FindNode(mutable_namespace);
    if (node == nullptr) {
      continue;
    }
    for (const auto& it : node->listeners) {
      RelayNamespaceListener* listener = it.second.GetIfAvailable();
      if (listener == nullptr) {
        QUICHE_BUG(subscriber_is_invalid)
            << "Subscriber WeakPtr is invalid but not removed from the set";
        continue;
      }
      absl::StatusOr<TrackNamespace> suffix =
          prefix.ExtractSuffix(mutable_namespace);
      if (!suffix.ok()) {
        QUICHE_BUG(cannot_extract_suffix) << "Namespace tuple is mangled";
        continue;
      }
      listener->AddPendingSuffix(*suffix, type);
    }
  } while (mutable_namespace.PopElement());
}

void RelayNamespaceTree::MaybePrune(Node* node,
                                    TrackNamespace track_namespace) {
  if (node == nullptr || !node->CanPrune()) {
    return;
  }
  Node* child = node;  // Save the pointer before erasing.
  nodes_.erase(track_namespace);
  // child is now gone, do not dereference!
  if (track_namespace.PopElement()) {
    Node* parent = FindNode(track_namespace);
    QUICHE_BUG_IF(quiche_bug_no_parent_namespace, parent == nullptr)
        << "Parent namespace not found for " << track_namespace;
    if (parent != nullptr) {
      parent->children.erase(child);
      MaybePrune(parent, track_namespace);
    }
  }
}

}  // namespace moqt
