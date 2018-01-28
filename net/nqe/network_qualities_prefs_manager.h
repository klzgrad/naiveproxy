// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NQE_NETWORK_QUALITIES_PREFS_MANAGER_H_
#define NET_NQE_NETWORK_QUALITIES_PREFS_MANAGER_H_

#include <map>
#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/base/net_export.h"
#include "net/nqe/cached_network_quality.h"
#include "net/nqe/effective_connection_type.h"
#include "net/nqe/network_id.h"
#include "net/nqe/network_quality_store.h"

namespace base {
class SequencedTaskRunner;
}

namespace net {
class NetworkQualityEstimator;

typedef base::Callback<void(
    const nqe::internal::NetworkID& network_id,
    const nqe::internal::CachedNetworkQuality& cached_network_quality)>
    OnChangeInCachedNetworkQualityCallback;

typedef std::map<nqe::internal::NetworkID, nqe::internal::CachedNetworkQuality>
    ParsedPrefs;

// Using the provided PrefDelegate, NetworkQualitiesPrefsManager creates and
// updates network quality information that is stored in prefs. Instances of
// this class must be constructed on the pref thread, and should later be moved
// to the network thread by calling InitializeOnNetworkThread.
//
// This class interacts with both the pref thread and the network thread, and
// propagates network quality pref changes from the network thread to the
// provided pref delegate on the pref thread.
//
// ShutdownOnPrefSequence must be called from the pref thread before
// destruction.
class NET_EXPORT NetworkQualitiesPrefsManager
    : public nqe::internal::NetworkQualityStore::NetworkQualitiesCacheObserver {
 public:
  // Provides an interface that must be implemented by the embedder.
  class NET_EXPORT PrefDelegate {
   public:
    virtual ~PrefDelegate() {}

    // Sets the persistent pref to the given value.
    virtual void SetDictionaryValue(const base::DictionaryValue& value) = 0;

    // Returns a copy of the persistent prefs.
    virtual std::unique_ptr<base::DictionaryValue> GetDictionaryValue() = 0;
  };

  // Creates an instance of the NetworkQualitiesPrefsManager. Ownership of
  // |pref_delegate| is taken by this class. Must be constructed on the pref
  // thread, and then moved to network thread.
  explicit NetworkQualitiesPrefsManager(
      std::unique_ptr<PrefDelegate> pref_delegate);
  ~NetworkQualitiesPrefsManager() override;

  // Initialize on the Network thread.
  void InitializeOnNetworkThread(
      NetworkQualityEstimator* network_quality_estimator);

  // Prepare for shutdown. Must be called on the pref thread before destruction.
  void ShutdownOnPrefSequence();

  // Clear the network quality estimator prefs.
  void ClearPrefs();

  // Reads the prefs again, parses them into a map of NetworkIDs and
  // CachedNetworkQualities, and returns the map.
  ParsedPrefs ForceReadPrefsForTesting() const;

 private:
  // Pref thread members:
  // Called on pref thread when there is a change in the cached network quality.
  void OnChangeInCachedNetworkQualityOnPrefSequence(
      const nqe::internal::NetworkID& network_id,
      const nqe::internal::CachedNetworkQuality& cached_network_quality);

  // Responsible for writing the persistent prefs to the disk.
  std::unique_ptr<PrefDelegate> pref_delegate_;

  scoped_refptr<base::SequencedTaskRunner> pref_task_runner_;

  // Current prefs on the disk. Should be accessed only on the pref thread.
  std::unique_ptr<base::DictionaryValue> prefs_;

  // Should be accessed only on the pref thread.
  base::WeakPtr<NetworkQualitiesPrefsManager> pref_weak_ptr_;

  // Network thread members:
  // nqe::internal::NetworkQualityStore::NetworkQualitiesCacheObserver
  // implementation:
  void OnChangeInCachedNetworkQuality(
      const nqe::internal::NetworkID& network_id,
      const nqe::internal::CachedNetworkQuality& cached_network_quality)
      override;

  NetworkQualityEstimator* network_quality_estimator_;

  scoped_refptr<base::SequencedTaskRunner> network_task_runner_;

  // Network quality prefs read from the disk at the time of startup. Can be
  // accessed on any thread.
  const ParsedPrefs read_prefs_startup_;

  // Used to get |weak_ptr_| to self on the pref thread.
  base::WeakPtrFactory<NetworkQualitiesPrefsManager> pref_weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(NetworkQualitiesPrefsManager);
};

}  // namespace net

#endif  // NET_NQE_NETWORK_QUALITIES_PREFS_MANAGER_H_
