// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_SERVER_ID_MAP_H_
#define QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_SERVER_ID_MAP_H_

#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "quiche/quic/load_balancer/load_balancer_server_id.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

// This class wraps an absl::flat_hash_map which associates server IDs to an
// arbitrary type T. It validates that all server ids are of the same fixed
// length. This might be used by a load balancer to connect a server ID with a
// pool member data structure.
template <typename T>
class QUIC_EXPORT_PRIVATE LoadBalancerServerIdMap {
 public:
  // Returns a newly created pool for server IDs of length |server_id_len|, or
  // nullptr if |server_id_len| is invalid.
  static std::shared_ptr<LoadBalancerServerIdMap> Create(uint8_t server_id_len);

  // Returns the entry associated with |server_id|, if present. For small |T|,
  // use Lookup. For large |T|, use LookupNoCopy.
  absl::optional<const T> Lookup(LoadBalancerServerId server_id) const;
  const T* LookupNoCopy(LoadBalancerServerId server_id) const;

  // Updates the table so that |value| is associated with |server_id|. Sets
  // QUIC_BUG if the length is incorrect for this map.
  void AddOrReplace(LoadBalancerServerId server_id, T value);

  // Removes the entry associated with |server_id|.
  void Erase(const LoadBalancerServerId server_id) {
    server_id_table_.erase(server_id);
  }

  uint8_t server_id_len() const { return server_id_len_; }

 private:
  LoadBalancerServerIdMap(uint8_t server_id_len)
      : server_id_len_(server_id_len) {}

  const uint8_t server_id_len_;  // All server IDs must be of this length.
  absl::flat_hash_map<LoadBalancerServerId, T> server_id_table_;
};

template <typename T>
std::shared_ptr<LoadBalancerServerIdMap<T>> LoadBalancerServerIdMap<T>::Create(
    const uint8_t server_id_len) {
  if (server_id_len == 0 || server_id_len > kLoadBalancerMaxServerIdLen) {
    QUIC_BUG(quic_bug_434893339_01)
        << "Tried to configure map with server ID length "
        << static_cast<int>(server_id_len);
    return nullptr;
  }
  return std::make_shared<LoadBalancerServerIdMap<T>>(
      LoadBalancerServerIdMap(server_id_len));
}

template <typename T>
absl::optional<const T> LoadBalancerServerIdMap<T>::Lookup(
    const LoadBalancerServerId server_id) const {
  if (server_id.length() != server_id_len_) {
    QUIC_BUG(quic_bug_434893339_02)
        << "Lookup with a " << static_cast<int>(server_id.length())
        << " byte server ID, map requires " << static_cast<int>(server_id_len_);
    return absl::optional<T>();
  }
  auto it = server_id_table_.find(server_id);
  return (it != server_id_table_.end()) ? it->second
                                        : absl::optional<const T>();
}

template <typename T>
const T* LoadBalancerServerIdMap<T>::LookupNoCopy(
    const LoadBalancerServerId server_id) const {
  if (server_id.length() != server_id_len_) {
    QUIC_BUG(quic_bug_434893339_02)
        << "Lookup with a " << static_cast<int>(server_id.length())
        << " byte server ID, map requires " << static_cast<int>(server_id_len_);
    return nullptr;
  }
  auto it = server_id_table_.find(server_id);
  return (it != server_id_table_.end()) ? &it->second : nullptr;
}

template <typename T>
void LoadBalancerServerIdMap<T>::AddOrReplace(
    const LoadBalancerServerId server_id, T value) {
  if (server_id.length() == server_id_len_) {
    server_id_table_[server_id] = value;
  } else {
    QUIC_BUG(quic_bug_434893339_03)
        << "Server ID of " << static_cast<int>(server_id.length())
        << " bytes; this map requires " << static_cast<int>(server_id_len_);
  }
}

}  // namespace quic

#endif  // QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_SERVER_ID_MAP_H_
