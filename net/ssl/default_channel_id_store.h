// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_DEFAULT_CHANNEL_ID_STORE_H_
#define NET_SSL_DEFAULT_CHANNEL_ID_STORE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "net/base/net_export.h"
#include "net/ssl/channel_id_store.h"

namespace crypto {
class ECPrivateKey;
}  // namespace crypto

namespace net {

// This class is the system for storing and retrieving Channel IDs. Modeled
// after the CookieMonster class, it has an in-memory store and synchronizes
// Channel IDs to an optional permanent storage that implements the
// PersistentStore interface. The use case is described in
// https://tools.ietf.org/html/draft-balfanz-tls-channelid-01
class NET_EXPORT DefaultChannelIDStore : public ChannelIDStore {
 public:
  class PersistentStore;

  // The key for each ChannelID* in ChannelIDMap is the
  // corresponding server.
  typedef std::map<std::string, ChannelID*> ChannelIDMap;

  // The store passed in should not have had Init() called on it yet. This
  // class will take care of initializing it. The backing store is NOT owned by
  // this class, but it must remain valid for the duration of the
  // DefaultChannelIDStore's existence. If |store| is NULL, then no
  // backing store will be updated.
  explicit DefaultChannelIDStore(PersistentStore* store);

  ~DefaultChannelIDStore() override;

  // ChannelIDStore implementation.
  int GetChannelID(const std::string& server_identifier,
                   std::unique_ptr<crypto::ECPrivateKey>* key_result,
                   const GetChannelIDCallback& callback) override;
  void SetChannelID(std::unique_ptr<ChannelID> channel_id) override;
  void DeleteChannelID(const std::string& server_identifier,
                       const base::Closure& callback) override;
  void DeleteForDomainsCreatedBetween(
      const base::Callback<bool(const std::string&)>& domain_predicate,
      base::Time delete_begin,
      base::Time delete_end,
      const base::Closure& callback) override;
  void DeleteAll(const base::Closure& callback) override;
  void GetAllChannelIDs(const GetChannelIDListCallback& callback) override;
  void Flush() override;
  int GetChannelIDCount() override;
  void SetForceKeepSessionState() override;
  bool IsEphemeral() override;

 private:
  class Task;
  class GetChannelIDTask;
  class SetChannelIDTask;
  class DeleteChannelIDTask;
  class DeleteForDomainsCreatedBetweenTask;
  class GetAllChannelIDsTask;

  // Deletes all of the certs. Does not delete them from |store_|.
  void DeleteAllInMemory();

  // Called by all non-static functions to ensure that the cert store has
  // been initialized.
  // TODO(mattm): since we load asynchronously now, maybe we should start
  // loading immediately on construction, or provide some method to initiate
  // loading?
  void InitIfNecessary() {
    if (!initialized_) {
      if (store_.get()) {
        InitStore();
      } else {
        loaded_ = true;
      }
      initialized_ = true;
    }
  }

  // Initializes the backing store and reads existing certs from it.
  // Should only be called by InitIfNecessary().
  void InitStore();

  // Callback for backing store loading completion.
  void OnLoaded(std::unique_ptr<std::vector<std::unique_ptr<ChannelID>>> certs);

  // Syncronous methods which do the actual work. Can only be called after
  // initialization is complete.
  void SyncSetChannelID(std::unique_ptr<ChannelID> channel_id);
  void SyncDeleteChannelID(const std::string& server_identifier);
  void SyncDeleteForDomainsCreatedBetween(
      const base::Callback<bool(const std::string&)>& domain_predicate,
      base::Time delete_begin,
      base::Time delete_end);
  void SyncGetAllChannelIDs(ChannelIDList* channel_id_list);

  // Add |task| to |waiting_tasks_|.
  void EnqueueTask(std::unique_ptr<Task> task);
  // If already initialized, run |task| immediately. Otherwise add it to
  // |waiting_tasks_|.
  void RunOrEnqueueTask(std::unique_ptr<Task> task);

  // Deletes the channel id for the specified server, if such a channel id
  // exists, from the in-memory store. Deletes it from |store_| if |store_|
  // is not NULL.
  void InternalDeleteChannelID(const std::string& server);

  // Adds the channel id to the in-memory store and adds it to |store_| if
  // |store_| is not NULL.
  void InternalInsertChannelID(std::unique_ptr<ChannelID> channel_id);

  // Indicates whether the channel id store has been initialized. This happens
  // lazily in InitIfNecessary().
  bool initialized_;

  // Indicates whether loading from the backend store is completed and
  // calls may be immediately processed.
  bool loaded_;

  // Tasks that are waiting to be run once we finish loading.
  std::vector<std::unique_ptr<Task>> waiting_tasks_;

  scoped_refptr<PersistentStore> store_;

  ChannelIDMap channel_ids_;

  base::WeakPtrFactory<DefaultChannelIDStore> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DefaultChannelIDStore);
};

typedef base::RefCountedThreadSafe<DefaultChannelIDStore::PersistentStore>
    RefcountedPersistentStore;

class NET_EXPORT DefaultChannelIDStore::PersistentStore
    : public RefcountedPersistentStore {
 public:
  typedef base::Callback<void(
      std::unique_ptr<std::vector<std::unique_ptr<ChannelID>>>)>
      LoadedCallback;

  // Initializes the store and retrieves the existing channel_ids. This will be
  // called only once at startup. Note that the channel_ids are individually
  // allocated and that ownership is transferred to the caller upon return.
  // The |loaded_callback| must not be called synchronously.
  virtual void Load(const LoadedCallback& loaded_callback) = 0;

  virtual void AddChannelID(const ChannelID& channel_id) = 0;

  virtual void DeleteChannelID(const ChannelID& channel_id) = 0;

  virtual void Flush() = 0;

  // When invoked, instructs the store to keep session related data on
  // destruction.
  virtual void SetForceKeepSessionState() = 0;

 protected:
  friend class base::RefCountedThreadSafe<PersistentStore>;

  PersistentStore();
  virtual ~PersistentStore();

 private:
  DISALLOW_COPY_AND_ASSIGN(PersistentStore);
};

}  // namespace net

#endif  // NET_SSL_DEFAULT_CHANNEL_ID_STORE_H_
