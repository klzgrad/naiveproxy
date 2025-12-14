// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_RELAY_NAMESPACE_TREE_H_
#define QUICHE_QUIC_MOQT_RELAY_NAMESPACE_TREE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

// A data structure for all namespaces an MOQT relay is aware of.
// For any given namespace, it stores all publishers, subscribers, and published
// tracks in that namespace.
// A subscriber must be notified of any publish in a child namespace, and a
// new PUBLISH(_NAMESPACE) has to find subscribers to parent namespaces.
// Therefore, this is a tree structure to easily and scalably move up and down
// the hierarchy to find parents or children.
class RelayNamespaceTree {
 public:
  // Adds a publisher to the namespace tree. The caller is responsible to call
  // RemovePublisher if it goes away. |session| is stored as a WeakPtr.
  void AddPublisher(const TrackNamespace& track_namespace,
                    MoqtSessionInterface* absl_nonnull session) {
    Node* node = FindOrCreateNode(track_namespace);
    if (node->publishers.empty()) {
      NotifyAllParents(track_namespace, /*adding=*/true);
    }
    node->publishers.emplace(session->GetWeakPtr());
  }

  void RemovePublisher(const TrackNamespace& track_namespace,
                       MoqtSessionInterface* absl_nonnull session) {
    Node* node = FindNode(track_namespace);
    if (node == nullptr) {
      return;
    }
    node->publishers.erase(session->GetWeakPtr());
    // Tell all the namespace listeners.
    if (node->publishers.empty()) {
      TrackNamespace mutable_namespace = track_namespace;
      NotifyAllParents(track_namespace, /*adding=*/false);
      MaybePrune(node, mutable_namespace);
    }
  }

  // The caller is responsible to call RemoveNamespaceListener if it goes away.
  // Thus, it is safe to store it as a raw pointer.
  void AddSubscriber(const TrackNamespace& track_namespace,
                     MoqtSessionInterface* absl_nonnull subscriber) {
    Node* node = FindOrCreateNode(track_namespace);
    node->subscribers.insert(subscriber->GetWeakPtr());
    // Notify the listener of every published namespace and track in this
    // namespace.
    TrackNamespace mutable_namespace = track_namespace;
    NotifyOfAllChildren(node, mutable_namespace, subscriber);
  }

  void RemoveSubscriber(const TrackNamespace& track_namespace,
                        MoqtSessionInterface* absl_nonnull subscriber) {
    Node* node = FindNode(track_namespace);
    if (node == nullptr) {
      return;
    }
    node->subscribers.erase(subscriber->GetWeakPtr());
    TrackNamespace mutable_namespace = track_namespace;
    MaybePrune(node, mutable_namespace);
  }

  // Returns a raw pointer to the session that publishes the smallest namespace
  // that contains |track_namespace|. If a WeakPtr is found to be invalid,
  // deletes them from the tree.
  MoqtSessionInterface* GetValidPublisher(
      const TrackNamespace& track_namespace) const {
    Node* node = FindNode(track_namespace);
    TrackNamespace mutable_namespace = track_namespace;
    while ((node == nullptr || node->publishers.empty()) &&
           mutable_namespace.PopElement()) {
      node = FindNode(mutable_namespace);
    }
    if (node == nullptr || node->publishers.empty()) {
      return nullptr;
    }
    MoqtSessionInterface* upstream = node->publishers.begin()->GetIfAvailable();
    if (!upstream) {
      QUICHE_BUG(publisher_is_invalid)
          << "Publisher WeakPtr is invalid but not removed from the set";
      return nullptr;
    }
    return upstream;
  }

 protected:
  uint64_t NumNamespaces() const { return nodes_.size(); }

 private:
  struct Node {
    explicit Node(absl::string_view element) : element(element) {}

    const std::string element;
    absl::flat_hash_set<Node*> children;

    // Publishers of this namespace.
    absl::flat_hash_set<quiche::QuicheWeakPtr<MoqtSessionInterface>> publishers;
    // Just store the track name. Additional information will be in the
    // TrackPublisher.
    absl::flat_hash_set<std::string> published_tracks;
    absl::flat_hash_set<quiche::QuicheWeakPtr<MoqtSessionInterface>>
        subscribers;
    bool CanPrune() const {
      return children.empty() && publishers.empty() &&
             published_tracks.empty() && subscribers.empty();
    }
  };

  Node* FindNode(const TrackNamespace& track_namespace) const {
    auto it = nodes_.find(track_namespace);
    if (it == nodes_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  Node* FindOrCreateNode(const TrackNamespace& track_namespace) {
    auto [it, inserted] =
        nodes_.emplace(track_namespace,
                       std::make_unique<Node>(track_namespace.tuple().back()));
    if (!inserted) {
      return it->second.get();
    }
    Node* node = it->second.get();  // store it in case it moves.
    TrackNamespace mutable_namespace = track_namespace;
    if (mutable_namespace.PopElement()) {
      Node* parent = FindOrCreateNode(mutable_namespace);
      parent->children.insert(node);
    }
    return node;
  }

  // Recursive function to notify |listener| of all published namespaces and
  // tracks in and below |node|.
  void NotifyOfAllChildren(Node* node, TrackNamespace& track_namespace,
                           MoqtSessionInterface* subscriber) {
    // TODO(martinduke): Publish everything in node->published_tracks.
    if (!node->publishers.empty()) {
      subscriber->PublishNamespace(
          track_namespace,
          [](const TrackNamespace&, std::optional<MoqtRequestError>) {},
          // TODO(martinduke): Add parameters.
          VersionSpecificParameters());
    }
    for (auto child = node->children.begin(); child != node->children.end();
         ++child) {
      track_namespace.AddElement((*child)->element);
      NotifyOfAllChildren(*child, track_namespace, subscriber);
      track_namespace.PopElement();
    }
  }

  // If |adding| is true, sends PUBLISH_NAMESPACE to all subscribers to a
  // parent namespace. If |adding| is false, sends PUBLISH_NAMESPACE_DONE.
  void NotifyAllParents(const TrackNamespace& track_namespace, bool adding) {
    TrackNamespace mutable_namespace = track_namespace;
    do {
      Node* node = FindNode(mutable_namespace);
      if (node == nullptr) {
        continue;
      }
      for (const quiche::QuicheWeakPtr<MoqtSessionInterface>& subscriber_ptr :
           node->subscribers) {
        MoqtSessionInterface* subscriber = subscriber_ptr.GetIfAvailable();
        if (subscriber == nullptr) {
          QUICHE_BUG(subscriber_is_invalid)
              << "Subscriber WeakPtr is invalid but not removed from the set";
          continue;
        }
        if (adding) {
          subscriber->PublishNamespace(
              track_namespace,
              [](const TrackNamespace&, std::optional<MoqtRequestError>) {},
              // TODO(martinduke): Add parameters.
              VersionSpecificParameters());
        } else {
          subscriber->PublishNamespaceDone(track_namespace);
        }
      }
    } while (mutable_namespace.PopElement());
  }

  // If a node has no children, publishers, or subscribers, remove it and see
  // if the same applies to its parent.
  void MaybePrune(Node* node, TrackNamespace& track_namespace) {
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

  // A map that allows quick access to any namespace without traversing the
  // tree. Use unique_ptr so that it's pointer stable.
  absl::flat_hash_map<TrackNamespace, std::unique_ptr<Node>> nodes_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_RELAY_NAMESPACE_TREE_H_
