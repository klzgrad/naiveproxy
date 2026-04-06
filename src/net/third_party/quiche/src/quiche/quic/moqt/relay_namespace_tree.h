// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_RELAY_NAMESPACE_TREE_H_
#define QUICHE_QUIC_MOQT_RELAY_NAMESPACE_TREE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// A data structure for all namespaces an MOQT relay is aware of.
// For any given namespace, it stores all publishers, subscribers, and published
// tracks in that namespace.
// A subscriber must be notified of any publish in a child namespace, and a
// new PUBLISH(_NAMESPACE) has to find subscribers to parent namespaces.
// Therefore, this is a tree structure to easily and scalably move up and down
// the hierarchy to find parents or children.
class RelayNamespaceTree {
 private:
  class RelayNamespaceListener : public MoqtNamespaceTask {
   public:
    // If |tracks| is nullptr, the listener is not interested in PUBLISH
    // messages.
    RelayNamespaceListener(RelayNamespaceTree& tree,
                           const TrackNamespace& prefix,
                           MoqtSessionInterface* absl_nullable tracks)
        : prefix_(prefix),
          tree_(tree),
          session_(tracks),
          weak_ptr_factory_(this) {}
    ~RelayNamespaceListener() override;
    // MoqtNamespaceTask methods.
    void SetObjectsAvailableCallback(ObjectsAvailableCallback
                                     absl_nullable callback) override;
    GetNextResult GetNextSuffix(TrackNamespace& suffix,
                                TransactionType& type) override;
    std::optional<webtransport::StreamErrorCode> GetStatus() override {
      return error_;
    }
    const TrackNamespace& prefix() override { return prefix_; }
    void Update(const MessageParameters& parameters,
                MoqtResponseCallback response_callback) override;

    // Queues a suffix corresponding to a NAMESPACE (if |type| is kAdd) or a
    // NAMESPACE_DONE (if |type| is kDelete).
    void AddPendingSuffix(TrackNamespace suffix, TransactionType type);
    // Publishes a track in this namespace.
    void Publish(TrackNamespace suffix, absl::string_view name);
    void DeclareEof();
    quiche::QuicheWeakPtr<RelayNamespaceListener> GetWeakPtr() {
      return weak_ptr_factory_.Create();
    }

   private:
    struct PendingSuffix {
      TrackNamespace suffix;
      TransactionType type;
    };

    static constexpr size_t kMaxPendingSuffixes = 100;

    const TrackNamespace prefix_;
    RelayNamespaceTree& tree_;
    std::optional<webtransport::StreamErrorCode> error_;
    quiche::QuicheCircularDeque<PendingSuffix> pending_suffixes_;
    MoqtSessionInterface* absl_nullable session_;
    ObjectsAvailableCallback absl_nullable callback_ = nullptr;
    bool eof_ = false;
    bool got_first_pull_ = false;
    // Must be last.
    quiche::QuicheWeakPtrFactory<RelayNamespaceListener> weak_ptr_factory_;
  };

 public:
  // Adds a publisher to the namespace tree. The caller is responsible to call
  // RemovePublisher if it goes away. |session| is stored as a WeakPtr.
  void AddPublisher(TrackNamespace prefix,
                    MoqtSessionInterface* absl_nonnull session);

  void RemovePublisher(const TrackNamespace& prefix,
                       MoqtSessionInterface* absl_nonnull session);

  // Called on incoming SUBSCRIBE_NAMESPACE messages. If track_subscriber is
  // nullptr, it is not interested in PUBLISH messages. If callback is nullptr,
  // it is not interested in NAMESPACE messages. If not interested in,
  // namespaces, will return nullptr. Otherwise, will return a task to flow
  // control published namespaces.
  std::unique_ptr<MoqtNamespaceTask> AddSubscriber(
      const TrackNamespace& prefix,
      MoqtSessionInterface* absl_nullable track_listener);

  // Returns a raw pointer to the session that publishes the smallest namespace
  // that contains |track_namespace|. If a WeakPtr is found to be invalid,
  // deletes it from the tree.
  MoqtSessionInterface* absl_nullable GetValidPublisher(
      TrackNamespace track_namespace);

 protected:
  uint64_t NumNamespaces() const { return nodes_.size(); }

 private:
  struct Node {
    Node() = default;  // The root node has no element.
    explicit Node(absl::string_view element) : element(element) {}
    std::optional<const std::string> element;
    absl::flat_hash_set<Node*> children;

    // For all of the maps below, the key is a raw pointer to the type in the
    // value. This is declared as void* because these raw pointers should NEVER
    // be dereferenced. They are present so that the session or listener can
    // delete itself from the tree quickly by passing a raw pointer to itself.

    // Publishers of this namespace.
    absl::flat_hash_map<void*, quiche::QuicheWeakPtr<MoqtSessionInterface>>
        publishers;
    // The use of a QuicheWeakPtr is out of an abundance of caution.
    // RelayNamespaceListeners should delete themselves from the tree when they
    // go away.
    absl::flat_hash_map<void*, quiche::QuicheWeakPtr<RelayNamespaceListener>>
        listeners;
    // Just store the track name. Additional information will be in the
    // TrackPublisher.
    absl::flat_hash_set<std::string> published_tracks;
    bool CanPrune() const;
  };

  Node* FindNode(const TrackNamespace& track_namespace) const;

  Node* FindOrCreateNode(TrackNamespace track_namespace);

  // Recursive function to notify |listener| of all published namespaces and
  // tracks in and below |node|.
  void NotifyOfAllChildren(Node* node, TrackNamespace& suffix,
                           RelayNamespaceListener* absl_nonnull listener);

  // If |adding| is true, sends NAMESPACE to all subscribers to a
  // parent namespace. If |adding| is false, sends NAMESPACE_DONE.
  void NotifyAllParents(const TrackNamespace& prefix, TransactionType type);

  // If a node has no children, publishers, or subscribers, remove it and see
  // if the same applies to its parent.
  void MaybePrune(Node* node, TrackNamespace track_namespace);

  void RemoveSubscriber(TrackNamespace prefix,
                        MoqtNamespaceTask* absl_nonnull namespace_listener);

  // A map that allows quick access to any namespace without traversing the
  // tree. Use unique_ptr so that it's pointer stable.
  absl::flat_hash_map<TrackNamespace, std::unique_ptr<Node>> nodes_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_RELAY_NAMESPACE_TREE_H_
